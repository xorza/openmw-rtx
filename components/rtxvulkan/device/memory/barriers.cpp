#include "barriers.hpp"

#include <cassert>
#include <cstdint>

#include <volk.h>

namespace Rtx
{
    void Barriers::add(const VkImageMemoryBarrier2& barrier)
    {
        assert(!mImages.empty() && "image barriers into no room");
        if (mImageCount == mImages.size())
            flush();

        mImages[mImageCount++] = barrier;
    }

    void Barriers::add(const VkBufferMemoryBarrier2& barrier)
    {
        if (mBufferCount == mBuffers.size())
            flush();

        mBuffers[mBufferCount++] = barrier;
    }

    void Barriers::add(const VkMemoryBarrier2& barrier)
    {
        if (!mHasMemory)
        {
            mMemory = barrier;
            mHasMemory = true;
            return;
        }

        mMemory.srcStageMask |= barrier.srcStageMask;
        mMemory.srcAccessMask |= barrier.srcAccessMask;
        mMemory.dstStageMask |= barrier.dstStageMask;
        mMemory.dstAccessMask |= barrier.dstAccessMask;
    }

    void Barriers::flush()
    {
        if (mImageCount == 0 && mBufferCount == 0 && !mHasMemory)
            return;

        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = mHasMemory ? 1u : 0u,
            .pMemoryBarriers = &mMemory,
            .bufferMemoryBarrierCount = static_cast<std::uint32_t>(mBufferCount),
            .pBufferMemoryBarriers = mBuffers.data(),
            .imageMemoryBarrierCount = static_cast<std::uint32_t>(mImageCount),
            .pImageMemoryBarriers = mImages.data(),
        };
        vkCmdPipelineBarrier2(mCommands, &dependency);
        ++mEmitted;

        mImageCount = 0;
        mBufferCount = 0;
        mHasMemory = false;
    }
}
