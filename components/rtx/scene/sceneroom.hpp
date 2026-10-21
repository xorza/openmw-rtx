#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a scene's tables are made with room for before one grows, which makes the table again on
    /// the frame that needs it and rewrites it whole: the backend's on the device, and the scene's own
    /// rows on the host (`SceneDesc`), so both grow at the same points. A picture's scene is opened at
    /// what it holds, nought, since a doll grows by a piece of armour and not by a town.
    struct SceneRoom
    {
        /// Placement slots: the top level, its rows and the instance rows.
        std::uint32_t mPlacements = 0;

        /// Mesh rows.
        std::uint32_t mMeshes = 0;

        /// Material rows, the untextured one among them (`Shaders::MATERIAL_ROW_FIRST`).
        std::uint32_t mMaterials = 0;
    };

    /// What a world's tables are made with room for. A world grows as cells arrive, and a growth past
    /// the room is a table and both its copies made again on the frame a cell arrives on. The suites'
    /// largest place reaches 80,324 placements, so the placements are three times that: 16 MiB of the
    /// top level's rows (64 bytes each) and 40 MiB of instance rows (`GpuInstance`, 160) a copy. One
    /// session through every place of the suites held 5,641 meshes and 1,561 materials at most, so each
    /// is five times that, for a mod list's: 896 KiB of meshes (28 bytes each) and 896 KiB of
    /// materials (112) a copy.
    inline constexpr SceneRoom sWorldRoom{ .mPlacements = 1u << 18, .mMeshes = 1u << 15, .mMaterials = 1u << 13 };
}
