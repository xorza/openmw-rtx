#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/shaders/scene.h>

#include "foldedshape.hpp"
#include "mesh.hpp"
#include "uvdensity.hpp"

namespace Rtx
{
    /// **A mesh in the device's form, with its key and its bounds, as its reader made them**: what `SceneDesc::addMesh`
    /// takes, so the frame that adopts it copies and packs nothing. The streams are parallel, each empty where the mesh
    /// brings none or as long as `mPositions`; the normals and tangents are `Shaders::packNormal`'s and
    /// `Shaders::packTangent`'s words and the colours halves in linear light.
    struct PackedMesh
    {
        std::span<const osg::Vec3f> mPositions;
        std::span<const std::uint32_t> mNormals{};
        std::span<const osg::Vec2f> mTexCoords{};
        std::span<const osg::Vec2f> mSecondTexCoords{};

        /// `MeshArrays::mUnitStreams`.
        std::uint32_t mUnitStreams = 0;
        std::span<const Shaders::GpuVertexColour> mColours{};

        /// Empty where every word packed to nought, which is no tangent.
        std::span<const std::uint32_t> mTangents{};
        std::span<const std::uint32_t> mIndices{};

        /// `MeshArrays::mSharedIndices`.
        bool mSharedIndices = false;

        FoldedShape mShape{};

        /// The box every position fits in.
        osg::BoundingBoxf mBounds;

        /// The least texture it lays on a unit of its surface (`UvDensity`).
        UvDensity mUvDensity{};

        /// What a static mesh is filed under: every stream in order, each a step of its own, the units the second
        /// coordinates stream to and the shape.
        MeshKey mKey;
    };

    /// What a static mesh of `mesh`'s streams, units and shape is filed under (`PackedMesh::mKey`), its own key and
    /// bounds aside.
    MeshKey keyOf(const PackedMesh& mesh);

    /// A vertex's colour as the device reads it, three halves, and white where a channel stands past the largest half,
    /// which a hit would multiply into the light as an infinity.
    Shaders::GpuVertexColour packColour(const osg::Vec3f& colour);

    /// Where one mesh stands in a `PackedStreams`, with what was made of it there.
    struct PackedRuns
    {
        Run mVertices;
        Run mNormals;
        Run mTexCoords;
        Run mSecondTexCoords;
        Run mColours;
        Run mTangents;
        Run mIndices;
        std::uint32_t mUnitStreams = 0;
        bool mSharedIndices = false;
        FoldedShape mShape;
        osg::BoundingBoxf mBounds;
        UvDensity mUvDensity;
        MeshKey mKey;
    };

    /// **Meshes packed into the device's form, laid end to end**, one buffer a stream: a reader's, which packs, keys
    /// and bounds a mesh on its own thread (`docs/architecture.md`, "The geometry has one host copy, for one
    /// hand-over").
    class PackedStreams
    {
    public:
        /// Packs `arrays` shaped as `shape`, which must be finite and parallel as `MeshArrays` states, and appends it.
        PackedRuns append(const MeshArrays& arrays, FoldedShape shape);

        /// The mesh at `runs`, which this appended.
        PackedMesh of(const PackedRuns& runs) const;

        /// Empties the streams, keeping their room.
        void clear();

        /// Empties the streams, giving back the room of each its content filled less than half of (`dropSlack`).
        void reuse();

        /// Empties the streams and gives their room back.
        void letGo();

        /// The room the streams grew to, and what they hold of it, in bytes.
        std::size_t getRoomBytes() const;
        std::size_t getContentBytes() const;

    private:
        std::vector<osg::Vec3f> mPositions;
        std::vector<std::uint32_t> mNormals;
        std::vector<osg::Vec2f> mTexCoords;
        std::vector<osg::Vec2f> mSecondTexCoords;
        std::vector<Shaders::GpuVertexColour> mColours;
        std::vector<std::uint32_t> mTangents;
        std::vector<std::uint32_t> mIndices;
    };
}
