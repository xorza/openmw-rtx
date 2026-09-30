#include "guitextures.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <exception>
#include <utility>

#include <components/rtx/common/runs.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    GuiTextures::GuiTextures(const Device& device)
        : mDevice(device)
        , mBatch(device.getPool())
    {
    }

    GuiTextures::~GuiTextures()
    {
        assert((std::uncaught_exceptions() > 0 || std::none_of(mImages.begin(), mImages.end(), [](const Image& image) {
            return !image.isEmpty();
        })) && "a renderer taken apart with an interface texture's slot still out");
    }

    GuiSlot GuiTextures::add(std::uint32_t width, std::uint32_t height)
    {
        Image image(mDevice, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            "gui texture");

        // Cleared rather than left undefined. A slot is sampleable from the moment anything can
        // observe it, so a batch drawn before the first write shows nothing instead of whatever the
        // memory held — and the pass never has to ask whether a texture is ready.
        image.clear(mBatch.getCommands(), Use::sUndefined, VkClearColorValue{}, Use::sFragmentSample);

        if (const Index taken = mFree.take(); taken != sNoIndex)
        {
            mImages[taken] = std::move(image);
            mForms[taken] = AlphaForm::Straight;
            return GuiSlot::at(taken);
        }

        mImages.push_back(std::move(image));
        mForms.push_back(AlphaForm::Straight);
        mCopies.push_back(Copy{ .mBuffer
            = GrowableBuffer(mDevice, BufferKind::ReadBack, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "gui read back") });
        return GuiSlot::at(static_cast<std::uint32_t>(mImages.size() - 1));
    }

    std::span<std::uint8_t> GuiTextures::lend(const GuiSlot slot, const GuiRegion& region)
    {
        assert(mLentSlot.isNone() && "a second lend before the first was sent");
        assert(holds(slot) && "a write to a slot nothing holds");
        assert(mForms[slot.get()] == AlphaForm::Straight && "host bytes into a slot a trace fills");
        assert(region.mX + region.mWidth <= mImages[slot.get()].getWidth()
            && region.mY + region.mHeight <= mImages[slot.get()].getHeight()
            && "a region past the edge of the texture");

        const VkDeviceSize bytes = VkDeviceSize{ region.mWidth } * region.mHeight * 4;
        const StagingLend lent = mBatch.reserve(bytes);

        mLentSlot = slot;
        mLentRegion = region;
        mLentRun = lent.mRun;

        return std::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(lent.mBytes.data()), lent.mBytes.size());
    }

    void GuiTextures::send(const GuiSlot slot)
    {
        assert(mLentSlot == slot && "a send of a slot nothing was lent for");

        const GuiRegion region = mLentRegion;
        mLentSlot = GuiSlot::none();

        if (region.mWidth == 0 || region.mHeight == 0)
            return;

        // The two transitions are what order this against the write before it: copies into one
        // image are otherwise unordered within a submit, and a picture written twice in a frame
        // would land in whichever order the device chose.
        const Image& image = mImages[slot.get()];
        const VkCommandBuffer commands = mBatch.getCommands();

        image.transition(commands, Use::sFragmentSample, Use::sCopyWrite);

        const VkBufferImageCopy copy{
            .bufferOffset = mLentRun.mOffset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { static_cast<std::int32_t>(region.mX), static_cast<std::int32_t>(region.mY), 0 },
            .imageExtent = { region.mWidth, region.mHeight, 1 },
        };
        vkCmdCopyBufferToImage(
            commands, mLentRun.mBuffer, image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        image.transition(commands, Use::sCopyWrite, Use::sFragmentSample);
    }

    void GuiTextures::handOver()
    {
        assert(mLentSlot.isNone() && "a hand over with a lend outstanding");

        mBatch.defer();
    }

    void GuiTextures::finish()
    {
        assert(mLentSlot.isNone() && "a finish with a lend outstanding");

        // Two calls, because the batch's own submit carries what was handed over before it only
        // when there is something left in the batch to submit.
        mBatch.flush();
        mDevice.getPool().finishDeferred();
    }

    void GuiTextures::drop(const GuiSlot slot)
    {
        assert(holds(slot) && "a slot given back twice");

        // Kept on the batch rather than buried here, because a copy recorded against this image
        // may not have run, and the batch is what knows which submit it rides; and not flushed,
        // because that would put a round trip on every window that closes. The draw two frames
        // back that sampled it is before that submit either way.
        mBatch.keep(std::move(mImages[slot.get()]));
        mFree.free(slot.get());

        // The buffer stays for whatever takes the slot next; what was in it is nobody's now.
        mCopies[slot.get()].mRides = Copy::sNever;
    }

    void GuiTextures::readBackWith(const GuiSlot slot, const VkCommandBuffer commands)
    {
        assert(holds(slot) && "a read back of a slot nothing holds");

        const Image& image = mImages[slot.get()];
        const VkDeviceSize bytes = image.getReadBytes();

        // Buried and not destroyed where it has to grow: a batch recorded against it may not have
        // run.
        Copy& copy = mCopies[slot.get()];
        copy.mBuffer.growTo(bytes);

        image.recordRead(commands, Use::sFragmentSample, Use::sFragmentSample, copy.mBuffer.get());

        copy.mRides = mDevice.getTimeline().getNext();
    }

    bool GuiTextures::takeCopy(const GuiSlot slot, const std::span<std::uint8_t> into)
    {
        assert(holds(slot) && "a copy of a slot nothing holds");

        const Copy& copy = mCopies[slot.get()];
        if (copy.mRides == Copy::sNever || !mDevice.getTimeline().hasFinished(copy.mRides))
            return false;

        const std::size_t bytes = std::min<std::size_t>(into.size(), copy.mBuffer.get().getSize());
        std::memcpy(into.data(), copy.mBuffer.get().map(), bytes);
        return true;
    }

    void GuiTextures::read(const GuiSlot slot, std::vector<std::uint8_t>& pixels)
    {
        assert(holds(slot) && "a read of a slot nothing holds");

        // The read back submits and waits for itself, and carries what is handed over here ahead of
        // its own copy — so the bytes it takes off the device are the ones just written.
        handOver();

        mImages[slot.get()].read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pixels);
    }

    VkImageView GuiTextures::getView(const GuiSlot slot)
    {
        handOver();

        if (!holds(slot))
            return VK_NULL_HANDLE;

        return mImages[slot.get()].getView();
    }
}
