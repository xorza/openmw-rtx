#pragma once

#include <cstdint>

#include <components/rtx/mirror/cells/readermemory.hpp>

namespace Rtx
{
    /// What the content holds on the host beyond what stands in the scene, for a report taken once
    /// a place: how far the mesh runs reach against what they hold, since a freed mesh leaves a
    /// hole nothing moves to close, how far each table of runs reaches, which its room is held to
    /// (`SceneRoom`), and what the cell reader keeps of the models it read.
    struct ContentMemory
    {
        std::uint32_t mVertexEnd = 0;
        std::uint32_t mVerticesUsed = 0;
        std::uint32_t mIndexEnd = 0;
        std::uint32_t mIndicesUsed = 0;

        /// The runs' reach, in elements: the materials' layers and masks, the rigs' runs and
        /// influences, the morphs' offsets, the poses' words, and the deforming meshes' bind vertices.
        std::uint32_t mLayerEnd = 0;
        std::uint32_t mMaskEnd = 0;
        std::uint32_t mRigRunEnd = 0;
        std::uint32_t mInfluenceEnd = 0;
        std::uint32_t mOffsetEnd = 0;
        std::uint32_t mPoseEnd = 0;
        std::uint32_t mBindVertexEnd = 0;

        ReaderMemory mReader;
    };
}
