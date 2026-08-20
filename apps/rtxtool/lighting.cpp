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
    }
}
