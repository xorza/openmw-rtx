#include "longestframes.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <vector>

namespace RtxTool
{
    void LongestFrames::offer(const Rtx::FrameSpend& spend, const Seen& seen)
    {
        const double ms = spend.at(Rtx::Timing::Frame);
        // A heap's front is its greatest, and the greatest under this order is the shortest.
        const auto shorterFirst = [](const Kept& left, const Kept& right) { return left.mMs > right.mMs; };
        if (mKept.size() == sKept)
        {
            if (ms <= mKept.front().mMs)
                return;
            std::ranges::pop_heap(mKept, shorterFirst);
            mKept.pop_back();
        }
        mKept.push_back(Kept{ .mMs = ms,
            .mEndNs = seen.mEndNs,
            .mFrame = seen.mFrame,
            .mLeg = std::string(seen.mLeg),
            .mStretches = Rtx::describeStretches(spend) });
        std::ranges::push_heap(mKept, shorterFirst);
    }

    std::string LongestFrames::describe() const
    {
        if (mKept.empty())
            return {};
        std::vector<const Kept*> order;
        order.reserve(mKept.size());
        for (const Kept& kept : mKept)
            order.push_back(&kept);
        std::ranges::sort(order, [](const Kept* left, const Kept* right) {
            return left->mMs != right->mMs ? left->mMs > right->mMs : left->mEndNs < right->mEndNs;
        });

        std::string text = "\nlongest frames, each ending at a second of the monotonic clock:\n";
        for (const Kept* kept : order)
            std::format_to(std::back_inserter(text), "  {:8.1f} ms  {} frame {}, ended {}.{:09}: {}\n", kept->mMs,
                kept->mLeg, kept->mFrame, kept->mEndNs / 1'000'000'000, kept->mEndNs % 1'000'000'000, kept->mStretches);
        return text;
    }
}
