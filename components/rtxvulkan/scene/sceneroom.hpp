#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a scene's tables are made with room for before one grows, which makes the table again on
    /// the frame that needs it and rewrites it whole. A picture's scene is opened at what it holds,
    /// nought, since a doll grows by a piece of armour and not by a town.
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
    /// largest place reaches 80,324 placements, so the placements are three times that, at 16 MiB of
    /// rows a copy. One session through every place of the suites held 5,641 meshes and 1,561
    /// materials at most, so each is five times that, for a mod list's: 768 KiB of meshes and
    /// 864 KiB of materials a copy.
    inline constexpr SceneRoom sWorldRoom{ .mPlacements = 1u << 18, .mMeshes = 1u << 15, .mMaterials = 1u << 13 };
}
