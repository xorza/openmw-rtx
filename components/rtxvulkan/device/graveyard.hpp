#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

#include <components/rtxvulkan/device/memory/memory.hpp>

#include "owned.hpp"
#include "retiring.hpp"

namespace Rtx
{
    class Device;

    /// Where every device object ends: each handle and each range of memory a submit may still
    /// read is held here until the timeline says that submit has run. Nobody calls this to be safe —
    /// `Owned`, `Buffer`, `Image` and `AccelerationStructure` bury themselves as they go, so an
    /// assignment that replaces a table and a destructor that lets a scene go are both safe while
    /// a frame in flight reads what they replace.
    ///
    /// **One for the queue, the device's own, and each burial is stamped with the value of the next
    /// submit** — which is after every submit already made and is the one a deferred batch rides —
    /// so an object cannot be freed before its last reader, whoever let go of it and whatever frame
    /// was recording. Two graveyards keyed by frame slot were the alternative, and a burial in the
    /// wrong one was a device lost with an invalid read.
    ///
    /// **One queue in burial order, and burial order is free order**: the stamps never fall, so a
    /// wait lets go of a prefix, and what an owner's destructor buried first is ended first — a
    /// structure before the storage it stands in, a view before its image, a handle before the
    /// memory bound to it.
    ///
    /// **Any thread may bury**, because a pipeline a compiling thread fails to finish ends on that
    /// thread. Only the device's own thread collects.
    class Graveyard
    {
    public:
        explicit Graveyard(const Device& device);
        ~Graveyard();

        /// Ends `handle` with `end`, and then gives `memory` back, once every submit made so far
        /// and the next has run.
        void bury(EndHandle end, std::uint64_t handle, DeviceMemory&& memory);

        /// The same for the handle `owned` gives up and the memory bound to it.
        template <class Handle, auto& Destroy>
        void bury(Owned<Handle, Destroy>&& owned, DeviceMemory&& memory)
        {
            bury(&Owned<Handle, Destroy>::end, std::bit_cast<std::uint64_t>(owned.release()), std::move(memory));
        }

        /// Ends what the timeline has been seen to pass. Asked by the device after every wait,
        /// which is where what the timeline knows changes, and by nothing else.
        void collect();

        /// Ends everything held, a burial stamped for a submit nobody has made included: for a
        /// queue nothing is on and nothing is recorded for, which a drain and the teardown are.
        /// The timeline idle is asserted rather than trusted, because the same call one wait too
        /// early is a destroyed object under a submit; that nothing is recorded is the caller's,
        /// through `Device::collectIdle`.
        void collectIdle();

        // Read by the tests and by nothing else.
        std::size_t getHeldCount() const;

    private:
        /// One handle and the memory bound to it, either of which may be empty.
        struct Burial
        {
            EndHandle mEnd = nullptr;
            std::uint64_t mHandle = 0;
            DeviceMemory mMemory;
        };

        /// Ends everything stamped at or below `finished`, in the order it was buried.
        void freeThrough(std::uint64_t finished);

        const Device& mDevice;

        /// Guards `mHeld`, for the burials a compiling thread makes.
        mutable std::mutex mLock;

        Retiring<Burial> mHeld;
    };
}
