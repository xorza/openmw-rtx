#pragma once

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

namespace Settings
{
    /// The ray tracing renderer's own settings, read as they are wanted.
    struct RTXCategory : WithIndex
    {
        using WithIndex::WithIndex;

        /// The most cells `distant land cells` takes: a bound on how much world a frame is asked to
        /// stand, and so on what the ring's ask costs. Nought stays nought, which hands the reach
        /// back to `viewing distance`.
        static constexpr float sMaxDistantLandCells = 10.0f;

        /// The fewest the two menus offer. A file may name fewer, and the launcher leaves such a
        /// value as it found it.
        static constexpr float sMinDistantLandCellsInMenu = 4.0f;

        /// The most bounce rays the eye's surface may trace, which the trace keeps a landing's list for.
        static constexpr int sMaxBounceRays = static_cast<int>(Rtx::Shaders::MAX_BOUNCE_RAYS);

        /// How far out from the eye the world is built, in cells: handed to the world mirror where
        /// the renderer is made and again when the menu moves it, so the rings, the air and the map
        /// follow one number. How much world exists is a property of the structure rays are cast
        /// against and not of the camera, which is what `viewing distance` is about; the air is
        /// tuned to it as well as the ground (`Rtx::CellGrid::reachOf`).
        SettingValue<float> mDistantLandCells{ mIndex, "RTX", "distant land cells",
            makeClampSanitizerFloat(0.0f, sMaxDistantLandCells) };

        /// How hard the upscaler works, or `off`, as `Rtx::sUpscaleNames` spells the modes and read
        /// by it at load. Changing it rebuilds every target. `Rtx::sUpscaleMenu` is what the launcher
        /// and the settings window offer of it.
        SettingValue<Rtx::Upscale> mUpscale{ mIndex, "RTX", "upscale" };

        /// How many bounce rays the eye's surface traces a frame: `Rtx::SamplingSwitches::mBounceRays`,
        /// handed to the renderer where it is made and again when the menu moves it.
        SettingValue<int> mBounceRays{ mIndex, "RTX", "bounce rays", makeClampSanitizerInt(1, sMaxBounceRays) };

        /// What the content's `_spec` maps mean, as `Rtx::sSpecularLayoutNames` spells the layouts:
        /// `classic` or `metal roughness`, read by it at load. Read where the renderer is
        /// made, because the maps are loaded with the models.
        SettingValue<Rtx::SpecularLayout> mSpecularMapLayout{ mIndex, "RTX", "specular map layout" };
    };
}
