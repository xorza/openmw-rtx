#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a backend built a scene's slot from, by kind: how many times each kind a build or an
    /// extend stages has appeared in the scene's tables. Equal is nothing to stage; any other is an
    /// arrival, and the field that moved says of what. A kind added later is a field here, which the
    /// compiler then shows at every reader, where a sum hid one kind behind another.
    struct StructureRevision
    {
        /// Meshes, each of which is a bottom-level structure to build (`MeshTable::getRevision`).
        std::uint64_t mMeshes = 0;

        /// Textures, each an upload into the array (`TextureTable::getRevision`).
        std::uint64_t mTextures = 0;

        /// A material's layer and mask runs, staged once on arrival and never again
        /// (`MaterialTable::getRunRevision`).
        std::uint64_t mMaterialRuns = 0;

        bool operator==(const StructureRevision& other) const = default;
    };
}
