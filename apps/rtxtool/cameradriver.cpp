#include "cameradriver.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <osg/Vec3d>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/camera.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/position.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/fallback/fallback.hpp>
#include <components/rtx/skylight.hpp>

#include "model/benchrun.hpp"
#include "model/cameratrack.hpp"
#include "run.hpp"

namespace RtxTool
{
    void CameraDriver::begin(const Stop& stop)
    {
        *this = CameraDriver{};
        beginTurn(stop);

        if (stop.mSchedule.mFreeCamera || !stop.mStand.mEye.has_value())
        {
            // **A window's picture is its body's own camera**, and the stager stood the body's feet
            // where the eye goes and let the ground take it: the eye stood a head's height over
            // wherever the feet landed. The body is put under the eye over the frames that follow.
            if (stop.mSchedule.mFreeCamera && stop.mStand.mEye.has_value())
                mSettling = *stop.mStand.mEye;
            standWhereThePlayerIs();
            return;
        }

        if (stop.mSchedule.mTrack.has_value())
        {
            const MWWorld::TimeStamp now = MWBase::Environment::get().getWorld()->getTimeStamp();
            mClockFrom = now.getDay() * 24.0 + now.getHour();
            mFacing = stop.mStand.getRotation();
        }

        standAt(*stop.mStand.mEye, stop.mStand.getLook());
        aim(stop);
    }

    void CameraDriver::step(const Stop& stop, const std::optional<std::uint32_t> measured, const float seconds)
    {
        if (mSettling.has_value())
            settleBody();

        if (stop.mSchedule.mTrack.has_value())
        {
            const TrackPose pose = stop.mSchedule.mTrack->pose(measured.value_or(0u));

            mFlown = pose.mEye;
            mFacing = pose.mRotation;
            moveBodyTo(pose.mEye);

            holdSky(pose.mWeather, pose.mNextWeather, pose.mCrossed);

            // **Run forward to the track's hour and never set to it**, because only an advance keeps
            // the day, the month and the days passed in step with the hour, which the moons read.
            MWBase::World& world = *MWBase::Environment::get().getWorld();
            const MWWorld::TimeStamp now = world.getTimeStamp();
            const double behind = mClockFrom + pose.mHoursOn - (now.getDay() * 24.0 + now.getHour());
            if (behind > 0.0)
                world.advanceTime(behind, true);
            return;
        }

        // **The route runs over the measured frames and not the warm-up.** Warming up is the GPU
        // coming off its idle clock; flying during it would start the measurement partway along
        // and leave the first crossing outside the numbers.
        if (measured.has_value())
        {
            if (stop.mSchedule.mRoute.has_value())
                fly(*stop.mSchedule.mRoute, seconds);

            turnWeather(stop, seconds);
        }

        // Every frame, warm-up included, as the world ran its own crossings: the turn's first one
        // begins at the stop's first frame.
        crossSky(stop, seconds);
    }

    void CameraDriver::turnSkyBy(const Stop& stop, const int steps)
    {
        if (stop.mSchedule.mTrack.has_value())
        {
            Log(Debug::Info) << "Ray tracing session: a take's keys hold its sky";
            return;
        }

        MWBase::World& world = *MWBase::Environment::get().getWorld();
        const ESM::RefId region = world.getPlayerPtr().getCell()->getCell()->getRegion();
        const ESM::Region* const record
            = region.empty() ? nullptr : MWBase::Environment::get().getESMStore()->get<ESM::Region>().search(region);
        if (record == nullptr)
        {
            Log(Debug::Info) << "Ray tracing session: no sky here";
            return;
        }

        std::vector<std::uint32_t> rolled;
        for (std::uint32_t weather = 0; !Rtx::weatherName(weather).empty(); ++weather)
            if (chanceOf(*record, weather) > 0)
                rolled.push_back(weather);

        if (rolled.empty())
        {
            Log(Debug::Info) << "Ray tracing session: the region rolls no weather";
            return;
        }

        SkyCrossing& sky = takeSky();
        const std::size_t at = sky.stepAmong(rolled, steps);
        const std::uint32_t chosen = rolled[at];

        const bool stopped = !(world.getTimeManager()->getGameTimeScale() > 0.0f);
        if (stopped)
            sky.settle(chosen);
        else
            sky.ask(chosen);
        mDelta = transitionDeltaOf(chosen);

        Log(Debug::Info) << std::format("Ray tracing session: {} {}, {} of {} the region rolls, {}%",
            Rtx::weatherName(chosen), stopped ? "at once under the stopped clock" : "arriving", at + 1, rolled.size(),
            chanceOf(*record, chosen));
    }

