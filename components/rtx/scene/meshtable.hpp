#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <osg/BoundingBox>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/shaders/scene.h>

#include "deformertable.hpp"
#include "mesh.hpp"

namespace Rtx
{
    /// Every mesh the scene holds, and the shared buffers its triangles live in. One type, because
    /// a mesh that deforms has to give its deformer back between the free list and the two
    /// allocators. The deformers are the scene's and handed in per call rather than held: a table
    /// that held a reference to its sibling was one a defaulted move of the scene left pointing at
    /// the scene it was moved from.
    class MeshTable : public HeldRows<MeshRange>
    {
    public:
        /// How many vertices one block of the vertex attribute buffers holds, and how many indices
        /// one block of the index buffer does — the shaders' own numbers, because a shader resolves
        /// a run back to its block by dividing by the same figure.
        static constexpr Index sVertexBlock = Shaders::VERTEX_BLOCK;
        static constexpr Index sIndexBlock = Shaders::INDEX_BLOCK;

        /// Whether `arrays` fits one block, and why not where it is longer: a run that straddled
        /// two would be written across two allocations that are not next to each other. A vertex
        /// count comes out of a content file, so whoever reads one asks this before `add`, which
        /// asserts it.
        static Result<void, std::string> checkFits(const MeshArrays& arrays);

        /// Copies the vertex data into the shared buffers and returns the new mesh's index. The
        /// mesh fits a block — `checkFits`. A deforming mesh is stood on `deformer` in
        /// `deformers`, which must hold it.
        Index add(DeformerTable& deformers, const MeshArrays& arrays, FoldedShape shape, Index deformer);

        /// What a pose that changed does beside its rows: the reach, and the mesh named for the
        /// frame, once.
        void notePosed(Index mesh, const osg::BoundingBoxf& bounds);

        const BlockedValues<osg::Vec3f>& getPositions() const { return mPositions; }
        const BlockedValues<osg::Vec3f>& getNormals() const { return mNormals; }
        const BlockedValues<osg::Vec2f>& getTexCoords() const { return mTexCoords; }
        const BlockedValues<osg::Vec2f>& getSecondTexCoords() const { return mSecondTexCoords; }
        const BlockedValues<osg::Vec3f>& getColours() const { return mColours; }
        const BlockedValues<std::uint32_t>& getTangents() const { return mTangents; }
        const BlockedValues<std::uint32_t>& getIndices() const { return mIndices; }

        /// Where the vertex and the index runs are handed out: how far they reach against what
        /// stands in them, which is the room freed meshes left and nothing moves to close.
        const RunAllocator& getVertexRuns() const { return mVertexRuns; }
        const RunAllocator& getIndexRuns() const { return mIndexRuns; }

        std::span<const osg::Vec3f> getMeshPositions(Index mesh) const;
        std::span<const std::uint32_t> getMeshIndices(Index mesh) const;

        /// Which meshes changed shape since the last `clearDeformed`, each named once.
        std::span<const Index> getDeformed() const { return mDeformed.getSlots(); }
        void clearDeformed() { mDeformed.clear(); }

        std::span<const Index> getArrived() const { return mChanges.getArrived(); }
        std::span<const Index> getFreed() const { return mChanges.getFreed(); }

        /// How many times a mesh has appeared, whether at the end of the table or into a slot
        /// something else left.
        std::uint64_t getRevision() const { return mRevision; }

        /// How many triangles the standing meshes hold: counted as each arrives and leaves, where
        /// the index buffer's length counts the room a freed mesh left too.
        std::uint32_t getTriangleCount() const { return mTriangles; }
        std::size_t getGeometryBytes() const;

        void clearArrivals();

    private:
        /// What `SceneDesc::holdMesh` and `SceneDesc::drop` stand on, so every hold on a mesh is
        /// taken and given back in one place.
        friend class SceneDesc;

        void hold(Index mesh) { mRows.hold(mesh); }

        /// Gives one hold on `mesh` back, and frees it where that was the last: its geometry goes
        /// back to the allocators and a deforming mesh's runs to `deformers`, which it stood on. The
        /// slot stays where it is, because every index above it names a bottom-level acceleration
        /// structure that would otherwise be built again.
        void drop(DeformerTable& deformers, Index mesh);

        /// Makes the five vertex buffers as long as the vertex runs reach and writes `range`'s run
        /// of each, and says in `MeshRange::mTangents` whether any tangent word is not nought. Fills
        /// one the mesh did not bring with what stands for nothing there, because a reused slot
        /// still holds its last tenant's.
        void writeVertices(MeshRange& range, const MeshArrays& arrays);

        /// Records `slot` as having arrived or gone, and grows the list to reach it.
        void note(Index slot, SlotNews what);

        /// Where a mesh's vertices and its indices live — runs and not slots, because the geometry
        /// behind a row is as long as the model. One vertex run names the same elements of the
        /// five vertex buffers (`writeVertices`); the second texture coordinates are in runs of
        /// their own — `MeshRange::mSecondTexCoords`. In the device's blocks, which is what lets
        /// the host's buffers grow without moving either.
        RunAllocator mVertexRuns{ sVertexBlock };
        RunAllocator mIndexRuns{ sIndexBlock };
        RunAllocator mSecondRuns{ sVertexBlock };

        BlockedValues<osg::Vec3f> mPositions{ sVertexBlock };
        BlockedValues<osg::Vec3f> mNormals{ sVertexBlock };
        BlockedValues<osg::Vec2f> mTexCoords{ sVertexBlock };

        /// The per-vertex colour, in linear light. White where a mesh brought none, so that a
        /// hit multiplies by it whatever the content said and no shader branches on whether there
        /// is one. `MeshArrays::mColours` says why it is linear here.
        BlockedValues<osg::Vec3f> mColours{ sVertexBlock };

        /// One word a vertex, `Rtx::packTangent`'s, and nought where the mesh brought none.
        BlockedValues<std::uint32_t> mTangents{ sVertexBlock };

        BlockedValues<std::uint32_t> mIndices{ sIndexBlock };
        BlockedValues<osg::Vec2f> mSecondTexCoords{ sVertexBlock };

        /// Which meshes were posed this frame. Emptied with the placement rather than with the
        /// arrivals: a pose is a fact about the frame and an arrival is a fact about the scene.
        SlotSet mDeformed;

        /// Which slots arrived and which were freed, since a backend last read them.
        SlotChanges mChanges;

        /// How many times a mesh has appeared. `SceneDesc::getStructureRevision` says what it is
        /// read for and why a texture arriving is counted apart from it.
        std::uint64_t mRevision = 0;
        std::uint32_t mTriangles = 0;
    };
}
