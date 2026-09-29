#pragma once

#include <cstdint>
#include <string_view>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>

namespace MWRender
{
    /// The ray tracer's settings as values, before anything reads their meaning. Two sources fill
    /// one: the player's registry for a played session, and a harness's own for a run — its
    /// command line, the player's registry for a watched window, the shipped defaults for a
    /// measured one. The sources stay apart and the meaning is `RtxSettings::derive`'s alone.
    ///
    /// The spellings are views into whatever filled them, so one lives for the one call it is
    /// read in.
    struct RtxSettingValues
    {
        std::string_view mUpscale;
        float mDistantLandCells = 0.0f;
        float mViewingDistance = 0.0f;
        bool mObjectPaging = true;
        float mObjectPagingMinSize = 0.0f;
        std::string_view mSpecularMapLayout;
        int mAnisotropy = 0;
        bool mGroundcover = false;

        /// `[RTX]`, `[Camera] viewing distance`, `[Terrain]`'s paging, `[General] anisotropy` and
        /// `[Groundcover] enabled`: the one place the game reads these settings.
        static RtxSettingValues fromRegistry();
    };

    /// What those values mean for a ray tracer: the one derivation, for both hosts.
    struct RtxSettings
    {
        Rtx::Upscaling mUpscaling;
        Rtx::MirrorKnobs mMirror;

        /// `RenderProfile::mAnisotropy`: the setting, where nought means what one does.
        std::uint32_t mAnisotropy = 1;

        /// Throws `Rtx::InputError` for a spelling that names no mode: a setting refused rather
        /// than defaulted, so a typo is said at once and not traced under for a session.
        static RtxSettings derive(const RtxSettingValues& values);
    };
}
