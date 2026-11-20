#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

#include "systemshare.hpp"

namespace Resource
{
    class CacheBudget;

    /// One reading of the system's memory: what it leaves the process, nothing where it states none, and what the
    /// process's heap took and holds free, which is the process's own to take again.
    struct SystemReading
    {
        std::optional<std::uint64_t> mAvailable;
        std::uint64_t mHeapFree = 0;
        std::chrono::steady_clock::time_point mAt;
    };

    /// **One sampler of the system's memory for everything that budgets by it** (`docs/architecture.md`, "One
    /// sampler"): each tick destroys what the caches evicted, takes one reading, derives from it both the device's
    /// bound on a device whose memory is the system's (`SystemShare`) and the caches' budget less what the device
    /// claims, and evicts to it. The device's figure reaches it as one its renderer posts once a frame, and its bound
    /// goes back the same way. A tick is its owner's thread's (`ResourceSystem`); the posts and the reads are any
    /// thread's.
    class MemoryArbiter
    {
    public:
        /// How often its owner ticks it.
        static constexpr std::chrono::seconds sPeriod{ 1 };

        /// **How often its owner reads the heap's free memory, beside the system's figure each tick**: glibc's report
        /// walks every arena under its locks, which every thread that allocates meanwhile waits on, and a read of the
        /// system with it took up to 11 ms on the handheld's Repack heap, a hitch a second where a frame allocates
        /// (`research/2026-11-20-handheld-open-issues.md`). What the heap keeps free moves slowly; the system's figure
        /// is read every tick.
        static constexpr std::chrono::seconds sHeapPeriod{ 30 };

        explicit MemoryArbiter(CacheBudget& budget);
        ~MemoryArbiter() = default;

        MemoryArbiter(const MemoryArbiter&) = delete;
        MemoryArbiter& operator=(const MemoryArbiter&) = delete;

        /// What the device whose memory is the system's holds now, nothing for a device of its own memory. Any thread,
        /// once a frame.
        void postDeviceHeld(std::optional<std::uint64_t> held);

        /// What that device is held to, the share's average bound: nothing before a tick read it held something.
        std::optional<std::uint64_t> getDeviceBound() const;

        /// What the system left the process at the last tick, nothing before one or where it states none.
        std::optional<std::uint64_t> getAvailable() const;

        /// One tick: what the caches evicted destroyed before `read` reads the system, since an evicted value still
        /// alive is in no term of the budget; then `take`. How long the read took is kept (`takeSlowestRead`).
        template <class Read>
        void tick(Read read)
        {
            destroyEvicted();
            const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
            const SystemReading reading = read();
            noteRead(std::chrono::steady_clock::now() - began);
            take(reading);
        }

        /// The slowest read of the system since the last ask: the heap's report walks every arena under its locks
        /// (`Platform::heapHeld`), which every thread that allocates meanwhile waits on.
        std::chrono::nanoseconds takeSlowestRead();

        /// The device's share and the caches' budget from `reading`, and the caches evicted to it.
        void take(const SystemReading& reading);

    private:
        void destroyEvicted();
        void noteRead(std::chrono::nanoseconds took);

        CacheBudget& mBudget;

        /// Under it: what the device posted, its share, and the last reading's figure and moment.
        mutable std::mutex mMutex;
        std::optional<std::uint64_t> mDeviceHeld;
        SystemShare mShare;
        std::optional<std::uint64_t> mAvailable;
        std::optional<std::chrono::steady_clock::time_point> mLastAt;
        std::chrono::nanoseconds mSlowestRead{};
    };
}
