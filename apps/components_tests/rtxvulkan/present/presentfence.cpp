#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/present/presentfence.hpp>

namespace Rtx
{
    namespace
    {
        using RtxPresentFenceTest = Testing::DeviceTest;

        /// What a present signals the fence by, stood in for by an empty submit, which signals it
        /// once the queue ran everything before it.
        void signal(const Device& device, VkFence fence)
        {
            ASSERT_EQ(vkQueueSubmit2(device.getQueue(), 0, nullptr, fence), VK_SUCCESS);
        }

        /// **A fence is waited only where a present the presentation engine took owes it.** The
        /// fence is never signalled here unless a step says so, so a wait that was not owed would
        /// end the process after its patience, and every settle below returns at once or not at
        /// all. One millisecond of patience, so the owed case ends in a moment.
        TEST_F(RtxPresentFenceTest, aFenceIsWaitedOnlyWhereAQueuedPresentOwesIt)
        {
            const Device& device = *mHarness.mDevice;

            // An image no present reached yet: the teardown of a swapchain that never presented.
            PresentFence fresh(device);
            fresh.settle(device, "a fence no present was handed", 1'000'000ull);

            // A present that failed ahead of the queue, or one the queue refused: nothing will
            // signal the fence, which `arm` left unsignalled, and nothing waits for it.
            for (const VkResult refused :
                { VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST })
            {
                PresentFence fence(device);
                fence.arm(device);
                fence.answered(refused);
                fence.settle(device, "a fence a refused present was handed", 1'000'000ull);
            }

            // Each answer the specification counts as enqueued owes the wait, which a signal ends,
            // and leaves the fence free for the next `arm` once settled.
            for (const VkResult enqueued : { VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR,
                     VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT })
            {
                PresentFence fence(device);
                const VkFence handle = fence.arm(device);
                fence.answered(enqueued);
                signal(device, handle);
                fence.settle(device, "a fence a queued present was handed");
                EXPECT_EQ(vkGetFenceStatus(device.getHandle(), handle), VK_SUCCESS);

                // Unsignalled again by the next present's `arm`, which a debug build refuses while
                // a wait is still owed.
                EXPECT_EQ(fence.arm(device), handle);
                EXPECT_EQ(vkGetFenceStatus(device.getHandle(), handle), VK_NOT_READY);
            }

            // And the owed wait is a real one: with nothing to signal it, it ends the process.
            Testing::expectDies(
                [&] {
                    PresentFence fence(device);
                    fence.arm(device);
                    fence.answered(VK_SUCCESS);
                    fence.settle(device, "a present nobody queued", 1'000'000ull);
                },
                "a present nobody queued did not complete within 1 ms");
        }
    }
}
