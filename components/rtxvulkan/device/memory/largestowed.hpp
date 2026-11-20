#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// **The largest of what one use's owners owe their next growth** (`OwedGrowth`): each owner changes its own
    /// figure, and the largest is read from any thread. What content leaves free for each use before it, one growth
    /// and not every owner's, so that a growth never takes the heap past its budget alone and two of one use at once
    /// make content give way (`docs/architecture.md`, "Content leaves room for one growth a use").
    ///
    /// Bounded by the owners standing, one figure each: a few dozen tables and two structure storages.
    class LargestOwed
    {
    public:
        /// One owner owes `bytes` in place of the `was` it owed before, nought owing nothing. Only a figure the owner
        /// owed before is taken back; only a new figure allocates, which ends the process where it cannot.
        void change(VkDeviceSize was, VkDeviceSize bytes) noexcept;

        /// The largest figure any owner owes, nought where none owes anything.
        VkDeviceSize largest() const noexcept { return mLargest.load(std::memory_order_relaxed); }

    private:
        std::mutex mMutex;

        /// Every owner's figure but nought, ascending.
        std::vector<VkDeviceSize> mOwed;

        std::atomic<VkDeviceSize> mLargest = 0;
    };
}
