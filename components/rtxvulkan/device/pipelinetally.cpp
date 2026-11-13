#include "pipelinetally.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace Rtx
{
    PipelineTally::PipelineTally(const std::chrono::steady_clock::time_point start)
        : mStart(start)
    {
    }

    void PipelineTally::add(const PipelineMade& made, const std::chrono::steady_clock::time_point at)
    {
        const std::scoped_lock lock(mLock);
        ++mSummary.mMade;
        mSummary.mSpan = std::max(mSummary.mSpan, std::chrono::duration<double>(at - mStart));

        switch (made.mFeedback.mSource)
        {
            case PipelineSource::Compiled:
                ++mSummary.mCompiled;
                mSummary.mCompileMs += made.mFeedback.mDurationMs;
                if (!mSummary.mSlowest.has_value() || made.mFeedback.mDurationMs > mSummary.mSlowest->mMs)
                    mSummary.mSlowest
                        = PipelineSlowest{ .mName = std::string(made.mName), .mMs = made.mFeedback.mDurationMs };
                break;
            case PipelineSource::Cached:
                ++mSummary.mCached;
                break;
            case PipelineSource::Unsaid:
                break;
        }

        if (!made.mStatistics.has_value())
            return;

        ++mSummary.mWithStatistics;
        if (made.mStatistics->mExecutables == 0)
            ++mSummary.mWithoutExecutable;
        if (made.mStatistics->mScratchBytes > 0)
            mSummary.mSpills.push_back(
                PipelineSpill{ .mName = std::string(made.mName), .mBytes = made.mStatistics->mScratchBytes });
    }

    PipelineSummary PipelineTally::take(const std::chrono::steady_clock::time_point now)
    {
        const std::scoped_lock lock(mLock);
        PipelineSummary taken = std::exchange(mSummary, PipelineSummary{});
        mStart = now;

        std::ranges::sort(taken.mSpills, [](const PipelineSpill& left, const PipelineSpill& right) {
            return left.mBytes != right.mBytes ? left.mBytes > right.mBytes : left.mName < right.mName;
        });
        return taken;
    }

    std::string describePipelines(const PipelineSummary& summary)
    {
        if (summary.mMade == 0)
            return "none made";

        std::string line = std::format("{} made over {:.2f} s: {} compiled and {} found in the pipeline cache",
            summary.mMade, summary.mSpan.count(), summary.mCompiled, summary.mCached);
        if (const std::uint32_t unsaid = summary.mMade - summary.mCompiled - summary.mCached; unsaid > 0)
            line += std::format(", {} the driver gave no feedback of", unsaid);
        if (summary.mSlowest.has_value())
            line += std::format("; {:.2f} s compiling, the slowest \"{}\" at {:.2f} ms", summary.mCompileMs / 1000.0,
                summary.mSlowest->mName, summary.mSlowest->mMs);
        if (summary.mWithoutExecutable > 0)
            line += std::format("; {} of the {} asked for statistics with no executable the driver reports",
                summary.mWithoutExecutable, summary.mWithStatistics);
        return line;
    }

    std::string describeSpills(const PipelineSummary& summary)
    {
        if (summary.mSpills.empty())
            return {};

        std::string line = std::format(
            "{} of {} pipelines use scratch memory, bytes a lane:", summary.mSpills.size(), summary.mMade);
        for (std::size_t at = 0; at < summary.mSpills.size(); ++at)
            line += std::format(
                "{} \"{}\" {}", at == 0 ? "" : ",", summary.mSpills[at].mName, summary.mSpills[at].mBytes);
        return line;
    }
}
