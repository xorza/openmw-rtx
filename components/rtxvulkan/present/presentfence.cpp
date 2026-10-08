#include "presentfence.hpp"

#include <cassert>

#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
    namespace
    {
        Fence makeFence(const Device& device)
        {
            const VkFenceCreateInfo create{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0
            };
            return Fence::make(device, vkCreateFence, create, "vkCreateFence");
        }
    }

    PresentFence::PresentFence(const Device& device)
        : mFence(makeFence(device))
    {
    }

    void PresentFence::settle(const Device& device, const char* what, const std::uint64_t patience)
    {
        if (!mOwed)
            return;
        awaitVk(device, mFence.get(), what, patience);
        mOwed = false;
    }

    VkFence PresentFence::arm(const Device& device)
    {
        assert(!mOwed && "a present fence handed to a present before the last present that signals it was waited");
        const VkFence fence = mFence.get();
        checkVk(vkResetFences(device.getHandle(), 1, &fence), "vkResetFences");
        return fence;
    }

    void PresentFence::answered(const VkResult result)
    {
        // What the specification counts as enqueued: success, a suboptimal surface, and the three
        // errors that reject a present after the queue took it, whose wait still runs and whose
        // fence is still signalled. Any other answer queued nothing.
        mOwed = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR
            || result == VK_ERROR_SURFACE_LOST_KHR || result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
    }
}
