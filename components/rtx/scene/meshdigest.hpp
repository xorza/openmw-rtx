#pragma once

#include <components/misc/digestwords.hpp>

namespace Rtx
{
    struct MeshGeometry;

    /// What a mesh's geometry digests to, made as it arrives where the host asks for digests
    /// (`MeshTable::keepDigests`): the harness's, which compares two runs by them once the host has let the geometry go
    /// (`docs/architecture.md`, "The harness digests a mesh's shape only where the host asks for digests").
    struct MeshDigest
    {
        /// Each stream's run as it stands: what says which stream a difference of layout landed in. The tangents are
        /// the normals' and the second and third coordinates the first's, each only where the mesh has any.
        Misc::DigestWords mPositions{};
        Misc::DigestWords mNormals{};
        Misc::DigestWords mTexCoords{};
        Misc::DigestWords mColours{};
        Misc::DigestWords mIndices{};

        /// The multiset of its triangles, each turned to start at its least corner, so the winding counts and the
        /// corner it happens to be spelt from does not: its shape, whatever order the content laid it in.
        Misc::DigestWords mTriangles{};
    };

    /// `geometry`'s digests, its indices whole however many meshes share them.
    MeshDigest digestMesh(const MeshGeometry& geometry);
}
