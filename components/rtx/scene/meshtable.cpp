#include "meshtable.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/misc/hashstate.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        /// A run of `runs` as long as `brought`.
        template <class T>
        Run put(RunAllocator& runs, std::span<const T> brought)
        {
            assert(!brought.empty() && "a run of nothing is not a run");
            return runs.allocate(static_cast<std::uint32_t>(brought.size()));
        }
    }

    Result<void, std::string> MeshTable::checkFits(const std::size_t vertices, const std::size_t indices)
    {
        if (vertices > sVertexBlock || indices > sIndexBlock)
            return Err{ "its " + std::to_string(vertices) + " vertices and " + std::to_string(indices)
                + " indices are past the " + std::to_string(sVertexBlock) + " and " + std::to_string(sIndexBlock)
                + " one block of the shared buffers holds" };

        return {};
    }

    MeshIndex MeshTable::add(DeformerTable& deformers, const PackedMesh& mesh, DeformerIndex deformer)
    {
        assert(checkFits(mesh.mPositions.size(), mesh.mIndices.size()).isOk()
            && "a mesh past a block, which its reader was to refuse");

        const std::span<const osg::Vec3f> positions = mesh.mPositions;
        const std::span<const std::uint32_t> indices = mesh.mIndices;

        // The rest of the arrays' contract is the packer's (`PackedStreams::append`), where they were read.
        assert(!positions.empty());
        assert(mesh.mNormals.empty() || mesh.mNormals.size() == positions.size());
        assert(mesh.mTexCoords.empty() || mesh.mTexCoords.size() == positions.size());
        assert(mesh.mSecondTexCoords.empty() || mesh.mSecondTexCoords.size() == positions.size());
        assert(mesh.mColours.empty() || mesh.mColours.size() == positions.size());
        assert(mesh.mTangents.empty() || mesh.mTangents.size() == positions.size());
        assert(indices.size() % 3 == 0);
        assert(deformer == sNoRow
            || (deformers.getRows().holds(deformer) && deformers.getRows()[deformer].mKind != Deform::None
                && deformers.getRows()[deformer].getVertexCount() == positions.size()
                && "a deformer moves exactly the vertices of the mesh on it"));

        ++mRevision;

        const Run vertices = mVertexRuns.allocate(static_cast<std::uint32_t>(positions.size()));

        // **One run of indices for every mesh that shares them**, held by a count and filed under their digest: a land
        // cell's ground carried its own copy of one triangulation, 96 KiB a cell on the host and as much on the device.
        std::optional<MeshKey> sharedKey;
        Run elements;
        bool bringsIndices = true;
        if (mesh.mSharedIndices)
        {
            Misc::HashState digest;
            digest.add(indices);
            sharedKey = MeshKey{ .mWords = digest.getWords() };
            auto [shared, made] = mSharedIndices.try_emplace(*sharedKey);
            if (made)
                shared->second.mRun = put(mIndexRuns, indices);
            ++shared->second.mHolders;
            elements = shared->second.mRun;
            bringsIndices = made;
        }
        else
            elements = put(mIndexRuns, indices);
        const Run second = mesh.mSecondTexCoords.empty() ? Run{} : put(mSecondRuns, mesh.mSecondTexCoords);

        MeshRange range{
            .mVertices = vertices,
            .mIndices = elements,
            .mSharedIndexKey = sharedKey,
            .mSecondTexCoords = second,
            .mUnitStreams = mesh.mUnitStreams,
            .mTangents = !mesh.mTangents.empty(),
            .mShape = mesh.mShape,
            .mDeformer = deformer,
            .mBounds = mesh.mBounds,
            .mUvDensity = mesh.mUvDensity,
            .mKey = std::nullopt,
        };

        deformers.stand(range);
        mTriangles += range.getTriangleCount();

        const MeshIndex index = takeRow(range);
        writeArrival(index, mesh, bringsIndices);
        note(index, SlotNews::Arrived);
        return index;
    }

    void MeshTable::note(MeshIndex slot, SlotNews what)
    {
        // Grown here rather than beside every push, so everything keyed on a mesh slot reaches the
        // table's size in one place. A resize to the size it already is does not allocate, which is
        // what the frame path pays.
        mChanges.grow(size());
        mDeformed.grow(size());
        mChanges.note(slot, what);
    }

    void MeshTable::writeArrival(const MeshIndex mesh, const PackedMesh& packed, const bool bringsIndices)
    {
        const MeshRange& range = rowAt(mesh);
        const MeshArrivals::Filling into = mArrivals.add(mesh,
            MeshArrivals::Lengths{ .mVertices = range.mVertices.mCount,
                .mSecondTexCoords = range.mSecondTexCoords.mCount,
                .mIndices = bringsIndices ? range.mIndices.mCount : 0u });

        // Filled where the mesh brought none, as the device's run still holds its last tenant's. A zero normal says
        // "use the triangle's plane", a white colour "no tint" and a zero tangent word no tangent, because the first
        // and the last are read and the colour is multiplied.
        const auto fill = [](const auto& into, const auto& brought, const auto& nothing) {
            if (brought.empty())
                std::ranges::fill(into, nothing);
            else
                std::ranges::copy(brought, into.begin());
        };
        constexpr std::uint16_t whiteHalf = 0x3c00;
        fill(into.mPositions, packed.mPositions, osg::Vec3f());
        fill(into.mTexCoords, packed.mTexCoords, osg::Vec2f());
        fill(into.mNormals, packed.mNormals, 0u);
        fill(into.mColours, packed.mColours,
            Shaders::GpuVertexColour{
                .mRedGreen = whiteHalf | (std::uint32_t{ whiteHalf } << 16), .mBlue = whiteHalf });
        fill(into.mTangents, packed.mTangents, 0u);
        std::ranges::copy(packed.mSecondTexCoords, into.mSecondTexCoords.begin());
        if (bringsIndices)
            std::ranges::copy(packed.mIndices, into.mIndices.begin());

        if (mKeepsDigests)
        {
            MeshGeometry whole = mArrivals.of(mesh);
            whole.mIndices = packed.mIndices;
            mDigests.resize(size());
            mDigests[mesh] = digestMesh(whole);
        }
    }

    void MeshTable::keepDigests()
    {
        assert(size() == 0 && "digests asked of a table that holds meshes without one");
        mKeepsDigests = true;
    }

    const MeshDigest& MeshTable::getDigest(const MeshIndex mesh) const
    {
        assert(mKeepsDigests && "a digest asked of a table that keeps none");
        assert((isLive(mesh) || rowAt(mesh).mRest.mResting) && "the digest of a mesh the table does not hold");
        return mDigests[mesh];
    }

    void MeshTable::notePosed(MeshIndex mesh, const osg::BoundingBoxf& bounds)
    {
        MeshRange& range = rowAt(mesh);
        range.mPosed = true;

        // A pose the size of the last one still reaches somewhere else. An arm that came down is
        // the same count of vertices in a different place, and a box left where the bind pose put it
        // is what a camera would then be framed from.
        range.mBounds = bounds;

        // Named once however many callers reach it, because a backend builds one structure per mesh
        // and building it twice in a frame is the same answer for twice the cost.
        mDeformed.add(mesh);
    }

    MeshIndex MeshTable::add(DeformerTable& deformers, const PackedMesh& mesh)
    {
        assert(!mKeyIndex.contains(mesh.mKey) && "a key filed twice, which `find` answers first");
        const MeshIndex added = add(deformers, mesh, sNoRow);
        rowAt(added).mKey = mesh.mKey;
        mKeyIndex.emplace(mesh.mKey, added);
        return added;
    }

    MeshIndex MeshTable::find(const MeshKey& key)
    {
        const auto known = mKeyIndex.find(key);
        if (known == mKeyIndex.end())
            return sNoRow;

        if (rowAt(known->second).mRest.mResting)
            wake(known->second);
        return known->second;
    }

    void MeshTable::stand(const MeshIndex mesh, const std::uint64_t structureBytes)
    {
        assert(isLive(mesh) && "a mesh stood that the table does not hold");
        MeshRange& range = rowAt(mesh);
        // A device builds what arrived or waited, and a rebuild comes after everything that rests is freed, so nothing
        // resting is built: the rest's bytes stay what each mesh rested with.
        assert(!range.mRest.mResting && "a resting mesh's structure was built");
        range.mStoodBytes = structureBytes;
        // Loose until its copy lands, which gives back nothing a waiting structure takes.
        range.mRest.mFit = 0;
    }

    void MeshTable::fit(const MeshIndex mesh, const std::uint64_t bytes, const std::uint64_t alignment)
    {
        assert((isLive(mesh) || rowAt(mesh).mRest.mResting) && "a mesh the table does not hold fitted");
        mResting.fit(mesh, bytes, alignment, placeOf());
    }

    bool MeshTable::releaseFitting(DeformerTable& deformers, const std::uint64_t hole, const std::uint64_t alignment)
    {
        settleResting();
        const MeshIndex fitting = mResting.firstFitting(hole, alignment, placeOf());
        if (fitting == sNoRow)
            return false;
        release(deformers, fitting);
        return true;
    }

    void MeshTable::drop(DeformerTable& deformers, const MeshIndex mesh, const HoldTier tier)
    {
        if (!dropRow(mesh, tier))
            return;

        // **A static mesh the device stands whole rests** (`docs/architecture.md`, "One residency policy for the
        // content"); anything else goes now, a mesh the device has not stood among them.
        const MeshRange& range = rowAt(mesh);
        if (range.mKey.has_value() && range.mStoodBytes > 0)
            rest(mesh);
        else
            release(deformers, mesh);
    }

    void MeshTable::rest(const MeshIndex mesh)
    {
        const MeshRange& range = rowAt(mesh);
        mResting.rest(mesh, range.mStoodBytes, placeOf());
        mRestingGeometry.mVertices += range.mVertices.mCount;
        mRestingGeometry.mIndices += range.mIndices.mCount;
    }

    void MeshTable::wake(const MeshIndex mesh)
    {
        const MeshRange& range = rowAt(mesh);
        mResting.wake(mesh, range.mStoodBytes, placeOf());
        assert(mRestingGeometry.mVertices >= range.mVertices.mCount && "a wake past the vertices that rest");
        assert(mRestingGeometry.mIndices >= range.mIndices.mCount && "a wake past the indices that rest");
        mRestingGeometry.mVertices -= range.mVertices.mCount;
        mRestingGeometry.mIndices -= range.mIndices.mCount;
    }

    void MeshTable::releaseOldest(DeformerTable& deformers)
    {
        settleResting();
        assert(mResting.first() != sNoRow && "the oldest freed where nothing rests");
        release(deformers, mResting.first());
    }

    void MeshTable::release(DeformerTable& deformers, const MeshIndex mesh)
    {
        MeshRange& range = rowAt(mesh);
        if (range.mRest.mResting)
            wake(mesh);
        if (range.mKey.has_value())
            mKeyIndex.erase(*range.mKey);
        range.mKey.reset();
        range.mStoodBytes = 0;
        range.mRest.mFit = 0;

        // The allocators merge the room with whatever it touches, so a cell leaves as the one hole
        // it came as.
        mVertexRuns.release(range.mVertices);
        if (range.mSharedIndexKey.has_value())
        {
            const auto shared = mSharedIndices.find(*range.mSharedIndexKey);
            assert(shared != mSharedIndices.end() && shared->second.mHolders > 0
                && "a mesh that shares indices no run of them holds");
            if (--shared->second.mHolders == 0)
            {
                mIndexRuns.release(range.mIndices);
                mSharedIndices.erase(shared);
            }
            range.mSharedIndexKey.reset();
        }
        else
            mIndexRuns.release(range.mIndices);
        mSecondRuns.release(range.mSecondTexCoords);
        deformers.release(range);
        mTriangles -= range.getTriangleCount();

        range.mVertices.mCount = 0;
        range.mIndices.mCount = 0;
        range.mSecondTexCoords.mCount = 0;
        range.mUnitStreams = 0;
        range.mTangents = false;
        range.mBounds = osg::BoundingBoxf();
        range.mUvDensity = UvDensity{};

        // A slot given back names no structure to refit, however it was posed this frame: the
        // structure has gone with it.
        mDeformed.remove(mesh);
        mArrivals.forget(mesh);

        freeRow(mesh);
        note(mesh, SlotNews::Freed);
    }

    void MeshTable::compact(const std::uint64_t clock)
    {
        mDeformed.compact();
        mChanges.compact();
        mClock = clock;
        settleResting();
    }

    void MeshTable::clearArrivals()
    {
        mChanges.clearArrivals();
        mArrivals.clear();
    }
}
