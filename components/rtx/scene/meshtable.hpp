#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/BoundingBox>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/misc/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/shaders/scene.h>

#include "deformertable.hpp"
#include "foldedshape.hpp"
#include "mesh.hpp"
#include "mesharrivals.hpp"
#include "meshdigest.hpp"
#include "packedstreams.hpp"
#include "restqueue.hpp"

namespace Rtx
{
    /// What the meshes that rest hold of the vertex and the index runs (`MeshTable::getRestingGeometry`).
    struct RestingGeometry
    {
        std::uint32_t mVertices = 0;
        std::uint32_t mIndices = 0;
    };

    /// Every mesh the scene holds, the runs of the device's streams its triangles live in, and the geometry of what
    /// arrived since the last hand-over (`MeshArrivals`). One type, because
    /// a mesh that deforms has to give its deformer back between the free list and the two
    /// allocators. The deformers are the scene's and handed in per call rather than held: a table
    /// that held a reference to its sibling would be left pointing at the scene a defaulted move
    /// came from.
    class MeshTable : public HeldRows<MeshRange>
    {
    public:
        /// How many vertices one block of the vertex attribute buffers holds, and how many indices
        /// one block of the index buffer does — the shaders' own numbers, because a shader resolves
        /// a run back to its block by dividing by the same figure.
        static constexpr Index sVertexBlock = Shaders::VERTEX_BLOCK;
        static constexpr Index sIndexBlock = Shaders::INDEX_BLOCK;

        /// Room for `count` meshes in the rows and the sets keyed on them, so no arrival up to it
        /// moves a row. The vertices are in blocks, which never move.
        void reserve(std::size_t count)
        {
            reserveRows(count);
            mChanges.reserve(count);
            mDeformed.reserve(count);
            mKeyIndex.reserve(count);
            mResting.reserve(count);
        }

        /// Whether a mesh of `vertices` and `indices` fits one block, and why not where it is longer: a run that
        /// straddled two would be written across two allocations that are not next to each other. A vertex count comes
        /// out of a content file, so whoever reads one asks this before `add`, which asserts it.
        static Result<void, std::string> checkFits(std::size_t vertices, std::size_t indices);

        /// Where the vertex, the index and the second coordinates' runs are handed out: how far they reach against
        /// what stands in them, which is the room freed meshes left and nothing moves to close. How far a device's
        /// streams reach.
        const RunAllocator& getVertexRuns() const { return mVertexRuns; }
        const RunAllocator& getIndexRuns() const { return mIndexRuns; }
        const RunAllocator& getSecondRuns() const { return mSecondRuns; }

        /// `mesh`'s geometry in the device's form, which arrived since the last `clearArrivals`: what a hand-over
        /// writes, and nothing holds after it (`MeshArrivals`).
        MeshGeometry getArrival(Index mesh) const { return mArrivals.of(mesh); }

        /// Makes a digest of each mesh as it arrives, which nothing reads but the harness: asked of a table that holds
        /// nothing yet, so every mesh has one.
        void keepDigests();
        bool keepsDigests() const { return mKeepsDigests; }

        /// What `mesh`, standing or resting, digested to as it arrived (`keepDigests`).
        const MeshDigest& getDigest(Index mesh) const;

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

        /// What the host holds of the geometry: the arrivals' buffers (`MeshArrivals::getBytes`).
        std::size_t getGeometryBytes() const { return mArrivals.getBytes(); }

        /// Settles what a drop took out of the posed and the arrived meshes, so they can be read, and files the
        /// meshes that came to rest since the last at `clock`, the scene's.
        void compact(std::uint64_t clock);

        /// The static mesh filed under `key`, taken back where it rests, or `sNoIndex`: nothing described and nothing
        /// uploaded. A mesh that stands is answered too, so two drawables of one geometry are one mesh. The caller
        /// holds it before anything can drop it.
        Index find(const MeshKey& key);

        /// What the device stands of `mesh`'s bottom-level structure, `structureBytes`, the device's answer: what lets
        /// a static mesh rest, and what its rest holds (`MeshRange::mStoodBytes`).
        void stand(Index mesh, std::uint64_t structureBytes);

        /// What `mesh`'s structure gives back of the storage its tight copy stands in, `bytes`, the device's answer
        /// (`DeviceAnswer::mMeshFits`): the hole its eviction leaves for a structure the device waits on. Nought from
        /// each build until its copy lands.
        void fit(Index mesh, std::uint64_t bytes);

        /// The meshes that rest, oldest first (`RestQueue`): what the scene frees across its tables.
        const RestQueue& getResting() const { return mResting; }

        /// What the meshes that rest hold of the vertex and the index runs: what stands in the runs and no frame draws.
        RestingGeometry getRestingGeometry() const { return mRestingGeometry; }

        void clearArrivals();

