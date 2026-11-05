#include "crashhangwatch.hpp"

#include <algorithm>

namespace Crash
{
    namespace
    {
        constexpr std::chrono::seconds sLongestWake{ 2 };

        std::uint32_t wholeSeconds(std::chrono::steady_clock::duration duration)
        {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(duration).count());
        }
    }

    HangWatch::HangWatch(std::chrono::steady_clock::time_point start)
        : mWoke(start)
    {
    }

    bool HangWatch::isNews(std::uint64_t frames) const
    {
        return frames != mLastFrames || !mStarted;
    }

    HangAnswer HangWatch::wake(const HangWake& read)
    {
        const std::chrono::steady_clock::duration step
            = std::min<std::chrono::steady_clock::duration>(read.mNow - mWoke, sLongestWake);
        mWoke = read.mNow;

        const bool counted = mCounting;

        if (isNews(read.mFrames))
        {
            const HangAnswer answer = mReported ? HangAnswer(HangEnded{ wholeSeconds(mStalledFor) }) : HangQuiet{};
            mStarted = mStarted || read.mFrames != 0;
            mLastFrames = read.mFrames;
            mStalledFor = {};
            mReported = false;
            mCounting = read.mLimitSeconds != 0;
            return answer;
        }

        mCounting = !read.mHeld && (read.mLimitSeconds != 0 || mReported);

        if (read.mHeld)
            return HangQuiet{};

        // A stall already reported keeps its count through a limit turned off, which the line for its
        // frames coming again says.
        if (read.mLimitSeconds == 0 && !mReported)
        {
            mStalledFor = {};
            return HangQuiet{};
        }

        if (counted)
            mStalledFor += step;

        const std::uint32_t stalled = wholeSeconds(mStalledFor);
        if (read.mLimitSeconds == 0 || mReported || stalled < read.mLimitSeconds)
            return HangQuiet{};

        mReported = true;
        return HangFound{ stalled, mLastFrames };
    }
}
