#include "growablebuffer.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include <components/rtxvulkan/device/commands.hpp>

#include "barriers.hpp"
#include "imageuse.hpp"

namespace Rtx
{
    bool GrowableBuffer::growTo(const VkDeviceSize bytes)
    {
        assert(mDevice != nullptr && "a buffer grown before its owner said what it is");

        if (!mBuffer.isEmpty() && mBuffer.getSize() >= bytes)
            return false;

        mBuffer = Buffer::make(MemoryUse::Essential, *mDevice, mKind, bytes, mUsage, mName);
        return true;
    }

    bool GrowableBuffer::outgrow(const VkDeviceSize bytes)
    {
        if (!mBuffer.isEmpty() && mBuffer.getSize() >= bytes)
            return false;

        return growTo(std::max(bytes, mBuffer.getSize() * 2));
    }

    void GrowableBuffer::outgrowKeeping(Batch& batch, const VkDeviceSize bytes)
    {
        assert((mUsage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 && (mUsage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0
            && "a table kept across a growth that cannot be copied");
        if (!mBuffer.isEmpty() && mBuffer.getSize() >= bytes)
            return;

        // The old buried after the copy is recorded, under the submit that carries it.
        const Buffer old = std::move(mBuffer);
        growTo(std::max(bytes, old.getSize() * 2));
        if (old.isEmpty())
            return;

        const VkCommandBuffer commands = batch.getCommands();
        old.copyTo(commands, mBuffer, old.getSize());
        handOver(commands, Use::sBufferCopyWrite, Use::sBufferCopyWrite);
    }
}
