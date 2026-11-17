#include "timeadvancer.hpp"

#include <cstdint>

namespace
{
    // Time per hour tick
    constexpr float kProgressStepDelay = 1.0f / 60.0f;
}

namespace MWGui
{
    TimeAdvancer::TimeAdvancer()
        : mRunning(false)
        , mCurHour(0)
        , mHours(1)
        , mInterruptAt(-1)
        , mRemainingTime(kProgressStepDelay)
    {
    }

    void TimeAdvancer::run(std::int32_t hours, std::int32_t interruptAt)
    {
        mHours = hours;
        mCurHour = 0;
        mInterruptAt = interruptAt;
        mRemainingTime = kProgressStepDelay;

        mRunning = true;
    }

    void TimeAdvancer::stop()
    {
        mRunning = false;
    }

    void TimeAdvancer::onFrame(float dt)
    {
        if (!mRunning)
            return;

        if (mCurHour == mInterruptAt)
        {
            stop();
            eventInterrupted();
            return;
        }

        mRemainingTime -= dt;

        while (mRemainingTime <= 0)
        {
            mRemainingTime += kProgressStepDelay;
            ++mCurHour;

            if (mCurHour <= mHours)
                eventProgressChanged(mCurHour, mHours);
            else
            {
                stop();
                eventFinished();
                return;
            }
        }
    }

    std::int32_t TimeAdvancer::getHours() const
    {
        return mHours;
    }

    bool TimeAdvancer::isRunning() const
    {
        return mRunning;
    }
}
