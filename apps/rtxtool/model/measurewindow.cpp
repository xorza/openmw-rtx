#include "measurewindow.hpp"

namespace RtxTool
{
    MeasureWindow::MeasureWindow(const std::uint32_t warmup, const std::uint32_t pauseLimit, const bool played)
        : mWarmup(warmup)
        , mPauseLimit(pauseLimit)
        , mPlayed(played)
    {
    }

    MeasureWindow::Taken MeasureWindow::take(const WindowFrame& frame)
    {
        Taken taken;

        // The world stood whole and the warm-up has run its length since, so this frame opens the
        // measurement.
        if (!mMeasuredFrom.has_value() && mWhole && mWarmedRan == mWarmup)
        {
            mMeasuredFrom = mSeen;
            taken.mOpened = true;
        }

        ++mSeen;

        taken.mClosed = mPendingSpend;
        taken.mClosed.at(Rtx::Timing::Frame) = frame.mSpend.at(Rtx::Timing::Frame);
        taken.mClosed.at(Rtx::Timing::Update) = frame.mSpend.at(Rtx::Timing::Update);
        taken.mClosed.at(Rtx::Timing::Sleep) = frame.mSpend.at(Rtx::Timing::Sleep);
        taken.mClosedArrived = mPendingArrived;
        mPendingSpend = frame.mSpend;
        mPendingArrived = frame.mArrivedMeshes;

        if (mMeasuredFrom.has_value())
        {
            taken.mOutcome = Outcome::Measured;
            taken.mDrawn = mSeen - *mMeasuredFrom;
            return taken;
        }

        if (frame.mPaused && !mPlayed)
        {
            taken.mFirstPause = mWarmedPaused++ == 0;
            if (mWarmedPaused > mPauseLimit)
                taken.mOutcome = Outcome::PausedTooLong;
            return taken;
        }

        // The frame that stood whole is no frame of the warm-up, which is what a stop asks for after it.
        if (mWhole)
        {
            ++mWarmedRan;
            return taken;
        }

        if (mWaited++ == 0 || frame.mCellsToStand < mLeastToStand)
        {
            mLeastToStand = frame.mCellsToStand;
            mStalledMs = 0.0;
        }
        else
            mStalledMs += frame.mSpend.at(Rtx::Timing::Frame);
        mWhole = frame.mWhole;

        // Not in a session somebody plays, which walks where it likes from its first frame and
        // brings cells in as it walks.
        if (!mWhole && !mPlayed && mStalledMs > sStallMs)
            taken.mOutcome = Outcome::Stalled;

        return taken;
    }

    std::optional<std::uint32_t> MeasureWindow::getMeasuredIndex() const
    {
        if (mMeasuredFrom.has_value())
            return mSeen - *mMeasuredFrom;

        if (mWhole && mWarmedRan == mWarmup)
            return 0u;

        return std::nullopt;
    }
}