    private:
        /// A row's place in the rest, for `RestQueue`.
        auto placeOf()
        {
            return [this](const Index at) -> RestPlace& { return rowAt(at).mRest; };
        }

        /// What `SceneDesc::holdMesh` and `SceneDesc::drop` stand on, so every hold on a mesh is
        /// taken and given back in one place, and every write that crosses into the deformers is
        /// made through the scene.
        friend class SceneDesc;

        /// Takes runs of the streams for the mesh, files its geometry with the arrivals and returns the new mesh's
        /// index. The mesh fits a block — `checkFits`. A deforming mesh is stood on `deformer` in
        /// `deformers`, which must hold it.
        Index add(DeformerTable& deformers, const PackedMesh& mesh, Index deformer);

        /// The same for a static mesh, filed under its key, which `find` answers and it rests under.
        Index add(DeformerTable& deformers, const PackedMesh& mesh);

        /// What a pose that changed does beside its rows: the reach, and the mesh named for the
        /// frame, once.
        void notePosed(Index mesh, const osg::BoundingBoxf& bounds);

        void hold(Index mesh) { holdRow(mesh); }

        /// Gives one hold on `mesh` back, and where that was the last, rests a static mesh the device stands whole
        /// and frees anything else: its geometry goes back to the allocators and a deforming mesh's runs to
        /// `deformers`, which it stood on. The slot stays where it is, because every index above it names a
        /// bottom-level acceleration structure that would otherwise be built again.
        void drop(DeformerTable& deformers, Index mesh);

        /// Frees `mesh`, which nothing holds, as `drop` frees it.
        void release(DeformerTable& deformers, Index mesh);

        /// Frees the mesh that has rested longest, which the scene asks for across its tables.
        void releaseOldest(DeformerTable& deformers);

        /// Frees the mesh that has rested longest of those whose eviction gives back at least `hole` where a structure
        /// the device waits on can take it, and answers whether one did (`RestQueue::firstFitting`).
        bool releaseFitting(DeformerTable& deformers, std::uint64_t hole);

        /// Files the meshes that came to rest since the last, by their slots, at the clock `compact` was last given.
        void settleResting() { mResting.settle(mClock, placeOf()); }

        /// `mesh` laid to rest and woken, in the queue and in `mRestingGeometry` together.
        void rest(Index mesh);
        void wake(Index mesh);

        /// Files `mesh`'s geometry, `packed`, with the arrivals, its indices where `bringsIndices`. Fills a stream the
        /// mesh did not bring with what stands for nothing there, as the device's run holds its last tenant's. Its
        /// digest beside it, where they are kept.
        void writeArrival(Index mesh, const PackedMesh& packed, bool bringsIndices);

        /// Records `slot` as having arrived or gone, and grows the list to reach it.
        void note(Index slot, SlotNews what);

        /// The static meshes by their key, one slot a key, which leaves with its slot; the meshes that rest; and the
        /// scene's clock at the last `compact`.
        boost::unordered_flat_map<MeshKey, Index, MeshKeyHash> mKeyIndex;

        /// The runs of indices meshes share (`MeshArrays::mSharedIndices`), by their digest, with how many meshes
        /// name each.
        struct SharedIndices
        {
            Run mRun;
            std::uint32_t mHolders = 0;
        };
        boost::unordered_flat_map<MeshKey, SharedIndices, MeshKeyHash> mSharedIndices;
        RestQueue mResting;
        RestingGeometry mRestingGeometry;
        std::uint64_t mClock = 0;

        /// Where a mesh's vertices and its indices live on the device — runs and not slots, because the geometry
        /// behind a row is as long as the model. One vertex run names the same elements of the five vertex streams;
        /// the second texture coordinates are in runs of their own — `MeshRange::mSecondTexCoords`. In the device's
        /// blocks, which is what lets its buffers grow without moving any.
        RunAllocator mVertexRuns{ sVertexBlock };
        RunAllocator mIndexRuns{ sIndexBlock };
        RunAllocator mSecondRuns{ sVertexBlock };

        /// The geometry of what arrived since the last hand-over, the host's one copy of it.
        MeshArrivals mArrivals;

        /// Each slot's digest as its mesh arrived, where `keepDigests` asked for them: as long as the rows.
        std::vector<MeshDigest> mDigests;
        bool mKeepsDigests = false;

        /// Which meshes were posed this frame. Emptied with the placement rather than with the
        /// arrivals: a pose is a fact about the frame and an arrival is a fact about the scene.
        SlotSet mDeformed;

        /// Which slots arrived and which were freed, since a backend last read them.
        SlotChanges mChanges;

        /// How many times a mesh has appeared: what `StructureRevision::mMeshes` reads, a structure
        /// to build for each, which a texture or a material's runs arriving are counted apart from.
        std::uint64_t mRevision = 0;
        std::uint32_t mTriangles = 0;
    };
}
