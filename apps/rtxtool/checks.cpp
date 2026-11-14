#include "checks.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Math>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/camera.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/esm3/refnum.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/cells/cellplacer.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    namespace
    {
        /// How wide the square of cells the simulation holds is, in units: what distant ground has to reach past to
        /// be distant. `Constants::CellGridRadius` is the ring the game loads around the player, so a scene no wider
        /// than this is one the residency contributed nothing to.
        constexpr float sActiveGridWidth
            = static_cast<float>(Constants::CellSizeInUnits) * (2 * Constants::CellGridRadius + 1);

        /// Whether one check holds of what the run was handed and what it drew, with what it found in `found`
        /// either way.
        bool checkHolds(const MWRender::FrameContext& context, const MWRender::FrameReport& report, const Check check,
            const StopFacts& facts, std::string& found)
        {
            const Rtx::SceneDesc& scene = context.mMirror.getScene();
            const Rtx::ExtractionStats& stats = report.mWalked.mFound;

            switch (check)
            {
                case Check::WalkTwice:
                {
                    if (!report.mWalked.mAgain.has_value())
                    {
                        found = "no second walk was made";
                        return false;
                    }

                    const Rtx::ExtractionStats& again = *report.mWalked.mAgain;
                    const Rtx::ExtractionStats& first = report.mWalked.mFound;
                    // **What it resolved or passed frozen**: a second walk over a world that stood still
                    // passes every reference that froze, and resolves only what did not. A graph the first walk
                    // met nothing in, as over open sea, has nothing for the second to meet, which holds: a tour's
                    // leg ending past the last land of its row stood there and failed for it.
                    const bool met = first.mMeshesAdded + first.mMeshesReused + first.mPassedFrozen > 0;
                    found = std::format(
                        "{} meshes and {} materials added by the second walk, {} drawables resolved, "
                        "{} passed frozen, {} stood again; the first walk met {}",
                        again.mMeshesAdded, again.mMaterialsAdded, again.mMeshesReused, again.mPassedFrozen,
                        again.mRestood, met ? "drawables" : "none");
                    return again.mMeshesAdded == 0 && again.mMaterialsAdded == 0
                        && (again.mMeshesReused + again.mPassedFrozen > 0 || !met) && again.mRestood == 0;
                }

                case Check::SurfacesDescribed:
                    // **The emitters are reported and not asserted**: a refused emitter is content the
                    // log names, and every world carries one of the rasterizer's that the traced path
                    // answers for itself.
                    found = std::format("{} surfaces undescribed, {} emitters refused", stats.mUndescribedSurfaces,
                        scene.refusals().count(Rtx::Refused::Emitter));
                    return stats.mUndescribedSurfaces == 0;

                case Check::LightsPlaced:
                {
                    const bool indoors = !MWBase::Environment::get().getWorld()->isCellExterior();
                    found = std::format("{} lights casting {}", scene.lights().size(),
                        indoors ? "in a room" : "under a sky, where none is a fair answer");
                    return !indoors || !scene.lights().empty();
                }

                case Check::GroundReaches:
                {
                    // **Asked of an exterior and answered yes by every room**, which has no distant
                    // ground to reach for.
                    const bool outdoors = MWBase::Environment::get().getWorld()->isCellExterior();

                    // What stands inside the reach, and not the whole scene's extent: the sea is one sheet a hundred
                    // and fifty cells across, so `getBounds` clears any threshold at every coastline.
                    // `getContentBoundsWithin` leaves a backdrop out and clips what it meets, which is exactly the
                    // ground this is about.
                    const osg::Vec3f eye
                        = MWBase::Environment::get().getWorld()->getPlayerPtr().getRefData().getPosition().asVec3();
                    const float reach = context.mMirror.getReach();
                    const float sky = std::numeric_limits<float>::max();
                    const osg::BoundingBoxf region(
                        eye.x() - reach, eye.y() - reach, -sky, eye.x() + reach, eye.y() + reach, sky);

                    const osg::BoundingBoxf bounds = scene.getContentBoundsWithin(region);
                    const float widest = bounds.valid()
                        ? std::max(bounds.xMax() - bounds.xMin(), bounds.yMax() - bounds.yMin())
                        : 0.0f;

                    found = std::format(
                        "the ground spans {:.0f} units against an active grid {:.0f} wide", widest, sActiveGridWidth);
                    return !outdoors || widest > sActiveGridWidth;
                }

                case Check::GroundStands:
                {
                    // **Every cell of the reach, the active grid's included**: the game builds no
                    // ground for this renderer, so a cell short is a hole the player can walk on. The
                    // reach is the disc `Rtx::CellGrid::withinReach` draws about the eye the walk
                    // stood, counted by the same rule.
                    const bool outdoors = MWBase::Environment::get().getWorld()->isCellExterior();
                    std::uint32_t expected = 0;
                    context.mMirror.getGrid().forEachCellWithin(
                        context.mMirror.getEye(), context.mMirror.getReach(), [&](const osg::Vec2i&) { ++expected; });

                    found = std::format("{} cells of ground stand against {} in the reach", stats.mGroundCells,
                        outdoors ? expected : 0);
                    return !outdoors || stats.mGroundCells == expected;
                }

                case Check::LightsNotDoubled:
                {
                    std::vector<osg::Vec3f> where;
                    where.reserve(scene.lights().size());
                    for (const Rtx::Light& light : scene.lights())
                        where.push_back(light.mPosition);

                    // `osg::Vec3f` orders lexicographically already, which is what a sort for
                    // duplicates needs and what its own `operator<` promises.
                    std::sort(where.begin(), where.end());

                    const auto doubled = std::adjacent_find(where.begin(), where.end());
                    found = std::format("{} lights, {}", where.size(),
                        doubled == where.end() ? "no two at one point" : "two at one point");

                    return doubled == where.end();
                }

                case Check::StaticsNotDoubled:
                {
                    // **Asked of the game's registry and not of the walk**, because the walk knows a
                    // placement by its node and the ring knows one by its reference: what the two share
                    // is the game's own `Ptr`, and a base node on it is the game standing the reference
                    // in the graph the walk mirrors.
                    const MWWorld::WorldModel& model = *MWBase::Environment::get().getWorldModel();
                    std::vector<ESM::RefNum> standing;
                    context.mMirror.getRing().collectStanding(standing);

                    std::size_t doubled = 0;
                    std::string first;
                    for (const ESM::RefNum refnum : standing)
                    {
                        const MWWorld::Ptr stood = model.getPtr(refnum);
                        if (stood.isEmpty() || stood.getRefData().getBaseNode() == nullptr)
                            continue;

                        if (doubled++ == 0)
                            first = std::format(", the first {} at {}", stood.getCellRef().getRefId().toDebugString(),
                                stood.getCell()->getCell()->getDescription());
                    }

                    found = std::format("{} statics the ring stands, {} of them stood by the game too{}",
                        standing.size(), doubled, first);
                    return doubled == 0;
                }

                case Check::GatesAgree:
                {
                    // A cell of the active grid is loaded, so its references are in the registry as
                    // the cell's scripts left them.
                    const MWWorld::WorldModel& model = *MWBase::Environment::get().getWorldModel();
                    std::vector<Rtx::GateVerdict> verdicts;
                    context.mMirror.getRing().collectGateVerdicts(verdicts);

                    std::size_t disagree = 0;
                    std::string first;
                    for (const Rtx::GateVerdict& verdict : verdicts)
                    {
                        const MWWorld::Ptr held = model.getPtr(verdict.mRefNum);
                        if (held.isEmpty())
                            continue;
                        const bool stands = held.getRefData().isEnabled() && held.getCellRef().getCount() > 0
                            && !held.mRef->isDeleted();
                        if (stands == verdict.mStands)
                            continue;

                        if (disagree++ == 0)
                            first = std::format(", the first {} at {}, which the gate {} and the game {}",
                                held.getCellRef().getRefId().toDebugString(),
                                held.getCell()->getCell()->getDescription(), verdict.mStands ? "stands" : "keeps down",
                                stands ? "stands" : "keeps down");
                    }

                    found = std::format(
                        "{} references behind a gate that decided in the active grid, {} of them "
                        "otherwise in the game{}",
                        verdicts.size(), disagree, first);
                    return disagree == 0;
                }

                case Check::TexturesReadable:
                {
                    const std::uint32_t refused = scene.refusals().count(Rtx::Refused::Texture);
                    found = std::format("{} of {} textures refused", refused, scene.textures().getRows().size());
                    return refused == 0;
                }

                case Check::RoomsHeld:
                {
                    const std::uint32_t past = context.mMirror.countPastRoom();
                    found = std::format("{} entries arrived past their map's room", past);
                    return past == 0;
                }

                case Check::CrossingsAppend:
                {
                    const Crossings& crossings = facts.mCrossings;
                    found = std::format("{} crossings, {} of them rebuilds", crossings.mCount, crossings.mRebuilds);
                    return crossings.mCount > 0 && crossings.mRebuilds < crossings.mCount;
                }

                case Check::FramesOverlap:
                {
                    const Overlap& overlap = facts.mOverlap;
                    found = std::format(
                        "{:.2f} frames in flight at a submit, {} at the least", overlap.getMean(), overlap.mLeast);
                    return overlap.mFrames > 0 && overlap.mLeast == Rtx::sFramesInFlight;
                }

                case Check::QueueHeld:
                {
                    const auto zone = std::find_if(facts.mZones.begin(), facts.mZones.end(),
                        [](const GpuZone& held) { return held.mZone == Rtx::FrameZone::Stress; });
                    if (zone == facts.mZones.end() || zone->mFrames == 0)
                    {
                        found = "no frame timed the hold";
                        return false;
                    }

                    // Every frame held for at least as long as asked, by both clocks: the loop's own, which leaves at
                    // its first tick past the time and so cannot read shorter unless the loop is wrong, and the timer's
                    // zone around it, which says the queue was occupied for all of it. No bound above, because
                    // everything past the tick is the card's (a clock switch, a compositor's slice) and a hold that ran
                    // long is a queue held longer, which weakens nothing the hold is for. The longest reading is
                    // printed so a stall can be seen, and the zone beside the loop so its excess is known to be the
                    // launch and the drain. Each in full: a tick is a hundred thousandth of a millisecond on RDNA's
                    // clock.
                    const HoldTimes& hold = facts.mHold;
                    const double zoneShortest = zone->mTimes.mBest;
                    found = std::format(
                        "{} ms held by the loop at the shortest frame of {} asked, {} at the "
                        "longest, {} timed around it at the shortest, on {} of {} frames",
                        hold.mShortestMs, facts.mHoldAskedMs, hold.mLongestMs, zoneShortest, zone->mFrames,
                        zone->mOfFrames);
                    return zone->mFrames == zone->mOfFrames && hold.mFrames == zone->mOfFrames
                        && hold.mShortestMs >= facts.mHoldAskedMs && zoneShortest >= facts.mHoldAskedMs;
                }

                case Check::SkyNamed:
                {
                    // `canAsk` asks this only of a stop that names its weather.
                    assert(facts.mWeather.has_value());
                    const MWBase::World& world = *MWBase::Environment::get().getWorld();
                    const std::optional<Rtx::Weather> standing
                        = Rtx::weatherOfScriptId(world.getCurrentWeatherScriptId());
                    const std::optional<Rtx::Weather> next = Rtx::weatherOfScriptId(world.getNextWeatherScriptId());
                    found = std::format("under {}{}, named {}",
                        standing.has_value() ? Rtx::nameOf(*standing) : "no weather",
                        next.has_value() ? std::format(" crossing into {}", Rtx::nameOf(*next)) : std::string(),
                        Rtx::nameOf(*facts.mWeather));
                    return standing == facts.mWeather && !next.has_value();
                }

                case Check::Finite:
                {
                    found = describeNotFinite(facts.mNotFinite) + " over the measured frames";
                    return facts.mNotFinite.total() == 0;
                }

                case Check::CameraStands:
                {
                    // `canAsk` asks this only of a stop standing still, which names its eye.
                    const Stand& stand = facts.mStand;
                    assert(stand.mEye.has_value());

                    // **The game's camera and not the note the session took**, which is read off the
                    // same object: a check against that would agree with itself however far either had
                    // drifted from what the stop asked for.
                    const MWRender::Camera& camera
                        = *MWBase::Environment::get().getWorld()->getRenderingManager()->getCamera();
                    const osg::Vec3f eye(camera.getPosition());
                    const osg::Vec3f forward = camera.getOrient() * osg::Vec3f(0.0f, 1.0f, 0.0f);

                    osg::Vec3f asked = stand.getLook() - *stand.mEye;
                    asked.normalize();

                    // A tenth of a unit and a tenth of a degree: the eye is set from the view file
                    // outright, and the aim goes out through a pitch and a yaw and comes back through a
                    // quaternion, so what survives is float rounding rather than a tolerance on a
                    // measurement.
                    const float slipped = (eye - *stand.mEye).length();
                    const float turned = osg::RadiansToDegrees(std::acos(std::clamp(forward * asked, -1.0f, 1.0f)));

                    found = std::format(
                        "the eye stands {:.2f} units and {:.2f}° from what the stop asked", slipped, turned);
                    return slipped < 0.1f && turned < 0.1f;
                }
            }

            return false;
        }
    }

    void askChecks(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
        const std::span<const Check> checks, const StopFacts& facts, RunRecord& record)
    {
        for (const Check check : checks)
        {
            std::string found;
            const bool held = checkHolds(context, report, check, facts, found);

            record.checked(check, held);
            record.note(std::format("  {:<20} {:<4} {}\n", checkName(check), held ? "ok" : "FAIL", found));
        }
    }
}
