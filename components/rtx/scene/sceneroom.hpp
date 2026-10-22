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

        /// The runs' tables, in elements: the materials' layers and their weights, the rigs' runs and
        /// influences, the morphs' offsets, the poses' words, and the deforming meshes' bind
        /// vertices.
        std::uint32_t mLayers = 0;
        std::uint32_t mMasks = 0;
        std::uint32_t mRigRuns = 0;
        std::uint32_t mInfluences = 0;
        std::uint32_t mOffsets = 0;
        std::uint32_t mPoseWords = 0;
        std::uint32_t mBindVertices = 0;
    };

    /// What a world's tables are made with room for. A world grows as cells arrive, and a growth past
    /// the room is a table and both its copies made again on the frame a cell arrives on. The suites'
    /// largest place reaches 80,324 placements, so the placements are three times that: 16 MiB of the
    /// top level's rows (64 bytes each) and 40 MiB of instance rows (`GpuInstance`, 160) a copy. One
    /// session through every place of the suites held 5,641 meshes and 1,561 materials at most, so each
    /// is five times that, for a mod list's: 896 KiB of meshes (28 bytes each) and 896 KiB of
    /// materials (112) a copy. The runs' tables are twice what that session and the crossing reached,
    /// to a power of two: 3,627 layers, 4,027,504 mask weights (16 MiB of floats, the room 32), 21,112
    /// rig runs, 4,421 influences, 22,315 morph offsets, 5,882 pose words and 48,856 bind vertices,
    /// read off `ContentMemory`. A growth past one copies the table on the device rather than staging
    /// it whole.
    ///
    /// **Held to the host-written window of a card without resizable BAR**, 246 MiB on an RTX 20:
    /// at the room the window's tables take 123 MiB live in 184 MiB of blocks, and a growth past it,
    /// 224 MiB of instance and top-level rows beside the copies frames in flight read, does not fit
    /// (`docs/rtx/research/2026-10-22-host-written-window.md`).
    inline constexpr SceneRoom sWorldRoom{ .mPlacements = 1u << 18,
        .mMeshes = 1u << 15,
        .mMaterials = 1u << 13,
        .mLayers = 1u << 13,
        .mMasks = 1u << 23,
        .mRigRuns = 1u << 16,
        .mInfluences = 1u << 14,
        .mOffsets = 1u << 16,
        .mPoseWords = 1u << 14,
        .mBindVertices = 1u << 17 };
}
