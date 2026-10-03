#include "threadcounters.hpp"

#include <format>

namespace RtxTool
{
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
