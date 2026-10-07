#pragma once

#include <cstdint>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/mirrorknobs.hpp>
#include <components/rtx/scene/specularlayout.hpp>

namespace MWRender
{
    /// The ray tracer's settings as values, before anything reads their meaning. Two sources fill
    /// one: the player's registry for a played session, and a harness's own for a run — its
    /// command line, the player's registry for a watched window, the shipped defaults for a
    /// measured one. The sources stay apart and the meaning is `RtxSettings::derive`'s alone.
    ///
    /// The upscaler and the map layout arrive read: each source parses its own text where it
    /// enters, the registry at load.
    struct RtxSettingValues
    {
        Rtx::Upscale mUpscale = Rtx::Upscale::Off;
        float mDistantLandCells = 0.0f;
        float mViewingDistance = 0.0f;
        bool mObjectPaging = true;
        float mObjectPagingMinSize = 0.0f;
        bool mGroundcover = false;
        float mGroundcoverDistance = 0.0f;
        float mGroundcoverDensity = 0.0f;
        bool mGroundcoverPointLighting = true;
        Rtx::SpecularLayout mSpecularMapLayout = Rtx::SpecularLayout::Ignore;
        int mAnisotropy = 0;
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
        Rtx::Upscale mUpscale = Rtx::Upscale::Off;
        Rtx::MirrorKnobs mMirror;

        /// `RenderProfile::mAnisotropy`: the setting, where nought means what one does.
        std::uint32_t mAnisotropy = 1;

        /// `RenderProfile::mGamma`: the setting, a finite number greater than nought.
        float mGamma = 1.0f;

        /// `RenderProfile::mLitEnvironmentMaps`: the setting.
        bool mLitEnvironmentMaps = false;

        /// Throws `Rtx::InputError` for a gamma that is not a finite number greater than nought: a
        /// setting refused rather than defaulted, so a typo is said at once and not traced under for
        /// a session.
        static RtxSettings derive(const RtxSettingValues& values);
    };
}
