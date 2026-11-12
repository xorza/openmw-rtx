#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include <volk.h>

#include "handles.hpp"
#include "owned.hpp"

namespace Rtx
{
    class Device;

    /// The timeline semaphore the queue's clock is, starting at nought.
    Immediate<VkSemaphore, vkDestroySemaphore> makeTimelineSemaphore(const Device& device, std::string_view name);

    /// The queue's one clock: a timeline semaphore that every submit the pool makes signals with
    /// the next value, so "has that finished" is a comparison against a counter and never a fence
    /// somebody has to own. A resource retires against the value of the submit that last named it.
    /// A count of frames, placements or calls never stands in for it: two placements in one frame
    /// made such a count run ahead of the queue.
    ///
    /// The clock is never read off the device on the frame path. What the host knows the queue has
    /// passed is what a wait left behind, and the frame ring waits once a frame, so what a frame
    /// reads is a function of how many frames were drawn and never of how fast the device drew them.
    /// Read off the device, it moved with the wall (sprite list sizes, compaction and reuse of
    /// freed room a frame earlier or later) and two builds of one tree drew three pixels apart,
    /// which `repeat` cannot see because two runs of one binary keep the same phase.
    ///
    /// Every wait is the device's (`Device::waitFor`, `Device::waitIdle`): a wait is where what the
    /// queue may still read changes, and the device is what collects that.
    class Timeline
    {
    public:
        explicit Timeline(const Device& device);

        VkSemaphore getHandle() const { return mHandle.get(); }

        /// The value the next submit will signal — what a batch recorded now and deferred rides,
        /// because the pool puts every deferred batch ahead of its next submit.
        std::uint64_t getNext() const { return mSubmitted.load(std::memory_order_relaxed) + 1; }

        /// Whether the host has waited past `value`: a comparison against what a wait left behind,
        /// and never a question to the device. A value once passed stays passed.
        bool hasFinished(const std::uint64_t value) const { return value <= mFinished; }

        /// Whether the host has waited past every submit made: nothing is on the queue.
        bool isIdle() const { return mFinished == mSubmitted.load(std::memory_order_relaxed); }

        /// The highest value a wait has left behind. What is retired against, once per wait rather
        /// than once per object.
        std::uint64_t getKnownFinished() const { return mFinished; }

        /// The signal a submit puts in its `pSignalSemaphoreInfos` for `value`.
        VkSemaphoreSubmitInfo signal(std::uint64_t value) const;

    private:
        friend class CommandPool;
        friend class Device;

        /// Takes the value the next submit signals, which the pool signals with `signal`. The
        /// pool's alone: a second caller would put the clock ahead of the queue.
        std::uint64_t next() { return mSubmitted.fetch_add(1, std::memory_order_relaxed) + 1; }

        /// Blocks until the queue has signalled `value`. `what` names the wait in the error a
        /// device that stops answering produces. The device's alone, for the reason the class
        /// doc gives.
        void waitFor(std::uint64_t value, const char* what) const;

        /// What a device idle leaves behind: every submit made has run. The device says so after
        /// `vkDeviceWaitIdle`, which is a wait the semaphore is not asked about.
        void markIdle() const;

        const Device& mDevice;

        /// Immediate, because the device takes the graveyard apart before the clock.
        Immediate<VkSemaphore, vkDestroySemaphore> mHandle;

        /// Atomic for the one reader off the device's thread: a burial a compiling thread makes,
        /// which stamps itself with `getNext`. Relaxed, because the value alone is what is read.
        std::atomic<std::uint64_t> mSubmitted = 0;

        /// The highest value a wait has left behind. Mutable because waiting is not a change to
        /// the clock, only to what is known of it.
        mutable std::uint64_t mFinished = 0;
    };
}