    void CameraDriver::beginTurn(const Stop& stop)
    {
        const std::vector<std::string>& through = stop.mSky.mTurnThrough;
        if (through.empty())
            return;

        // **From the stop's own weather where it names one**, which the stager asked the world to
        // settle under: the world settles it in its next update, and a held sky has none.
        const std::optional<std::uint32_t> named
            = stop.mSky.mWeather.has_value() ? Rtx::weatherIndex(*stop.mSky.mWeather) : std::nullopt;
        mSky = named.has_value() ? SkyCrossing(*named, *named, 0.0f) : skyOfTheWorld();

        askTurn(through.front());
    }

    void CameraDriver::askTurn(const std::string& weather)
    {
        const std::optional<std::uint32_t> named = Rtx::weatherIndex(weather);
        if (!named.has_value())
        {
            Log(Debug::Warning) << "Ray tracing session: no weather is called \"" << weather << '"';
            return;
        }

        mSky->ask(*named);
    }

    void CameraDriver::crossSky(const Stop& stop, const float seconds)
    {
        if (!mSky.has_value())
            return;

        // **Seconds of the simulation's clock, as the world runs its own crossings, and at the
        // game clock's speed**: a sky the clock runs eight times as fast crosses eight times as
        // fast, and one it stopped stands. A turn crosses in `sTurnSeconds` of world whatever the
        // clock does, because its asks come on the same schedule.
        const MWWorld::DateTimeManager& clock = *MWBase::Environment::get().getWorld()->getTimeManager();
        const float simulated = seconds * clock.getSimulationTimeScale();
        mSky->advance(stop.mSky.mTurnThrough.empty() ? SkyCrossing::shareOf(simulated, mDelta, clock.getGameTimeScale())
                                                     : simulated / sTurnSeconds);

        holdSky(mSky->getWeather(), mSky->getNextWeather(), mSky->getCrossed());
    }

    void CameraDriver::holdSky(const std::uint32_t weather, const std::uint32_t next, const float crossed)
    {
        MWBase::Environment::get().getWorld()->holdWeather(ESM::Weather::indexToRefId(static_cast<int>(weather)),
            ESM::Weather::indexToRefId(static_cast<int>(next)), crossed);
    }

    SkyCrossing& CameraDriver::takeSky()
    {
        if (!mSky.has_value())
            mSky = skyOfTheWorld();

        return *mSky;
    }

    SkyCrossing CameraDriver::skyOfTheWorld()
    {
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        const auto current = static_cast<std::uint32_t>(world.getCurrentWeatherScriptId());
        const int next = world.getNextWeatherScriptId();

        // The factor the world counts down from one, so what has crossed counts up.
        return SkyCrossing(current, next < 0 ? current : static_cast<std::uint32_t>(next),
            std::max(1.0f - world.getWeatherTransition(), 0.0f));
    }

    float CameraDriver::transitionDeltaOf(const std::uint32_t weather)
    {
        // What the world reads for the same weather (`MWWorld::Weather::mTransitionDelta`).
        return Fallback::Map::getFloat(std::format("Weather_{}_Transition_Delta", Rtx::weatherName(weather)));
    }

    int CameraDriver::chanceOf(const ESM::Region& region, const std::uint32_t weather)
    {
        const auto found = region.mData.mProbabilities.find(ESM::Weather::indexToRefId(static_cast<int>(weather)));
        return found != region.mData.mProbabilities.end() ? found->second : 0;
    }

    void CameraDriver::fly(const Route& route, const float step)
    {
        // **Measured from `mFlown` and never from the player**, which says why.
        osg::Vec3f along = route.mTo - mFlown;
        const float left = along.length();
        if (left <= 0.0f)
        {
            mArrived = true;
            return;
        }

        along.normalize();

        // **Off the frame index and not the clock**, for the reason the world is stepped that way:
        // a camera advanced by how long the last frame took crosses its boundaries somewhere else
        // on every machine, and where they fall is the whole measurement. The height is the line's
        // between the two ends, which is what a view states when it names both.
        mFlown += along * std::min(route.mSpeed * step, left);
        moveBodyTo(mFlown);
    }

