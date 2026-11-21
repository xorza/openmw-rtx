#include "framedescriber.hpp"

#include <cassert>
#include <cstdint>
#include <optional>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/settings/values.hpp>

#include "fogmanager.hpp"

namespace MWRender
{
    FramePlace placeOf(const MWWorld::Cell& cell)
    {
        if (cell.isExterior())
            return FramePlace{ .mLocation = Location::Exterior, .mRoom = std::nullopt };
        const auto& mood = cell.getMood();
        return FramePlace{
            .mLocation = cell.isQuasiExterior() ? Location::QuasiExterior : Location::Interior,
            .mRoom = cell.isQuasiExterior() ? std::nullopt
                                            : std::optional(ESM::Cell::AMBIstruct{
                                                  .mAmbient = mood.mAmbiantColor,
                                                  .mSunlight = mood.mDirectionalColor,
                                                  .mFog = mood.mFogColor,
                                                  .mFogDensity = mood.mFogDensity,
                                              }),
        };
    }

    namespace
    {
        /// Where the world stands: the player's cell, and a room with no facts where the player stands in none, as
        /// in the main menu.
        FramePlace placeOfPlayer()
        {
            const MWWorld::Ptr& player = MWMechanics::getPlayer();
            if (!player.isInCell())
                return FramePlace{ .mLocation = Location::Interior, .mRoom = std::nullopt };
            return placeOf(*player.getCell()->getCell());
        }
    }

    WorldState FrameDescriber::describeWorld(const FrameSources& sources, const FramePlace& place) const
    {
        // Not `const`: `getTimeManager` is not.
        MWBase::World& simulation = *MWBase::Environment::get().getWorld();

        // The simulation's "no transition" is -1, and `WorldState` would rather say it in the type.
        const std::int32_t next = simulation.getNextWeatherScriptId();
        const std::optional<std::int32_t> nextWeather = next < 0 ? std::nullopt : std::optional(next);

        // **Off the light and the toggles, and nothing recorded twice.** The sun light is the one
        // copy of what the four setters wrote, and what the weather settled is the weather
        // manager's, handed beside this.
        WorldState described;
        described.mSunLightPosition = sources.mSun.getPosition();
        described.mSunColour = sources.mSun.getDiffuse();
        described.mAmbientColour = sources.mSun.getAmbient();
        described.mAmbientBeforeNightEye = sources.mAmbientBeforeNightEye;
        described.mNightEye = sources.mSun.getAmbient() - sources.mAmbientBeforeNightEye;
        described.mSunVisibility = mSunVisibility;
        described.mSkyShown = mSkyShown;
        described.mMoonRed = mMoonRed;

        // **The place is the one the frame stands in**, handed in: the player's cell for a frame drawn, and for a wait
        // the cell its change arrived in, whatever the scene has moved yet. The weather system stops the moment the
        // player steps inside, so everything it settled for the sky is the last outdoor hour's, bar the sun
        // `configureAmbient` pointed.
        described.mLocation = place.mLocation;
        described.mRoom = place.mRoom;

        described.mWater = mWater;
        described.mUnderwater = mWater.isUnderwater(sources.mEyePosition);
        described.mAir
            = { sources.mFog.getFogColor(false), sources.mFog.getFogStart(false), sources.mFog.getFogEnd(false) };
        described.mWaterFog
            = { sources.mFog.getFogColor(true), sources.mFog.getFogStart(true), sources.mFog.getFogEnd(true) };
        described.mPlayerPosition = MWMechanics::getPlayer().getRefData().getPosition().asVec3();

        described.mGameHour = simulation.getTimeStamp().getHour();
        if (Settings::game().mDayNightSwitches)
            described.mNightDayMode = simulation.getNightDayMode();
        described.mGameTime = simulation.getTimeManager()->getGameTime();
        described.mTimeScale = simulation.getTimeManager()->getGameTimeScale();
        described.mWeatherId = simulation.getCurrentWeatherScriptId();
        described.mNextWeatherId = nextWeather;
        described.mWeatherTransition = simulation.getWeatherTransition();
        described.mWindSpeed = simulation.getWindSpeed();

        return described;
    }

    const SceneFrame& FrameDescriber::describe(const FrameSources& sources)
    {
        mWorld = describeWorld(sources, placeOfPlayer());
        mEye = sources.mEye;
        mEye.mProjectionMatrix = mProjection;
        mEye.mProjectionShift = mProjectionShift;

        mFrame.emplace(SceneFrame{
            .mScene = sources.mScene,
            .mWhen = sources.mWhen,
            .mSky = MWBase::Environment::get().getWorld()->getSkyState(),
            .mPrecipitation = sources.mPrecipitation,
            .mWorld = mWorld,
            .mEye = mEye,
            .mTerrain = sources.mTerrain,
            .mObjectStorage = sources.mObjectStorage,
            .mDeltaTime = mDeltaTime,
            .mPaused = mPaused,
            .mJumped = mJumped,
        });

        // **A frame no update stepped stood still.** The engine updates the world only while a game
        // runs, and describes every frame: the main menu's kept the last step it was handed, or
        // none and "not paused", and the rasterizer's water stepped its ripples for a player with
        // no cell, which upstream never updates there.
        mDeltaTime = 0.f;
        mPaused = true;

        mDescribed = true;
        return *mFrame;
    }

    const SceneFrame& FrameDescriber::describeStill(const FrameSources& sources, const MWWorld::CellStore& destination)
    {
        assert(!mDescribed && "a still frame described while the frame described spans the jumps");
        mWorld = describeWorld(sources, placeOf(*destination.getCell()));
        mEye = sources.mEye;
        mEye.mProjectionMatrix = mProjection;
        mEye.mProjectionShift = mProjectionShift;

        mFrame.emplace(SceneFrame{
            .mScene = sources.mScene,
            .mWhen = sources.mWhen,
            .mSky = MWBase::Environment::get().getWorld()->getSkyState(),
            .mPrecipitation = sources.mPrecipitation,
            .mWorld = mWorld,
            .mEye = mEye,
            .mTerrain = sources.mTerrain,
            .mObjectStorage = sources.mObjectStorage,
            .mDeltaTime = 0.0f,
            .mPaused = true,
            .mJumped = {},
        });
        return *mFrame;
    }

    const SceneFrame& FrameDescriber::get() const
    {
        assert(mFrame.has_value() && "a frame is described before it is drawn");
        return *mFrame;
    }
}
