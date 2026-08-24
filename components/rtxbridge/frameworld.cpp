#include "frameworld.hpp"

namespace RtxBridge
{
    void applyWorld(const FrameWorld& world, Rtx::Shaders::VisibilityConstants& constants)
    {
        constants.mSunPosition = world.mSun.mPosition;
        constants.mSunIrradiance = world.mSun.mIrradiance;
        constants.mSunDiscColour = world.mSun.mDiscColour;
        constants.mAmbient = world.mAmbient;

        constants.mSkyHorizon = world.mSkyHorizon;
        constants.mSkyZenith = world.mSkyZenith;

        constants.mFogColour = world.mAir.mColour;
        constants.mFogExtinction = world.mAir.mExtinction;
        constants.mFogUniform = world.mAir.mUniform;

        constants.mWaterLevel = world.mWaterLevel;
        constants.mTime = world.mSeconds;

        constants.mWeather = world.mWeather;
        constants.mNextWeather = world.mNextWeather;
        constants.mWeatherBlend = world.mWeatherBlend;

        constants.mWindSpeed = world.mWindSpeed;
        constants.mStormDirection = world.mStormDirection;

        for (std::size_t moon = 0; moon < world.mMoons.size(); ++moon)
            constants.mMoons[moon] = describeMoon(world.mMoons[moon]);
    }
}
