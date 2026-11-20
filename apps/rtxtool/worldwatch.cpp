#include "worldwatch.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/openmw/mwrender/rtx/worldmirror.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>

#include "standingnote.hpp"

namespace RtxTool
{
    WorldWatch::WorldWatch(StandingNote& note)
        : mNote(note)
    {
        // The active grid's cells, which the game keeps active at once, with room for a frame that holds an old grid
        // beside a new one.
        constexpr std::size_t sActiveMost
            = 2 * (2 * Constants::CellGridRadius + 1) * (2 * Constants::CellGridRadius + 1);
        mActive.reserve(sActiveMost);
    }

    WatchFrame WorldWatch::frameOf(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
        const std::optional<ClosedSpan>& measured)
    {
        const MWWorld::Scene::CellStoreCollection& active
            = MWBase::Environment::get().getWorldScene()->getActiveCells();
        mActive.assign(active.begin(), active.end());

        const MWWorld::CellStore* const standing = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell();
        return WatchFrame{
            .mAir = report.mAir,
            .mNotFinite = report.mResult.has_value() ? report.mResult->mNotFinite.kernels()
                                                     : std::span<const Rtx::NotFiniteStores>(),
            .mScene = context.mMirror.getScene(),
            .mActiveCells = mActive,
            .mWhole = report.isWhole(),
            .mStandingColumn = standing != nullptr && standing->getCell()->isExterior()
                ? std::optional<std::int32_t>(standing->getCell()->getGridX())
                : std::nullopt,
            .mMeasured = measured,
            .mEndNs
            = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count(),
        };
    }

    FindingPlace WorldWatch::read(const std::string_view leg, const std::uint32_t frame, const Rtx::AirClock& air)
    {
        mNote.take(air);
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        FindingPlace place{ .mLeg = std::string(leg), .mFrame = frame };
        if (const Stop* stood = mNote.getLeft())
            place.mStood = *stood;
        FindingSky sky;
        sky.mLeft = Rtx::weatherOfScriptId(world.getCurrentWeatherScriptId()).value_or(Rtx::Weather::Clear);
        sky.mArriving = Rtx::weatherOfScriptId(world.getNextWeatherScriptId()).value_or(sky.mLeft);
        sky.mCrossed = 1.0f - world.getWeatherTransition();
        place.mSky = sky;
        return place;
    }
}
