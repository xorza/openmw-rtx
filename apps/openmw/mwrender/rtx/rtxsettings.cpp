#include "rtxsettings.hpp"

#include <algorithm>

#include <components/rtx/cellgrid.hpp>
#include <components/rtx/specularlayout.hpp>
#include <components/rtx/upscale.hpp>
#include <components/settings/values.hpp>

namespace MWRender
{
    RtxSettingValues RtxSettingValues::fromRegistry()
    {
        return RtxSettingValues{
            .mUpscale = Settings::rtx().mUpscale.get(),
            .mPreset = Settings::rtx().mPreset.get(),
            .mReflex = Settings::rtx().mReflex.get(),
            .mDistantLandCells = Settings::rtx().mDistantLandCells,
            .mViewingDistance = Settings::camera().mViewingDistance,
            .mObjectPaging = Settings::terrain().mObjectPaging,
            .mObjectPagingMinSize = Settings::terrain().mObjectPagingMinSize,
            .mSpecularMapLayout = Settings::rtx().mSpecularMapLayout.get(),
            .mAnisotropy = Settings::general().mAnisotropy,
            .mReflexFlash = Settings::rtx().mReflexFlash,
            .mGroundcover = Settings::groundcover().mEnabled,
        };
    }

    RtxSettings RtxSettings::derive(const RtxSettingValues& values)
    {
        return RtxSettings{
            .mUpscaling = {
                .mMode = Rtx::sUpscaleNames.require(values.mUpscale, "an upscale mode"),
                .mPreset = Rtx::sPresetNames.require(values.mPreset, "a Ray Reconstruction preset"),
            },
            .mLatency = Rtx::sLatencyModeNames.require(values.mReflex, "a Reflex mode"),
            .mMirror = {
                .mReach = Rtx::distantLandReach(values.mDistantLandCells, values.mViewingDistance),
                .mDistantStatics = values.mObjectPaging,
                .mMinSize = values.mObjectPagingMinSize,
                .mSpecularLayout = Rtx::sSpecularLayoutNames.require(values.mSpecularMapLayout, "a specular map layout"),
            },
            .mAnisotropy = static_cast<std::uint32_t>(std::max(values.mAnisotropy, 1)),
            .mReflexFlash = values.mReflexFlash,
        };
    }
}
