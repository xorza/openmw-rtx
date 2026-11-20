#include "framepacer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <components/misc/frameclock.hpp>

namespace MWRender
{
    namespace
    {
        /// The longest a frame is allowed to stand for, whatever the wall says: a frame after a
        /// stall steps the world by this much and no more.
        constexpr FramePacer::Duration sLongestFrame = std::chrono::milliseconds(200);

        double secondsStood(const FramePacer::Duration stood)
        {
            return std::chrono::duration_cast<std::chrono::duration<double>>(std::min(stood, sLongestFrame)).count();
        }
    }

    FramePacer::FramePacer(Misc::LoopClock& clock, const float frameRateLimit, const TimePoint now)
        : mClock(clock)
        , mFrameRateLimit(frameRateLimit)
        , mPeriod(frameRateLimit > 0.0f
                  ? std::chrono::duration_cast<Duration>(std::chrono::duration<float>(1.0f / frameRateLimit))
                  : Duration::zero())
        , mLastDue(now)
    {
        assert(frameRateLimit >= 0.0f && "a negative frame-rate limit, which the setting's sanitizer refuses");
    }

    FramePacer::HoldPlan FramePacer::planHold(const TimePoint now)
    {
        const Duration passed = now - mLastDue;
        const Duration left = mPeriod - passed;
        if (left > Duration::zero())
        {
            mLastDue = now + left;
            return HoldPlan{ .mWait = left, .mStood = mPeriod };
        }

        mLastDue = now;
        return HoldPlan{ .mWait = Duration::zero(), .mStood = passed };
    }

    FramePacer::Duration FramePacer::hold()
    {
        const TimePoint began = std::chrono::steady_clock::now();
        const HoldPlan plan = planHold(began);
        if (plan.mWait > Duration::zero())
            SDL_DelayPrecise(
                static_cast<Uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(plan.mWait).count()));
        mLastHold = std::chrono::steady_clock::now() - began;
        return plan.mStood;
    }

    double FramePacer::openLoop(const Duration stood, const TimePoint now)
    {
        mClock.advance(secondsStood(stood));
        mAside.openLoop(now);
        return mClock.getStep();
    }

    float FramePacer::openNested(const Duration stood, const TimePoint now)
    {
        mAside.openNested(now);

        // **A clock that states its step counts the loop's frames and no others.** How many
        // loading frames a run draws is the wall's answer, and a clock stepped by them would age
        // the resource caches differently in two runs of one build. Such a frame stands for nothing
        // of the world's.
        if (mClock.getStatedStep().has_value())
        {
            mClock.hold();
            return 0.0f;
        }

        mClock.advance(secondsStood(stood));
        return static_cast<float>(mClock.getStep());
    }
}
