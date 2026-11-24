#include "rtxsettings.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <utility>

#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/settings/values.hpp>

namespace MWRender
{
    static_assert(Settings::RTXCategory::sMaxDistantLandCells == Rtx::LandReach::sMostCells,
        "the setting's bound and the reach's are one number");

    RtxSettingValues RtxSettingValues::fromRegistry()
    {
        return RtxSettingValues{
            .mUpscale = Settings::rtx().mUpscale.get(),
            .mUpscaler = Settings::rtx().mUpscaler.get(),
            .mIndirectLighting = Settings::rtx().mIndirectLighting,
            .mBounceRays = Settings::rtx().mBounceRays,
            .mLamps = {
                .mSourceScale = Settings::rtx().mLampSourceSize.get() / 25.0f,
                .mFlicker = Settings::rtx().mLampFlicker.get() / 100.0f,
                .mSpeed = Settings::rtx().mLampSpeed.get() / 100.0f,
                .mMovement = Settings::rtx().mLampMovement.get() / 100.0f,
                .mSizeVariation = Settings::rtx().mLampBreathing.get() / 100.0f,
            },
            .mInteriorWindowSunlight = Settings::rtx().mInteriorWindowSunlight,
            .mInteriorSunlightStrength = Settings::rtx().mInteriorSunlightStrength.get() / 100.0f,
            .mInteriorWindowSkylight = Settings::rtx().mInteriorWindowSkylight.get() / 100.0f,
            .mInteriorWindowGodrays = Settings::rtx().mInteriorWindowGodrays.get() / 100.0f,
            .mDistantLandCells = Settings::rtx().mDistantLandCells,
            .mViewingDistance = Settings::camera().mViewingDistance,
            .mObjectPaging = Settings::terrain().mObjectPaging,
            .mObjectPagingMinSize = Settings::terrain().mObjectPagingMinSize,
            .mGroundcover = Settings::groundcover().mEnabled,
            .mGroundcoverDistance = Settings::groundcover().mRenderingDistance,
            .mGroundcoverDensity = Settings::groundcover().mDensity,
            .mGroundcoverPointLighting = Settings::groundcover().mPointLighting,
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

        if (values.mBounceRays < 1 || std::cmp_greater(values.mBounceRays, Rtx::Shaders::MAX_BOUNCE_RAYS))
            throw Rtx::InputError(
                std::format("{} bounce rays is outside one to {}", values.mBounceRays, Rtx::Shaders::MAX_BOUNCE_RAYS));
        if (!(values.mInteriorSunlightStrength >= 0.0f && values.mInteriorSunlightStrength <= 30.0f))
            throw Rtx::InputError("interior sunlight strength must be finite and between zero and 30.0");
        if (!(values.mInteriorWindowSkylight >= 0.0f && values.mInteriorWindowSkylight <= 30.0f))
            throw Rtx::InputError("interior window skylight must be finite and between zero and 30.0");
        if (!(values.mInteriorWindowGodrays >= 0.0f && values.mInteriorWindowGodrays <= 3.0f))
            throw Rtx::InputError("interior window godrays must be finite and between zero and 3.0");
        assert(values.mUpscale != Rtx::Upscale::Off && "no upscaler named as a mode");

        return RtxSettings{
            .mUpscale = values.mUpscale,
            .mUpscaler = values.mUpscaler,
            .mIndirectLighting = values.mIndirectLighting,
            .mBounceRays = static_cast<std::uint32_t>(values.mBounceRays),
            .mInteriorWindowSunlight = values.mInteriorWindowSunlight,
            .mInteriorSunlightStrength = values.mInteriorSunlightStrength,
            .mInteriorWindowSkylight = values.mInteriorWindowSkylight,
            .mInteriorWindowGodrays = values.mInteriorWindowGodrays,
            .mMirror = {
                .mReach = { .mCells = values.mDistantLandCells, .mViewingDistance = values.mViewingDistance },
                .mLamps = values.mLamps,
                .mDistantStatics = values.mObjectPaging,
                .mMinSize = values.mObjectPagingMinSize,
                .mGroundcoverReach = values.mGroundcover ? values.mGroundcoverDistance : 0.0f,
                .mGroundcoverDensity = values.mGroundcoverDensity,
                .mGroundcoverLampLit = values.mGroundcoverPointLighting,
                .mInteriorWindowSunlight = values.mInteriorWindowSunlight,
            },
            .mAnisotropy = static_cast<std::uint32_t>(std::max(values.mAnisotropy, 1)),
            .mGamma = values.mGamma,
            .mLitEnvironmentMaps = values.mLitEnvironmentMaps,
        };
    }

    void RtxSettings::applyTo(Rtx::RenderProfile& profile) const
    {
        profile.mUpscale = mUpscaler == Rtx::UpscalerChoice::Off ? Rtx::Upscale::Off : mUpscale;
        profile.mUpscaler = mUpscaler;
        profile.mReconstruction.mSampling.mIndirect = mIndirectLighting;
        profile.mReconstruction.mSampling.mBounceRays = mBounceRays;
        profile.mAnisotropy = mAnisotropy;
        profile.mGamma = mGamma;
        profile.mLitEnvironmentMaps = mLitEnvironmentMaps;
    }
}
