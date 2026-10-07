#pragma once

#include <string>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

namespace Settings
{
    /// The experimental ray tracing renderer. Which renderer draws is read once, before the window
    /// exists; the rest are read as they are wanted.
    struct RTXCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mEnabled{ mIndex, "RTX", "enabled" };

        /// The most cells `distant land cells` takes: a bound on how much world a frame is asked to
        /// stand, and so on what the ring's ask costs. Nought stays nought, which hands the reach
        /// back to `viewing distance`.
        static constexpr float sMaxDistantLandCells = 10.0f;

        /// The fewest the two menus offer. A file may name fewer, and the launcher leaves such a
        /// value as it found it.
        static constexpr float sMinDistantLandCellsInMenu = 4.0f;

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

        /// What the content's `_spec` maps mean, as `Rtx::sSpecularLayoutNames` spells the layouts:
        /// `ignore`, `classic` or `metal roughness`, read by it at load. Read where the renderer is
        /// made, because the maps are loaded with the models.
        SettingValue<Rtx::SpecularLayout> mSpecularMapLayout{ mIndex, "RTX", "specular map layout" };

        /// Whether a surface takes light from anything that is not a light, as
        /// `Rtx::sIndirectLightNames` spells it: `traced`, one bounce and the passes that clean it, or
        /// `off`, none, which costs none of them. A name the list refuses rather than defaults.
        /// Changed while the game runs.
        SettingValue<std::string> mIndirectLight{ mIndex, "RTX", "indirect light" };
    };
}
