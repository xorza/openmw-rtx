#include "tourwatch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/rtx/worldmirror.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/world/weather.hpp>

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

    TourWatch::TourWatch(const float stepSeconds, const bool paced)
        : mBudgetMs(static_cast<double>(stepSeconds) * 1000.0)
        , mPaced(paced)
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
        mSlowWorstMs = 0.0;
        mSlowWorst.reset();
    }

    FindingPlace TourWatch::placeOf(StandingNote& note)
    {
        note.take(mAir);
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        FindingPlace place{ .mLeg = mLeg, .mFrame = mFrame };
        if (const Stop* stood = note.getLeft())
            place.mStood = *stood;
        place.mLeft = Rtx::weatherOfScriptId(world.getCurrentWeatherScriptId()).value_or(Rtx::Weather::Clear);
        place.mArriving = Rtx::weatherOfScriptId(world.getNextWeatherScriptId()).value_or(place.mLeft);
        place.mCrossed = 1.0f - world.getWeatherTransition();
        return place;
    }

    void TourWatch::add(const FindingKind kind, const std::string_view detail, StandingNote& note, const double ms)
    {
        if (mTable.holds(kind, detail))
            mTable.again(kind, detail, ms);
        else
            mTable.add(kind, detail, placeOf(note), ms);
    }

    void TourWatch::frame(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
        StandingNote& note, const bool measured)
    {
        ++mFrame;
        mAir = report.mAir;
        mLog.at(mLeg, mFrame);

        if (report.mResult.has_value())
            for (const Rtx::NotFiniteStores& stores : report.mResult->mNotFinite.kernels())
                add(FindingKind::NotFinite, stores.mKernel, note);

        const Rtx::Refusals& refusals = context.mMirror.getScene().refusals();
        for (std::size_t kind = 0; kind < Rtx::sRefusedKinds; ++kind)
        {
            const std::uint32_t count = refusals.count(static_cast<Rtx::Refused>(kind));
            if (count > mRefused[kind])
                add(FindingKind::Refused, sRefusedNames[kind], note);
            mRefused[kind] = count;
        }

        // How many cells the game made active on this frame: its loads, which run on the frame's own thread.
        const MWWorld::Scene::CellStoreCollection& active
            = MWBase::Environment::get().getWorldScene()->getActiveCells();
        mActive.assign(active.begin(), active.end());
        std::ranges::sort(mActive);
        std::size_t loaded = 0;
        if (!mLastActive.empty())
            for (const void* cell : mActive)
                loaded += std::ranges::binary_search(mLastActive, cell) ? 0 : 1;
        std::swap(mActive, mLastActive);

        // Covered where the world stood whole while the player stood in the cell's column: the rows of a leg's band
        // stand in the active grid together.
        const MWWorld::CellStore* standing = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell();
        if (report.isWhole())
        {
            if (standing != nullptr && standing->getCell()->isExterior())
            {
                const int column = standing->getCell()->getGridX();
                if (std::ranges::find(mCovered, column) == mCovered.end())
                    mCovered.push_back(column);
            }
        }

        if (!measured)
            return;

        ++mMeasured;
        const double frameMs = report.mSpend.at(Rtx::Timing::Frame);
        if (frameMs > mBudgetMs)
        {
            ++mSlow;
            // Where the worst stood, taken only where a slow frame is a finding: a paced run's are the limiter's.
            if (!mPaced && frameMs > mSlowWorstMs)
            {
                mSlowWorstMs = frameMs;
                mSlowWorst = placeOf(note);
            }
            // **Whose it was, said**: `update` is the game's own work between two frames, and a frame the game made
            // cells active on is one it loaded them on.
            if (mLastMs > 0.0 && frameMs > sHitchRatio * mLastMs)
                add(FindingKind::Hitch,
                    std::format("{:.1f} ms after {:.1f}{}: {} meshes arrived, {}", frameMs, mLastMs,
                        loaded > 0 ? std::format(", the game loading {} cells", loaded) : std::string(),
                        report.mUpload.mArrivedMeshes, Rtx::describeStretches(report.mSpend)),
                    note, frameMs);
        }
        mLastMs = frameMs;
    }

    void TourWatch::endLeg(
        const Stop& stop, const BenchPlace& place, const std::span<const Check> failed, StandingNote& note)
    {
        drainLog();
        if (stop.mActions.mSoak == Actions::Soak::First)
            mFirstPass = SoakFigures::of(place);
        if (stop.mActions.mSoak == Actions::Soak::Again && mFirstPass.has_value())
            if (const std::optional<std::string> growth = describeGrowth(*mFirstPass, SoakFigures::of(place)))
                add(FindingKind::Growth, *growth, note);

        for (const Check check : failed)
            add(FindingKind::CheckFailed, checkName(check), note);

        if (!mPaced && mSlow > 0 && mSlowWorst.has_value())
            mTable.add(FindingKind::SlowFrames,
                std::format("{} of {} frames of {} over the {:.1f} ms budget", mSlow, mMeasured, mLeg, mBudgetMs),
                *mSlowWorst, mSlowWorstMs);

        for (const osg::Vec2i& cell : stop.mActions.mCovers)
            if (std::ranges::find(mCovered, cell.x()) == mCovered.end())
                add(FindingKind::Outrun,
                    std::format("cell {},{} never stood whole while the player was in its column", cell.x(), cell.y()),
                    note);
    }

    void TourWatch::failLeg(const Stop& stop, const std::string_view why, StandingNote& note)
    {
        drainLog();
        add(FindingKind::LegFailed, std::format("{}: {}", stop.mName, why), note);
    }

    void TourWatch::mark(StandingNote& note)
    {
        mTable.add(FindingKind::Mark, std::format("marked on frame {} of {}", mFrame, mLeg), placeOf(note));
    }
}
