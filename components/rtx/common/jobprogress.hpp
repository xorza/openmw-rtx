#pragma once

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
    };
}
