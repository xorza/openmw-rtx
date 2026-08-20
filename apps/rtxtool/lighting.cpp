#include "lighting.hpp"

#include <cstdint>

#include <components/rtx/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

namespace RtxTool
{
    void applyLighting(
        const CellLighting& lighting, const Rtx::SceneDesc& scene, Rtx::Shaders::VisibilityConstants& constants)
    {
        constants.mAmbient = lighting.mAmbient;
        constants.mTime = lighting.mSeconds;
        constants.mSunDirection = lighting.mDaylight.mSun.mDirection;
        constants.mSunIrradiance = lighting.mDaylight.mSun.mIrradiance;
        constants.mSkyHorizon = lighting.mDaylight.mSkyHorizon;
        constants.mSkyZenith = lighting.mDaylight.mSkyZenith;
        constants.mLightCount = static_cast<std::uint32_t>(scene.getLights().size());
    }
}
