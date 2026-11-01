#include "threadcounters.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <span>

namespace RtxTool
{
    ThreadCounts countsOf(const std::span<const GroupCounts> groups, const bool kernelCounted, const bool twoKinds)
    {
        ThreadCounts counts{ .mKernelCounted = kernelCounted, .mTwoKinds = twoKinds };
        std::uint64_t counted = 0;
        std::uint64_t efficiency = 0;
        for (const GroupCounts& group : groups)
        {
            // Every group is enabled for as long as the thread runs, on whichever kind of core, from
            // its own start: the longest enabled is the run.
            counts.mRunningNs = std::max(counts.mRunningNs, group.mEnabledNs);
            counts.mCycles += group.mCycles;
            counts.mInstructions += group.mInstructions;
            counts.mCacheMisses += group.mCacheMisses;
            counted += group.mRunningNs;
            if (group.mEfficiency)
                efficiency = group.mRunningNs;
        }
        if (counted == 0 || counts.mRunningNs == 0)
            return ThreadCounts{ .mWhyNot = "the processor never counted the thread" };

        // **Below one only where another process counted on the same core.**
        counts.mCounted = static_cast<double>(counted) / static_cast<double>(counts.mRunningNs);
        const auto scaled = [&](const std::uint64_t value) {
            return counts.mCounted < 1.0 ? static_cast<std::uint64_t>(static_cast<double>(value) / counts.mCounted)
                                         : value;
        };
        counts.mCycles = scaled(counts.mCycles);
        counts.mInstructions = scaled(counts.mInstructions);
        counts.mCacheMisses = scaled(counts.mCacheMisses);
        counts.mEfficiencyNs = scaled(efficiency);
        counts.mRead = true;

        return counts;
    }

    std::string describeCounts(const ThreadCounts& counts)
    {
        if (!counts.mRead)
            return std::format("host thread not counted: {}", counts.mWhyNot);
        if (counts.mCycles == 0 || counts.mInstructions == 0 || counts.mRunningNs == 0)
            return "host thread not counted: it ran nothing the counters saw";

        std::string line = "host thread";
        if (counts.mKernelCounted)
            line += std::format(
                " {:.2f} GHz,", static_cast<double>(counts.mCycles) / static_cast<double>(counts.mRunningNs));
        line += std::format(" {:.2f} instructions a cycle, {:.2f} cache misses a thousand instructions",
            static_cast<double>(counts.mInstructions) / static_cast<double>(counts.mCycles),
            1000.0 * static_cast<double>(counts.mCacheMisses) / static_cast<double>(counts.mInstructions));
        if (counts.mTwoKinds)
            line += std::format(", {:.0f}% on efficiency cores",
                100.0 * static_cast<double>(counts.mEfficiencyNs) / static_cast<double>(counts.mRunningNs));
        // Where the share shows at all: the kinds' groups start a microsecond apart, and each move
        // between the two kinds loses a sliver.
        if (counts.mCounted < 0.995)
            line += std::format(", counted {:.0f}% of the time", 100.0 * counts.mCounted);

        return line;
    }
}