    void CameraDriver::turnWeather(const Stop& stop, const float step)
    {
        const std::vector<std::string>& through = stop.mSky.mTurnThrough;
        if (through.size() < 2)
            return;

        // **How often a run that turns its sky asks for the next weather, by frames of world**: off
        // the frame index rather than the clock, so the same frame stands under the same sky on
        // every machine. The crossing takes the same `sTurnSeconds` (`crossSky`), so every ask is a
        // crossing and every crossing swaps.
        mTurned += step / sTurnSeconds;
        if (mTurned < 1.0f)
            return;

        mTurned = 0.0f;
        mTurnedTo = (mTurnedTo + 1) % through.size();
        askTurn(through[mTurnedTo]);
    }

    void CameraDriver::aim(const Stop& stop)
    {
        if (stop.mSchedule.mFreeCamera || !stop.mStand.mEye.has_value())
            return;

        if (stop.mSchedule.mTrack.has_value())
        {
            aimCamera(mFlown, mFacing);
            return;
        }

        const std::optional<Route>& route = stop.mSchedule.mRoute;
        const osg::Vec3f look = route.has_value() ? route->mLookTo : mFlown + (mFromLook - mFrom);

        aimCamera(mFlown, Stand{ .mCell = {}, .mEye = mFlown, .mLook = look }.getRotation());
    }

    float CameraDriver::getTravelled(const Route& route) const
    {
        const float whole = (route.mTo - mFrom).length();
        return whole > 0.0f ? std::clamp((mFlown - mFrom).length() / whole, 0.0f, 1.0f) : 1.0f;
    }

    void CameraDriver::standAt(const osg::Vec3f& eye, const osg::Vec3f& look)
    {
        mFrom = eye;
        mFromLook = look;
        mFlown = eye;
    }

    void CameraDriver::standWhereThePlayerIs()
    {
        // The reference lives in the cell store rather than in the `Ptr`, which is what the named
        // player says: the position outlives the handle it was reached through.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        const ESM::Position& stood = player.getRefData().getPosition();

        const osg::Vec3f eye(stood.pos[0], stood.pos[1], stood.pos[2]);
        standAt(eye, eye + Stand::forwardOf(osg::Vec3f(stood.rot[0], stood.rot[1], stood.rot[2])));
    }

    void CameraDriver::settleBody()
    {
        // **Measured off the camera and moved by the difference, because the camera is the body's
        // own and follows it an update behind.** Where the first-person eye stands over the feet is
        // the head's, and it turns with the body's facing: at the pier, the first correction lifts
        // the eye 162 units and leaves it five units aside, and the second puts it on the stop's
        // eye to the bit. **Ended where it is exact, or where a correction brought it no closer** —
        // then something else is moving the body, which in a window is the player, whose it is.
        MWBase::World& world = *MWBase::Environment::get().getWorld();
        const osg::Vec3f off = *mSettling - osg::Vec3f(world.getRenderingManager()->getCamera()->getPosition());
        const float left = off.length2();
        if (left == 0.0f || !(left < mSettleLeft))
        {
            mSettling.reset();
            return;
        }

        mSettleLeft = left;
        world.moveObjectBy(world.getPlayerPtr(), off, true);
    }

    void CameraDriver::moveBodyTo(const osg::Vec3f& eye)
    {
        MWBase::World& world = *MWBase::Environment::get().getWorld();
        const MWWorld::Ptr player = world.getPlayerPtr();
        const ESM::Position& stood = player.getRefData().getPosition();

        // **`moveObjectBy` and not `moveObject`, because the player is an actor.** The actor's
        // position lives in the physics world as well, and a move that writes only the world's
        // copy is written back over it on the next step.
        world.moveObjectBy(player, eye - osg::Vec3f(stood.pos[0], stood.pos[1], stood.pos[2]), true);
    }

    void CameraDriver::aimCamera(const osg::Vec3f& eye, const osg::Vec3f& rotation)
    {
        MWRender::Camera* camera = MWBase::Environment::get().getWorld()->getRenderingManager()->getCamera();

        // **A static camera and not the player's own.** Nothing tracks the body, nothing rotates
        // to its facing and nothing casts a ray to keep the eye out of a wall, which is what a view
        // file's coordinates mean. It does not hold on its own, for the reason `aim` gives.
        camera->setMode(MWRender::Camera::Mode::Static);
        camera->setStaticPosition(osg::Vec3d(eye));

        // The body's rotation, negated into the camera's own angles the way
        // `Camera::rotateCameraToTrackingPtr` negates a tracked body's.
        camera->setPitch(-rotation.x(), true);
        camera->setYaw(-rotation.z(), true);
    }
}
