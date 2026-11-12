#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace RtxTool
{
    /// What the processor counted of one thread through a place's measured frames: it says which state a leg's host
    /// rows ran in. Six legs of one build read walk medians of 1.02 to 1.53 ms at a steady clock, and the misses per
    /// thousand instructions moved with them (3.14 to 4.75): a heap laid out worse in one process than another.
    struct ThreadCounts
    {
        std::uint64_t mCycles = 0;
        std::uint64_t mInstructions = 0;

        /// Misses of the last level of cache, the ones that go to memory.
        std::uint64_t mCacheMisses = 0;

        /// How long the thread ran, on any core, which the cycles over it are the clock of: the
        /// longest any group was enabled for it, whether or not the processor counted all of it.
        std::uint64_t mRunningNs = 0;

        /// How much of `mRunningNs` was on the efficiency cores of a part with two kinds, where a
        /// walk runs at about half the speed, scaled up as the counts are.
        std::uint64_t mEfficiencyNs = 0;

        /// The share of the thread's running time the processor counted, the counts scaled up from
        /// it. Below one where the kernel shared the counters with another counting process.
        double mCounted = 0.0;

        /// Whether the cycles count the kernel's time on the thread too, without which they are not
        /// the clock over `mRunningNs`, which does.
        bool mKernelCounted = false;

        /// Whether the processor has two kinds of core, which `mEfficiencyNs` is then the share of.
        bool mTwoKinds = false;

        /// False where nothing was counted, with why.
        bool mRead = false;
        std::string_view mWhyNot{};
    };

    /// What one group of counters read of a thread over a window: its counts, how long the
    /// processor counted it, how long it was enabled for the thread, and whether it is the group of
    /// a part's efficiency cores.
    struct GroupCounts
    {
        std::uint64_t mCycles = 0;
        std::uint64_t mInstructions = 0;
        std::uint64_t mCacheMisses = 0;
        std::uint64_t mRunningNs = 0;
        std::uint64_t mEnabledNs = 0;
        bool mEfficiency = false;
    };

    /// The thread's counts out of what each of its groups read, all scaled to the thread's run: where another
    /// process counted on the same core the kernel shares the counters in turns, so a group counts only part of the
    /// time it was enabled; the counts and the efficiency cores' time are scaled up by the share counted, as perf
    /// does, and the run is the time enabled.
    ThreadCounts countsOf(std::span<const GroupCounts> groups, bool kernelCounted, bool twoKinds);

    /// Counters on the thread that makes them, started and stopped around a place's measured frames. The frame
    /// thread alone: its rows are what the report reads as the host's, and a counter per thread would add each
    /// loader's to the walk's.
    class ThreadCounters
    {
    public:
        /// Opens the counters on the calling thread, stopped. Where the system has none or refuses
        /// them, every count says why.
        ThreadCounters();
        ~ThreadCounters();

        ThreadCounters(const ThreadCounters&) = delete;
        ThreadCounters& operator=(const ThreadCounters&) = delete;

        /// From nought, on the thread that made this.
        void start();

        /// What was counted since `start`, and stops.
        ThreadCounts stop();

    private:
        struct Events;

        std::unique_ptr<Events> mEvents;
        std::string_view mWhyNot;
    };

    /// The counts as one line of the report, without the indent and the line break.
    std::string describeCounts(const ThreadCounts& counts);
}
