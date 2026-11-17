#pragma once

#include <cstdint>

#include <components/rtx/mirror/cells/readermemory.hpp>
#include <components/rtx/mirror/cells/ringstanding.hpp>
#include <components/rtx/preprocess/preparedheld.hpp>

namespace Rtx
{
    /// What the content holds on the host beyond what stands in the scene, for a report taken once a place: how far
    /// the mesh runs reach against what they hold, since a freed mesh leaves a hole nothing moves to close, and what of
    /// that the meshes that rest hold; how far each table of runs reaches, which its room is held to (`SceneRoom`), and
    /// what the cell reader keeps of the models it read.
    struct ContentMemory
    {
        std::uint32_t mVertexEnd = 0;
        std::uint32_t mVerticesUsed = 0;
        std::uint32_t mIndexEnd = 0;
        std::uint32_t mIndicesUsed = 0;
        std::uint32_t mRestingVertices = 0;
        std::uint32_t mRestingIndices = 0;

        /// What the host's copy of the geometry takes, as far as the runs reach (`MeshTable::getGeometryBytes`).
        std::uint64_t mGeometryBytes = 0;

        /// The runs' reach, in elements: the materials' layers and masks, the rigs' runs and
        /// influences, the morphs' offsets, the poses' words, and the deforming meshes' bind vertices.
        std::uint32_t mLayerEnd = 0;
        std::uint32_t mMaskEnd = 0;
        std::uint32_t mRigRunEnd = 0;
        std::uint32_t mInfluenceEnd = 0;
        std::uint32_t mOffsetEnd = 0;
        std::uint32_t mPoseEnd = 0;
        std::uint32_t mBindVertexEnd = 0;

        /// The textures nothing names that rest on the device until their room is needed (`TextureTable`), and the
        /// bytes they were handed over in: what the scene's own figure of textures counts with the ones named.
        std::uint32_t mRestingTextures = 0;
        std::uint64_t mRestingTextureBytes = 0;

        ReaderMemory mReader;

        /// What the files' chains made again and texels read hold that no slot was handed (`PreparedImages`), and the
        /// slots that left while theirs were read; nought with none.
        PreparedHeld mPrepared{};

        /// What the ring holds against what stands, by distance (`CellRing::countStanding`).
        RingStanding mRing{};
    };
}
