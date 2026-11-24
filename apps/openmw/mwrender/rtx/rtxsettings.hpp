#pragma once

#include <cstdint>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/lightbuilder.hpp>

#include "mirrorknobs.hpp"

namespace MWRender
{
    /// The ray tracer's settings as values, before anything reads their meaning. The player's registry fills
    /// one for a played session, and a harness's own sources for a run; the meaning is `RtxSettings::derive`'s
    /// alone. The upscaler and the map layout arrive parsed, each source parsing its own text where it enters.
    struct RtxSettingValues
    {
        /// A mode an upscaler runs at, never `Off`: no upscaler is `mUpscaler`'s to say.
        Rtx::Upscale mUpscale = Rtx::Upscale::Balanced;
        Rtx::UpscalerChoice mUpscaler = Rtx::UpscalerChoice::Fsr;
        bool mIndirectLighting = true;
        std::int32_t mBounceRays = 1;
        Rtx::LampSettings mLamps{};
        float mDistantLandCells = 0.0f;
        float mViewingDistance = 0.0f;
        bool mObjectPaging = true;
        float mObjectPagingMinSize = 0.0f;
        bool mGroundcover = false;
        float mGroundcoverDistance = 0.0f;
        float mGroundcoverDensity = 0.0f;
        bool mGroundcoverPointLighting = true;
        std::int32_t mAnisotropy = 0;
        float mGamma = 1.0f;
        bool mLitEnvironmentMaps = false;

        /// `[RTX]`, `[Camera] viewing distance`, `[Terrain]`'s paging, `[Groundcover]`, `[General] anisotropy`,
        /// `[Video] gamma` and `[Shaders] apply lighting to environment maps`: the one place the
        /// game reads these settings.
        static RtxSettingValues fromRegistry();
    };

    /// What those values mean for a ray tracer: the one derivation, for both hosts.
    struct RtxSettings
    {
        /// The mode the upscaler runs at, never `Off`: `RenderProfile::mUpscale` under an upscaler, and
        /// waiting under none.
        Rtx::Upscale mUpscale = Rtx::Upscale::Balanced;

        /// `RenderProfile::mUpscaler`: the setting.
        Rtx::UpscalerChoice mUpscaler = Rtx::UpscalerChoice::Fsr;

        /// `SamplingSwitches::mIndirect`: the setting.
        bool mIndirectLighting = true;

        /// `SamplingSwitches::mBounceRays`: the setting, from one to `Shaders::MAX_BOUNCE_RAYS`.
        std::uint32_t mBounceRays = 1;

        MirrorKnobs mMirror;

        /// `RenderProfile::mAnisotropy`: the setting, where nought means what one does.
        std::uint32_t mAnisotropy = 1;

        /// `RenderProfile::mGamma`: the setting, a finite number greater than nought.
        float mGamma = 1.0f;

        /// `RenderProfile::mLitEnvironmentMaps`: the setting.
        bool mLitEnvironmentMaps = false;

        /// Throws `Rtx::InputError` for a gamma that is not a finite number greater than nought, and for
        /// a count of bounce rays outside one to `Shaders::MAX_BOUNCE_RAYS`: a setting refused rather than
        /// defaulted, so a typo is said at once and not traced under for a session.
        static RtxSettings derive(const RtxSettingValues& values);

        /// Writes what the settings decide of a profile into `profile`, and nothing else of it: the
        /// one mapping both hosts make, the game at its start and the harness over its own line.
        void applyTo(Rtx::RenderProfile& profile) const;
    };
}
