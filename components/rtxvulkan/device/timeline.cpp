#include "timeline.hpp"

#include <algorithm>
#include <cassert>

#include "device.hpp"
#include "result.hpp"

namespace Rtx
{
    Timeline::Timeline(const Device& device)
        : mDevice(device)
        , mHandle(makeTimelineSemaphore(device, "queue timeline"))
    {
    }

    void Timeline::waitFor(const std::uint64_t value, const char* const what) const
    {
        // A value past every submit is one nothing will signal: the wait would sit out its patience
        // and blame the device for a call made out of its turn.
        assert(value <= mSubmitted.load(std::memory_order_relaxed) && "a wait for a value no submit signals");
        if (value <= mFinished)
            return;

        const VkSemaphore handle = mHandle.get();
        const VkSemaphoreWaitInfo wait{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &handle,
            .pValues = &value,
        };
        checkVkWait(mDevice, vkWaitSemaphores(mDevice.getHandle(), &wait, sPatience), what, sPatience);
        mFinished = std::max(mFinished, value);
    }

    void Timeline::markIdle() const
    {
        mFinished = mSubmitted.load(std::memory_order_relaxed);
    }

    VkSemaphoreSubmitInfo Timeline::signal(const std::uint64_t value) const
    {
        return VkSemaphoreSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = mHandle.get(),
            .value = value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        };
    }
}
