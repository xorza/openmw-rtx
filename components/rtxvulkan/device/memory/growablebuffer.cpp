#include "growablebuffer.hpp"

#include <algorithm>
#include <cassert>

namespace Rtx
{
    bool GrowableBuffer::growTo(const VkDeviceSize bytes)
    {
        assert(mDevice != nullptr && "a buffer grown before its owner said what it is");

        if (!mBuffer.isEmpty() && mBuffer.getSize() >= bytes)
            return false;

        mBuffer = Buffer::make(*mDevice, mKind, bytes, mUsage, mName);
        return true;
    }

    bool GrowableBuffer::outgrow(const VkDeviceSize bytes)
    {
        if (!mBuffer.isEmpty() && mBuffer.getSize() >= bytes)
            return false;

        return growTo(std::max(bytes, mBuffer.getSize() * 2));
    }
}
