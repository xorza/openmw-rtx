#pragma once

#include <cstdint>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/result.hpp>

namespace Rtx
{
    class Device;

    /// What the presentation engine signals once it let go of a swapchain image, where the device
    /// offers `VK_KHR_swapchain_maintenance1`, and whether a present that signals it is in flight.
    /// The one thing that says a present is over: a queue-idle proves the queue is empty rather
    /// than that the compositor let go, and the timeline cannot say it either.
    ///
    /// **Waited only where a present owes it.** A present the presentation engine never took
    /// signals nothing, and the wait on its fence was ten seconds and a crash report that blamed
    /// the presentation engine for a clean error.
    class PresentFence
    {
    public:
        explicit PresentFence(const Device& device);

        /// Waits for the present this fence was last handed to, where one is in flight, and ends
        /// the process naming `what` where the presentation engine does not answer in `patience`
        /// nanoseconds, a parameter so a test can reach that end.
        void settle(const Device& device, const char* what, std::uint64_t patience = sPatience);

        /// Unsignals the fence for the present about to be queued and hands it over: the last
        /// thing before `vkQueuePresentKHR`, so nothing that fails ahead of the present leaves it
        /// unsignalled.
        VkFence arm(const Device& device);

        /// Takes what `vkQueuePresentKHR` answered for the present `arm` handed the fence to.
        void answered(VkResult result);

    private:
        Fence mFence;
        bool mOwed = false;
    };
}
