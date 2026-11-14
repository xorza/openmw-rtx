#include "blockedbuffer.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/requirements.hpp>

#include "barriers.hpp"
#include "batch.hpp"
#include "imageuse.hpp"
#include "memory.hpp"

namespace Rtx
{
    void BlockedBuffer::open(const Device& device, VkBufferUsageFlags usage, std::string_view name)
    {
        assert(mDevice == nullptr && "a blocked buffer opened twice");

        mDevice = &device;

        // A shader reaches a block through a pointer out of the table, and a buffer only has an
        // address if it was created saying so. `TRANSFER_DST` because a block is filled and written
        // by the device rather than by the host: `writeAt` says why.
        mUsage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        // Kept only where a capture could read them: a release build names nothing, and building
        // them is a trip to the heap apiece.
        if constexpr (sDebugNames)
        {
            mName = name;
            mTableName = std::string(name) + " blocks";
        }
    }

    void BlockedBuffer::reserve(Batch& batch, std::uint32_t elements)
    {
        assert(mDevice != nullptr && "a blocked buffer written before it was opened");

        const std::uint32_t wanted
            = std::max(1u, static_cast<std::uint32_t>(alignUp(elements, mBlockSize) / mBlockSize));
        if (wanted <= mBlocks.size())
            return;

        while (mBlocks.size() < wanted)
        {
            std::string name;
            if constexpr (sDebugNames)
                name = mName + " " + std::to_string(mBlocks.size());

            Buffer made = Buffer::deviceLocal(*mDevice, getBlockBytes(), mUsage, name);

            // Zeroed at birth, not left as the allocator found it: a block is longer than what is put in
            // it and holds gaps between the runs handed out, which a picture must not depend on.
            made.clear(batch.getCommands());

            mAddresses.push_back(made.getDeviceAddress());
            mBlocks.push_back(std::move(made));
        }

        // The fills above and the copies after them write the same bytes: a run written into a block this
        // call just emptied is a write after a write, which the queue orders on its own for neither. Found
        // by the layers.
        handOver(batch.getCommands(), Use::sBufferClearWrite,
            BufferUse{ VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });

        // Made again rather than appended to, which is what a table of a few dozen addresses is worth: the
        // address changes, and every frame carries it afresh. Addressable and never bound, because the
        // frame block is how a shader reaches it. Uploaded through the batch into video memory and ordered
        // ahead of what reads it. A frame in flight carries the old one's address and reads it on every
        // hit, which its burial covers.
        mTable = uploadBuffer(
            batch, std::span<const VkDeviceAddress>(mAddresses), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, mTableName);
    }

    void BlockedBuffer::writeInto(Batch& batch, std::uint32_t element, std::span<const std::byte> bytes)
    {
        assert(blockOf(element) < mBlocks.size());

        stageInto(batch, mBlocks[blockOf(element)], offsetOf(element), bytes);
    }
}
