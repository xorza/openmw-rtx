#pragma once

#include <algorithm>
#include <cstdint>

namespace Rtx
{
    /// How far a background job has come: `mMade` of `mCount` steps — what a loading screen shows
    /// while it waits. Short of the count until the job has returned, whatever its work counted, so
    /// `isDone` is the promise that nothing is left to wait for.
    struct JobProgress
    {
        std::uint32_t mMade = 0;
        std::uint32_t mCount = 0;

        bool isDone() const { return mMade == mCount; }

        /// `made` of `count`, held short of the count until `finished`, whatever the work counted:
        /// the one rule every job's progress is read by. A finished job of no steps is done.
        static constexpr JobProgress of(const std::uint32_t made, const std::uint32_t count, const bool finished)
        {
            if (finished)
                return JobProgress{ .mMade = count, .mCount = count };
            return JobProgress{ .mMade = std::min(made, count == 0 ? 0u : count - 1), .mCount = count };
        }
    };
}
