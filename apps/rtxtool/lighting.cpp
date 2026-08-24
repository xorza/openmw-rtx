#include "lighting.hpp"

#include <components/rtx/shaders/visibility.h>

namespace RtxTool
{
    void applyLighting(const CellLighting& lighting, Rtx::Shaders::VisibilityConstants& constants)
    {
        constants.mAmbient = lighting.mAmbient;
        constants.mTime = lighting.mSeconds;
        constants.mWaterLevel = lighting.mWaterLevel;
        constants.mSunDirection = lighting.mDaylight.mSun.mDirection;
        constants.mSunIrradiance = lighting.mDaylight.mSun.mIrradiance;
        constants.mSkyHorizon = lighting.mDaylight.mSkyHorizon;
        constants.mSkyZenith = lighting.mDaylight.mSkyZenith;
        constants.mFogColour = lighting.mFog.mColour;
        constants.mFogExtinction = lighting.mFog.mExtinction;
        constants.mFogUniform = lighting.mFog.mUniform;

        // **The same weather twice, at a blend of nothing.** A harness frame is a settled sky, so
        // there is no second one to name; the shader mixes unconditionally and reads this as the
        // whole of the first.
        constants.mWeather = lighting.mWeather;
        constants.mNextWeather = lighting.mWeather;
        constants.mWeatherBlend = 0.0f;

        constants.mWindSpeed = lighting.mWindSpeed;

        // **Asked of the eye, which is the only body standing in this weather.** The game aims an
        // ashstorm at the player; every caller here has already put its camera in `mOrigin`, so the
        // same rule reaches the same answer for whoever is looking.
        constants.mStormDirection = RtxBridge::stormDirection(lighting.mWeather, constants.mOrigin);
    }
}
