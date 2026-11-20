#include "tourwatch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/misc/constants.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace RtxTool
{
    namespace
    {
        /// How much longer than the frame before a slow frame takes to be a hitch: Unreal's
        /// `t.HitchVersusNonHitchRatio`, so a slow stretch is one hitch and not a hundred.
        constexpr double sHitchRatio = 1.5;

        constexpr std::array<std::string_view, Rtx::sRefusedKinds> sRefusedNames{ "a mesh", "a model", "a texture",
            "a sky layer", "a moon", "a lamp", "an emitter", "an emitter's sprites", "a surface's state" };
    }

    TourWatch::TourWatch(const float stepSeconds, const bool paced, Expectations expected)
        : mBudgetMs(static_cast<double>(stepSeconds) * 1000.0)
        , mPaced(paced)
        , mTable(std::move(expected))
    {
        // The active grid's cells, which the game keeps active at once, with room for a frame that holds an old grid
        // beside a new one.
        constexpr std::size_t sActiveMost
            = 2 * (2 * Constants::CellGridRadius + 1) * (2 * Constants::CellGridRadius + 1);
        mActive.reserve(sActiveMost);
        mLastActive.reserve(sActiveMost);
    }

    void TourWatch::begin(const Stop& stop)
    {
        mLeg = stop.mName;
        mFrame = 0;
        mLastMs = 0.0;
        mLastActive.clear();
        mCovered.clear();
        mSlow = 0;
        mMeasured = 0;
        mStandInFrames = 0;
        mStandInsMost = 0;
        mStandInFirst.reset();
        mBelowWantRun = 0;
        mBelowWantLongest = 0;
        mBelowWantAt.reset();
        mStoodWhole = false;
        mPops.reset();
        mLateStands.reset();
        mLevelSteps.reset();
        mHeld.reset();
        mSlowWorstMs = 0.0;
        mSlowWorst.reset();
    }

    void TourWatch::note(std::optional<Met>& met, const std::uint32_t count, FindingPlaces& places)
    {
        if (!met.has_value())
            met = Met{ .mFirst = places.read(mLeg, mFrame, mAir), .mCount = 0 };
        met->mCount += count;
    }

    void TourWatch::add(const FindingKind kind, const std::string_view detail, FindingPlaces& places, const double ms)
    {
        if (mTable.takesPlace(kind, detail, ms))
            mTable.add(kind, detail, places.read(mLeg, mFrame, mAir), FindingMet{ .mMs = ms });
        else
            mTable.count(kind, detail, ms);
    }

    void TourWatch::addHitch(const ClosedSpan& span, const std::size_t loaded, FindingPlaces& places)
    {
        const double frameMs = span.mSpend.at(Rtx::Timing::Frame);
        describeHitchKey(mHitch, mLeg, span.mSpend);
        if (!mTable.takesPlace(FindingKind::Hitch, mHitch, frameMs))
        {
            mTable.count(FindingKind::Hitch, mHitch, frameMs);
            return;
        }
        mTable.add(FindingKind::Hitch, mHitch, places.read(mLeg, mFrame, mAir),
            FindingMet{ .mMs = frameMs, .mNote = describeHitch(span, mLastMs, loaded) });
    }

    void TourWatch::frame(const WatchFrame& frame, FindingPlaces& places)
    {
        ++mFrame;
        mAir = frame.mAir;
        mLog.at(mLeg, mFrame);

        for (const Rtx::NotFiniteStores& stores : frame.mNotFinite)
            add(FindingKind::NotFinite, stores.mKernel, places);

        // **Each new refusal by its kind and its name**, as the log names it, so an expected one is told apart from
        // another of its kind (`Expectations`). A scene made again begins its record from nothing.
        const std::span<const Rtx::Refusal> met = frame.mScene.refusals().getMet();
        if (met.size() < mRefusalsRead)
            mRefusalsRead = 0;
        for (const Rtx::Refusal& refusal : met.subspan(mRefusalsRead))
        {
            const std::string_view kind = sRefusedNames[static_cast<std::size_t>(refusal.mKind)];
            add(FindingKind::Refused,
                refusal.mName.empty() ? std::format("{}: {}", kind, refusal.mWhy)
                                      : std::format("{} \"{}\": {}", kind, refusal.mName, refusal.mWhy),
                places);
        }
        mRefusalsRead = met.size();

        // How many cells the game made active on this frame: its loads, which run on the frame's own thread.
        mActive.assign(frame.mActiveCells.begin(), frame.mActiveCells.end());
        std::ranges::sort(mActive);
        std::size_t loaded = 0;
        if (!mLastActive.empty())
            for (const void* cell : mActive)
                loaded += std::ranges::binary_search(mLastActive, cell) ? 0 : 1;
        std::swap(mActive, mLastActive);

        // Covered where the world stood whole while the player stood in the cell's column: the rows of a leg's band
        // stand in the active grid together.
        if (frame.mWhole && frame.mStandingColumn.has_value()
            && std::ranges::find(mCovered, *frame.mStandingColumn) == mCovered.end())
            mCovered.push_back(*frame.mStandingColumn);

        // **A pop is a cell that came in where it could be seen**, once the leg stood whole: the frames a cut opens
        // with are the wait's.
        if (mStoodWhole && frame.mCellsAdoptedInReach > 0)
            note(mPops, frame.mCellsAdoptedInReach, places);
        mStoodWhole = mStoodWhole || frame.mWhole;
        if (frame.mHeldAtCrossingMs.has_value())
        {
            if (!mHeld.has_value() || *frame.mHeldAtCrossingMs > mHeld->mWorstMs)
            {
                const Held before = mHeld.value_or(Held{});
                mHeld = Held{ .mWorst = places.read(mLeg, mFrame, mAir),
                    .mWorstMs = *frame.mHeldAtCrossingMs,
                    .mTotalMs = before.mTotalMs,
                    .mCount = before.mCount };
            }
            mHeld->mTotalMs += *frame.mHeldAtCrossingMs;
            ++mHeld->mCount;
        }

        const std::optional<ClosedSpan>& measured = frame.mMeasured;
        if (!measured.has_value())
            return;

        ++mMeasured;
        if (frame.mStandIns > 0)
        {
            if (mStandInFrames++ == 0)
                mStandInFirst = places.read(mLeg, mFrame, mAir);
            mStandInsMost = std::max(mStandInsMost, frame.mStandIns);
        }
        if (frame.mMeshesMissing > 0)
            note(mLateStands, 1, places);
        if (frame.mLevelSteps > 0)
            note(mLevelSteps, frame.mLevelSteps, places);
        mBelowWantRun = frame.mBelowWant > 0 ? mBelowWantRun + 1 : 0;
        if (mBelowWantRun > mBelowWantLongest)
        {
            mBelowWantLongest = mBelowWantRun;
            if (mBelowWantRun == sBelowWantFramesMost + 1)
                mBelowWantAt = places.read(mLeg, mFrame, mAir);
        }

        // As it stood on the screen: a window covered for a minute is no frame of a minute.
        const ClosedSpan seen{ .mSpend = measured->mSpend.seen(), .mArrived = measured->mArrived };
        const double frameMs = seen.mSpend.at(Rtx::Timing::Frame);
        mLongest.offer(seen.mSpend, LongestFrames::Seen{ .mLeg = mLeg, .mFrame = mFrame, .mEndNs = frame.mEndNs });
        if (frameMs > mBudgetMs)
        {
            ++mSlow;
            // Where the worst stood, taken only where a slow frame is a finding: a paced run's are the limiter's.
            if (!mPaced && frameMs > mSlowWorstMs)
            {
                mSlowWorstMs = frameMs;
                mSlowWorst = places.read(mLeg, mFrame, mAir);
            }
            // **Whose it was, said**: `update` is the game's own work between two frames, and a frame the game made
            // cells active on is one it loaded them on.
            if (mLastMs > 0.0 && frameMs > sHitchRatio * mLastMs)
                addHitch(seen, loaded, places);
        }
        mLastMs = frameMs;
    }

    void TourWatch::endLeg(
        const Stop& stop, const BenchPlace& place, const std::span<const Check> failed, FindingPlaces& places)
    {
        drainLog();
        if (stop.mActions.mSoak == Actions::Soak::First)
            mFirstPass = SoakFigures::of(place);
        if (stop.mActions.mSoak == Actions::Soak::Again && mFirstPass.has_value())
            if (const std::optional<std::string> growth = describeGrowth(*mFirstPass, SoakFigures::of(place)))
                mTable.add(FindingKind::Growth, "the closing leg against the first", places.read(mLeg, mFrame, mAir),
                    FindingMet{ .mNote = *growth });

        for (const Check check : failed)
            add(FindingKind::CheckFailed, checkName(check), places);

        const ResidencyMost& residency = place.mResidency;
        if (residency.mStarvedTextures + residency.mStarvedStructures > 0)
            add(FindingKind::Starved,
                std::format("{}: {} textures and {} meshes fitted nowhere at their floors, at the most", mLeg,
                    residency.mStarvedTextures, residency.mStarvedStructures),
                places);
        if (mStandInFrames > 0 && mStandInFirst.has_value())
            mTable.add(FindingKind::StandInDrawn, mLeg, *mStandInFirst,
                FindingMet{ .mNote = std::format("{} of {} measured frames drew a stand-in, {} textures at the most",
                                mStandInFrames, mMeasured, mStandInsMost) });
        if (mBelowWantLongest > sBelowWantFramesMost && mBelowWantAt.has_value())
            mTable.add(FindingKind::BelowWant, mLeg, *mBelowWantAt,
                FindingMet{ .mNote = std::format("a demanded texture below its want for {} measured frames on end",
                                mBelowWantLongest) });

        if (mPops.has_value())
            mTable.add(FindingKind::Pop, mLeg, mPops->mFirst,
                FindingMet{ .mNote = std::format(
                                "{} cells adopted inside the drawn reach after the leg stood whole", mPops->mCount) });
        if (mLateStands.has_value())
            mTable.add(FindingKind::LateStand, mLeg, mLateStands->mFirst,
                FindingMet{ .mNote = std::format("{} of {} measured frames drew a placement whose mesh stood nothing",
                                mLateStands->mCount, mMeasured) });
        if (mLevelSteps.has_value())
            mTable.add(FindingKind::LevelStep, mLeg, mLevelSteps->mFirst,
                FindingMet{ .mNote = std::format(
                                "{} textures raised a level in view over the measured frames", mLevelSteps->mCount) });
        if (mHeld.has_value())
            mTable.add(FindingKind::HeldAtCrossing, mLeg, mHeld->mWorst,
                FindingMet{ .mMs = mHeld->mWorstMs,
                    .mNote = std::format(
                        "{} crossings held for their band, {:.0f} ms in all", mHeld->mCount, mHeld->mTotalMs) });

        if (!mPaced && mSlow > 0 && mSlowWorst.has_value())
        {
            const std::string figures
                = std::format("{} of {} frames over the {:.1f} ms budget", mSlow, mMeasured, mBudgetMs);
            mTable.add(FindingKind::SlowFrames, mLeg, *mSlowWorst, FindingMet{ .mMs = mSlowWorstMs, .mNote = figures });
        }

        for (const osg::Vec2i& cell : stop.mActions.mCovers)
            if (std::ranges::find(mCovered, cell.x()) == mCovered.end())
                add(FindingKind::Outrun,
                    std::format("cell {},{} never stood whole while the player was in its column", cell.x(), cell.y()),
                    places);
    }

    void TourWatch::failLeg(const Stop& stop, const std::string_view why, FindingPlaces& places)
    {
        drainLog();
        add(FindingKind::LegFailed, std::format("{}: {}", stop.mName, why), places);
    }

    void TourWatch::mark(FindingPlaces& places)
    {
        add(FindingKind::Mark, std::format("marked on frame {} of {}", mFrame, mLeg), places);
    }
}
