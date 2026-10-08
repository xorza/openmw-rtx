#include "framedescriber.hpp"

#include <cassert>
#include <optional>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "fogmanager.hpp"
#include "sky.hpp"

namespace MWRender
{
    WorldState FrameDescriber::describeWorld(const FrameSources& sources) const
    {
        // Not `const`: `getTimeManager` is not.
        MWBase::World& simulation = *MWBase::Environment::get().getWorld();

        // The simulation's "no transition" is -1, and `WorldState` would rather say it in the type.
        const int next = simulation.getNextWeatherScriptId();
        const std::optional<int> nextWeather = next < 0 ? std::nullopt : std::optional(next);

        // **Off the light and the toggles, and nothing recorded twice.** The sun light is the one
        // copy of what the four setters wrote, and what the weather settled is the weather
        // manager's, handed beside this.
        WorldState described;
        described.mSunLightPosition = sources.mSun.getPosition();
        described.mSunColour = sources.mSun.getDiffuse();
        described.mAmbientColour = sources.mSun.getAmbient();
        described.mNightEye = sources.mSun.getAmbient() - sources.mAmbientBeforeNightEye;
        described.mSunVisibility = mSunVisibility;
        described.mSkyShown = mSkyShown;
        described.mMoonRed = mMoonRed;

        described.mLocation = simulation.isCellExterior() ? Location::Exterior
            : simulation.isCellQuasiExterior()            ? Location::QuasiExterior
                                                          : Location::Interior;

        // **A room's facts, read off the cell the player stands in.** The weather system stops the
        // moment they step inside, so everything it settled for the sky is the last outdoor
        // hour's, bar the sun `configureAmbient` pointed. The ray tracer lights a room from the
        // record alone.
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (described.mLocation == Location::Interior && player.isInCell())
        {
            const auto& mood = player.getCell()->getCell()->getMood();
            described.mRoom = ESM::Cell::AMBIstruct{
                .mAmbient = mood.mAmbiantColor,
                .mSunlight = mood.mDirectionalColor,
                .mFog = mood.mFogColor,
                .mFogDensity = mood.mFogDensity,
            };
        }

        described.mWater = mWater;
        described.mUnderwater = mWater.isUnderwater(sources.mEyePosition);
        described.mAir
            = { sources.mFog.getFogColor(false), sources.mFog.getFogStart(false), sources.mFog.getFogEnd(false) };
        described.mWaterFog
            = { sources.mFog.getFogColor(true), sources.mFog.getFogStart(true), sources.mFog.getFogEnd(true) };
        described.mPlayerPosition = player.getRefData().getPosition().asVec3();

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
        mWorld = describeWorld(sources);
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

        mDescribed = true;
        return *mFrame;
    }

    const SceneFrame& FrameDescriber::get() const
    {
        assert(mFrame.has_value() && "a frame is described before it is drawn");
        return *mFrame;
    }
}
