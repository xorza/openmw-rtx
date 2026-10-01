#include "rtxsettings.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/settings/values.hpp>

namespace MWRender
{
    RtxSettingValues RtxSettingValues::fromRegistry()
    {
        return RtxSettingValues{
            .mUpscale = Settings::rtx().mUpscale.get(),
            .mDistantLandCells = Settings::rtx().mDistantLandCells,
            .mViewingDistance = Settings::camera().mViewingDistance,
            .mObjectPaging = Settings::terrain().mObjectPaging,
            .mObjectPagingMinSize = Settings::terrain().mObjectPagingMinSize,
            .mSpecularMapLayout = Settings::rtx().mSpecularMapLayout.get(),
            .mAnisotropy = Settings::general().mAnisotropy,
            .mGamma = Settings::video().mGamma,
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
            .mUpscale = Rtx::sUpscaleNames.require(values.mUpscale, "an upscale mode"),
            .mMirror = {
                .mReach = { .mCells = values.mDistantLandCells, .mViewingDistance = values.mViewingDistance },
                .mDistantStatics = values.mObjectPaging,
                .mMinSize = values.mObjectPagingMinSize,
                .mSpecularLayout = Rtx::sSpecularLayoutNames.require(values.mSpecularMapLayout, "a specular map layout"),
            },
            .mAnisotropy = static_cast<std::uint32_t>(std::max(values.mAnisotropy, 1)),
            .mGamma = values.mGamma,
        };
    }
}
