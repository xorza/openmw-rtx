#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SKIN_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SKIN_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// `<cstddef>` for the `offsetof` the pinned layout below is checked with, last because only the
// host has it.
#ifdef RTX_HOST
#include <cstddef>
#endif

// What `skin.comp` and `morph.comp` are handed, one dispatch a deformed mesh. `skinning.h` says what
// a rig is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What one skinned mesh's dispatch is handed: where its bind pose, its rig and its rows are,
    /// and where the pose goes.
    ///
    /// **Addresses of the runs themselves and not of the tables**, so the kernel indexes from
    /// nought. `Rtx::SceneDesc` never lets a run straddle a block, so one address covers it.
    struct SkinConstants
    {
        uint64 mBindPositions;
        uint64 mBindNormals;

        /// `Rtx::packTangent`'s words: posed as the normals are, the handedness kept, and nought
        /// kept nought.
        uint64 mBindTangents;
        uint64 mRuns;
        uint64 mInfluences;
        uint64 mBones;
        uint64 mPositions;
        uint64 mNormals;
        uint64 mTangents;
        uint mCount;
    };

    /// The same for a morphed mesh: its base, every target's offsets laid end to end, this frame's
    /// weights, and where the positions go. A morph moves no normal.
    struct MorphConstants
    {
        uint64 mBase;
        uint64 mOffsets;
        uint64 mWeights;
        uint64 mPositions;
        uint mCount;
        uint mTargets;
    };

#ifdef RTX_HOST
    static_assert(
        offsetof(SkinConstants, mCount) + sizeof(uint) == 76, "SkinConstants must be scalar-packed on every side");
    static_assert(sizeof(MorphConstants) == 40, "MorphConstants must be scalar-packed on every side");
}
#endif

#endif
