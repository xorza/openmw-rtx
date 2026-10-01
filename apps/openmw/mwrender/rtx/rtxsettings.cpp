#include "rtxsettings.hpp"

#include <algorithm>

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
        };
    }

    RtxSettings RtxSettings::derive(const RtxSettingValues& values)
    {
        return RtxSettings{
            .mUpscale = Rtx::sUpscaleNames.require(values.mUpscale, "an upscale mode"),
            .mMirror = {
                .mReach = { .mCells = values.mDistantLandCells, .mViewingDistance = values.mViewingDistance },
                .mDistantStatics = values.mObjectPaging,
                .mMinSize = values.mObjectPagingMinSize,
                .mSpecularLayout = Rtx::sSpecularLayoutNames.require(values.mSpecularMapLayout, "a specular map layout"),
            },
            .mAnisotropy = static_cast<std::uint32_t>(std::max(values.mAnisotropy, 1)),
        };
    }
}
