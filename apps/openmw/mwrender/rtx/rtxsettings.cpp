#include "rtxsettings.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/settings/values.hpp>

namespace MWRender
{
    static_assert(Settings::RTXCategory::sMaxDistantLandCells == Rtx::LandReach::sMostCells,
        "the setting's bound and the reach's are one number");

    RtxSettingValues RtxSettingValues::fromRegistry()
    {
        return RtxSettingValues{
            .mUpscale = Settings::rtx().mUpscale.get(),
            .mDistantLandCells = Settings::rtx().mDistantLandCells,
            .mViewingDistance = Settings::camera().mViewingDistance,
            .mObjectPaging = Settings::terrain().mObjectPaging,
            .mObjectPagingMinSize = Settings::terrain().mObjectPagingMinSize,
            .mGroundcover = Settings::groundcover().mEnabled,
            .mGroundcoverDistance = Settings::groundcover().mRenderingDistance,
            .mGroundcoverDensity = Settings::groundcover().mDensity,
            .mGroundcoverPointLighting = Settings::groundcover().mPointLighting,
            .mSpecularMapLayout = Settings::rtx().mSpecularMapLayout.get(),
            .mIndirectLight = Settings::rtx().mIndirectLight.get(),
            .mAnisotropy = Settings::general().mAnisotropy,
            .mGamma = Settings::video().mGamma,
            .mLitEnvironmentMaps = Settings::shaders().mApplyLightingToEnvironmentMaps,
        };
    }

    RtxSettings RtxSettings::derive(const RtxSettingValues& values)
    {
        // The registry holds the setting to a finite number over nought; a harness's line holds
        // nothing, and past either end the picture is all black, all white or no number at all.
        if (!(values.mGamma > 0.0f && std::isfinite(values.mGamma)))
            throw Rtx::InputError(
                std::format("a gamma of {} is not a finite number greater than nought", values.mGamma));

        return RtxSettings{
            .mUpscale = values.mUpscale,
            .mMirror = {
                .mReach = { .mCells = values.mDistantLandCells, .mViewingDistance = values.mViewingDistance },
                .mDistantStatics = values.mObjectPaging,
                .mMinSize = values.mObjectPagingMinSize,
                .mGroundcoverReach = values.mGroundcover ? values.mGroundcoverDistance : 0.0f,
                .mGroundcoverDensity = values.mGroundcoverDensity,
                .mGroundcoverLampLit = values.mGroundcoverPointLighting,
                .mSpecularLayout = values.mSpecularMapLayout,
            },
            .mAnisotropy = static_cast<std::uint32_t>(std::max(values.mAnisotropy, 1)),
            .mGamma = values.mGamma,
            .mLitEnvironmentMaps = values.mLitEnvironmentMaps,
            .mIndirect = Rtx::sIndirectLightNames.require(values.mIndirectLight, "an indirect light"),
        };
    }
}
