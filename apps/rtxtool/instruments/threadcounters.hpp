#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace RtxTool
{
    /// What the processor counted of one thread through a place's measured frames.
    ///
    /// **What says which state a leg's host rows ran in.** Six legs of one build at `one-cell-walk`
    /// read a walk median of 1.02 to 1.53 ms at a steady clock, and the misses per thousand
    /// instructions moved with it, 3.14 to 4.75: a heap laid out worse in one process than another.
    /// Without these the report showed the rows move and nothing beside them that moved too.
    struct ThreadCounts
    {
        std::uint64_t mCycles = 0;
        std::uint64_t mInstructions = 0;

        /// Misses of the last level of cache, the ones that go to memory.
        std::uint64_t mCacheMisses = 0;

        /// How long the thread ran, on any core, which the cycles over it are the clock of.
        std::uint64_t mRunningNs = 0;

        /// How much of `mRunningNs` was on the efficiency cores of a part with two kinds, where a
        /// walk runs at about half the speed.
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

    /// Counters on the thread that makes them, started and stopped around a place's measured frames.
    ///
    /// **The frame thread alone**: its rows are what the report reads as the host's, and a counter
    /// per thread of the run would add each loader's to the walk's.
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
