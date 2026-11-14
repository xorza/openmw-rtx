#include "batch.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "barriers.hpp"
#include "imageuse.hpp"
#include "memory.hpp"
#include "stagingring.hpp"

namespace Rtx
{
    Batch::Batch(CommandPool& pool)
        : mPool(pool)
        , mHold(pool.takeHold())
    {
    }

    const Device& Batch::getDevice() const
    {
        return mPool.getDevice();
    }

    Batch::~Batch()
    {
        assert((mCommands == VK_NULL_HANDLE || std::uncaught_exceptions() > 0)
            && "a batch that recorded something was neither flushed nor deferred");

        // Through `tearDown`, because this runs while an exception unwinds, and a reset the driver
        // refuses would end the process there. A hold whose release failed is kept from the pool.
        tearDown("a batch would not give back what it held", [&] {
            if (mCommands != VK_NULL_HANDLE)
                mPool.discard(std::exchange(mCommands, VK_NULL_HANDLE));
            release();
            mPool.giveHold(mHold);
        });
    }

    VkCommandBuffer Batch::getCommands()
    {
        if (mCommands == VK_NULL_HANDLE)
            mCommands = mPool.beginBatch();

        return mCommands;
    }

    void Batch::keep(Buffer&& buffer)
    {
        mPool.holdAt(mHold).mBuffers.push_back(std::move(buffer));
    }

    void Batch::keep(Image&& image)
    {
        mPool.holdAt(mHold).mImages.push_back(std::move(image));
    }

    StagingRun Batch::stage(std::span<const std::byte> bytes)
    {
        const StagingLend lent = reserve(bytes.size());
        std::copy(bytes.begin(), bytes.end(), lent.mBytes.begin());
        return lent.mRun;
    }

    StagingLend Batch::reserve(const VkDeviceSize bytes)
    {
        VkDeviceSize at = alignUp(mFilled, sStagingAlignment);

        std::vector<std::size_t>& blocks = mPool.holdAt(mHold).mBlocks;
        if (blocks.empty() || at + bytes > mPool.getStaging().at(blocks.back()).getSize())
        {
            blocks.push_back(mPool.getStaging().take(getDevice(), bytes));
            at = 0;
        }

        const Buffer& block = mPool.getStaging().at(blocks.back());
        mFilled = at + bytes;

        return StagingLend{
            .mRun = StagingRun{ .mBuffer = block.getHandle(), .mOffset = at },
            .mBytes = block.writable<std::byte>(at, bytes),
        };
    }

    void Batch::stageRuns(
        const Buffer& into, const std::span<const std::byte> source, const std::span<const CopyRun> runs)
    {
        VkDeviceSize bytes = 0;
        for (const CopyRun& run : runs)
        {
            assert(run.mFrom + run.mSize <= source.size() && "a run past the end of what it is written from");
            assert(run.mTo + run.mSize <= into.getSize() && "a run past the end of the buffer it is written into");
            bytes += run.mSize;
        }
        if (bytes == 0)
            return;

        const StagingLend lent = reserve(bytes);
        std::vector<VkBufferCopy>& regions = mPool.holdAt(mHold).mRegions;
        regions.clear();
        VkDeviceSize packed = 0;
        for (const CopyRun& run : runs)
        {
            if (run.mSize == 0)
                continue;

            const std::span<const std::byte> from = source.subspan(run.mFrom, run.mSize);
            std::copy(from.begin(), from.end(), lent.mBytes.begin() + static_cast<std::ptrdiff_t>(packed));
            regions.push_back(
                VkBufferCopy{ .srcOffset = lent.mRun.mOffset + packed, .dstOffset = run.mTo, .size = run.mSize });
            packed += run.mSize;
        }

        vkCmdCopyBuffer(getCommands(), lent.mRun.mBuffer, into.getHandle(), static_cast<std::uint32_t>(regions.size()),
            regions.data());

        // Named, as `stageInto` names its destination: a host write over it while the copy is on
        // the queue would be two writers racing.
        into.nameForNext();
    }

    void Batch::release()
    {
        // What the batch kept buries itself as the lists empty, under the submit the batch rides.
        BatchHold& hold = mPool.holdAt(mHold);
        hold.mBuffers.clear();
        hold.mImages.clear();

        // Under the same value a burial is, for the same reason.
        const std::uint64_t readUntil = getDevice().getTimeline().getNext();
        for (const std::size_t block : hold.mBlocks)
            mPool.getStaging().give(block, readUntil);
        hold.mBlocks.clear();
        mFilled = 0;
    }

    void Batch::flush()
    {
        // Buried ahead of the submit, so the stamp is the value the wait below waits for, and the
        // wait's own collect is what frees them. Nothing recorded is a caller that kept something
        // and decided against the copy; what it kept may still have an earlier reader, and the
        // burial covers that too.
        release();

        if (mCommands != VK_NULL_HANDLE)
            mPool.endAndWait(std::exchange(mCommands, VK_NULL_HANDLE));
    }

    void Batch::defer()
    {
        // Buried under the next submit, which is the one the pool puts this batch ahead of.
        release();

        if (mCommands != VK_NULL_HANDLE)
            mPool.defer(std::exchange(mCommands, VK_NULL_HANDLE));
    }

    void Batch::abandon()
    {
        release();

        if (mCommands != VK_NULL_HANDLE)
            mPool.discard(std::exchange(mCommands, VK_NULL_HANDLE));
    }

    void stageInto(Batch& batch, const Buffer& into, VkDeviceSize offset, std::span<const std::byte> bytes)
    {
        const StagingRun staged = batch.stage(bytes);
        const VkBufferCopy region{
            .srcOffset = staged.mOffset,
            .dstOffset = offset,
            .size = bytes.size(),
        };
        vkCmdCopyBuffer(batch.getCommands(), staged.mBuffer, into.getHandle(), 1, &region);

        // A copy takes a handle and names nothing on its own, and a host write over the destination
        // while the copy is still on the queue is a race between two writers: named, so `isIdle`
        // says so. The source needs no stamp: a staging block is the ring's, and the batch gives it
        // back stamped with the submit that carries it.
        into.nameForNext();
    }

    Buffer uploadBuffer(Batch& batch, std::span<const std::byte> bytes, VkBufferUsageFlags usage, std::string_view name)
    {
        Buffer result
            = Buffer::deviceLocal(batch.getDevice(), bytes.size(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, name);
        stageInto(batch, result, 0, bytes);

        // What makes an upload self-contained. Batched, the next thing recorded may be an
        // acceleration structure built out of exactly these bytes, and without this it would read
        // them before the copy had run.
        result.transition(batch.getCommands(), Use::sBufferCopyWrite, Use::sBufferAnyRead);

        return result;
    }

    void uploadImage(Batch& batch, Image& image, std::span<const std::byte> bytes, std::span<VkBufferImageCopy> regions)
    {
        const StagingRun staged = batch.stage(bytes);
        for (VkBufferImageCopy& region : regions)
            region.bufferOffset += staged.mOffset;

        const VkCommandBuffer commands = batch.getCommands();

        image.transition(commands, Use::sUndefined, Use::sCopyWrite);

        vkCmdCopyBufferToImage(commands, staged.mBuffer, image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<std::uint32_t>(regions.size()), regions.data());

        image.transition(commands, Use::sCopyWrite, Use::sTextureSample);
    }

    void orderStagedWrites(Batch& batch)
    {
        handOver(batch.getCommands(), Use::sBufferCopyWrite, Use::sBufferAnyReadWrite);
    }
}
