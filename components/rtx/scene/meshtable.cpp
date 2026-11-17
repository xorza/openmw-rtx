#include "meshtable.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/misc/hashstate.hpp>
#include <components/rtx/common/finite.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/tangent.h>

namespace Rtx
{
    namespace
    {
        /// A vertex's tangent as the device stores it, `Shaders::TANGENT_*`, from
        /// `osgUtil::TangentSpaceGenerator`'s: a direction, and in `w` the handedness the bitangent
        /// `cross(N, T) * w` is taken with.
        std::uint32_t packTangent(const osg::Vec4f& tangent)
        {
            return Shaders::packTangent(osg::Vec3f(tangent.x(), tangent.y(), tangent.z()), tangent.w() < 0.0f);
        }

        /// A vertex's colour as the device reads it, `Shaders::GpuVertexColour`: three half floats.
        ///
        /// **White where a channel stands past the largest half**, as `MeshReader` reads a colour that
        /// is not a finite number: the half would be an infinity, which a hit multiplies into the
        /// light and every filter after it into NaNs.
        Shaders::GpuVertexColour packColour(const osg::Vec3f& colour)
        {
            const std::uint16_t red = toHalf(colour.x());
            const std::uint16_t green = toHalf(colour.y());
            const std::uint16_t blue = toHalf(colour.z());
            const auto infinite = [](const std::uint16_t half) { return (half & 0x7c00u) == 0x7c00u; };
            if (infinite(red) || infinite(green) || infinite(blue))
                return packColour(osg::Vec3f(1.0f, 1.0f, 1.0f));

            return Shaders::GpuVertexColour{
                .mRedGreen = std::uint32_t{ red } | (std::uint32_t{ green } << 16),
                .mBlue = blue,
            };
        }

        /// The box every one of `positions` fits in.
        osg::BoundingBoxf boundsOf(std::span<const osg::Vec3f> positions)
        {
            osg::BoundingBoxf bounds;
            for (const osg::Vec3f& position : positions)
                bounds.expandBy(position);

            return bounds;
        }

        /// A run of `runs` as long as `brought`.
        template <class T>
        Run put(RunAllocator& runs, std::span<const T> brought)
        {
            assert(!brought.empty() && "a run of nothing is not a run");
            return runs.allocate(static_cast<std::uint32_t>(brought.size()));
        }
    }

    Result<void, std::string> MeshTable::checkFits(const MeshArrays& arrays)
    {
        const std::span<const osg::Vec3f> positions = arrays.mPositions;
        const std::span<const std::uint32_t> indices = arrays.mIndices;

        if (positions.size() > sVertexBlock || indices.size() > sIndexBlock)
            return Err{ "its " + std::to_string(positions.size()) + " vertices and " + std::to_string(indices.size())
                + " indices are past the " + std::to_string(sVertexBlock) + " and " + std::to_string(sIndexBlock)
                + " one block of the shared buffers holds" };

        return {};
    }

    Index MeshTable::add(DeformerTable& deformers, const MeshArrays& arrays, FoldedShape shape, Index deformer)
    {
        assert(checkFits(arrays).isOk() && "a mesh past a block, which its reader was to refuse");

        const std::span<const osg::Vec3f> positions = arrays.mPositions;
        const std::span<const std::uint32_t> indices = arrays.mIndices;

        assert(!positions.empty());
        assert(arrays.mNormals.empty() || arrays.mNormals.size() == positions.size());
        assert(arrays.mTexCoords.empty() || arrays.mTexCoords.size() == positions.size());
        assert(arrays.mSecondTexCoords.empty() || arrays.mSecondTexCoords.size() == positions.size());
        assert((arrays.mUnitStreams == 0 || !arrays.mSecondTexCoords.empty())
            && "a unit reads a second set the mesh did not bring");
        assert(arrays.mColours.empty() || arrays.mColours.size() == positions.size());
        assert(arrays.mTangents.empty() || arrays.mTangents.size() == positions.size());
        assert(std::ranges::all_of(positions, [](const osg::Vec3f& position) { return isFinite(position); })
            && "a vertex that is not finite, which the reader refuses");
        assert(std::ranges::all_of(arrays.mColours, [](const osg::Vec3f& colour) { return isFinite(colour); })
            && "a colour that is not finite, which the reader reads as white");
        assert(std::ranges::all_of(arrays.mNormals, [](const osg::Vec3f& normal) { return isFinite(normal); })
            && "a normal that is not finite, which the reader reads as none");
        assert(std::ranges::all_of(arrays.mTangents, [](const osg::Vec4f& tangent) { return isFinite(tangent); })
            && "a tangent that is not finite, which the reader reads as none");
        assert(std::ranges::all_of(arrays.mTexCoords, [](const osg::Vec2f& coord) { return isFinite(coord); })
            && std::ranges::all_of(arrays.mSecondTexCoords, [](const osg::Vec2f& coord) { return isFinite(coord); })
            && "a texture coordinate that is not finite, which the reader reads as none");
        assert(indices.size() % 3 == 0);
        assert(std::all_of(indices.begin(), indices.end(), [&](std::uint32_t i) { return i < positions.size(); }));
        assert(deformer == sNoIndex
            || (deformer < deformers.getRows().size() && deformers.getRows()[deformer].mKind != Deform::None
                && deformers.getRows()[deformer].getVertexCount() == positions.size()
                && "a deformer moves exactly the vertices of the mesh on it"));

        ++mRevision;

        const Run vertices = mVertexRuns.allocate(static_cast<std::uint32_t>(positions.size()));

        // **One run of indices for every mesh that shares them**, held by a count and filed under their digest: a land
        // cell's ground carried its own copy of one triangulation, 96 KiB a cell on the host and as much on the device.
        std::optional<MeshKey> sharedKey;
        Run elements;
        bool bringsIndices = true;
        if (arrays.mSharedIndices)
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
        const Run second = arrays.mSecondTexCoords.empty() ? Run{} : put(mSecondRuns, arrays.mSecondTexCoords);

        MeshRange range{
            .mVertices = vertices,
            .mIndices = elements,
            .mSharedIndexKey = sharedKey,
            .mSecondTexCoords = second,
            .mUnitStreams = arrays.mSecondTexCoords.empty() ? 0u : arrays.mUnitStreams,
            .mShape = shape,
            .mDeformer = deformer,
            .mBounds = boundsOf(positions),
            .mKey = std::nullopt,
        };

        deformers.stand(range);
        mTriangles += range.getTriangleCount();

        const Index index = takeRow(range);
        writeArrival(index, arrays, bringsIndices);
        note(index, SlotNews::Arrived);
        return index;
    }

    void MeshTable::note(Index slot, SlotNews what)
    {
        // Grown here rather than beside every push, so everything keyed on a mesh slot reaches the
        // table's size in one place. A resize to the size it already is does not allocate, which is
        // what the frame path pays.
        mChanges.grow(size());
        mDeformed.grow(size());
        mChanges.note(slot, what);
    }

    void MeshTable::writeArrival(const Index mesh, const MeshArrays& arrays, const bool bringsIndices)
    {
        MeshRange& range = rowAt(mesh);
        const MeshArrivals::Filling into = mArrivals.add(mesh,
            MeshArrivals::Lengths{ .mVertices = range.mVertices.mCount,
                .mSecondTexCoords = range.mSecondTexCoords.mCount,
                .mIndices = bringsIndices ? range.mIndices.mCount : 0u });

        // Filled where the mesh brought none, as the device's run still holds its last tenant's. A zero normal says
        // "use the triangle's plane" and a white colour says "no tint", because one is read and the other is
        // multiplied. The normal is packed to a word (`Shaders::packNormal`) and the colour to halves (`packColour`)
        // as they are copied: once a mesh, and never a frame.
        const auto fill = [](const auto& into, const auto& brought, const auto& nothing, const auto& stored) {
            if (brought.empty())
                std::ranges::fill(into, nothing);
            else
                std::ranges::transform(brought, into.begin(), stored);
        };
        const auto asItIs = [](const auto& value) { return value; };

        fill(into.mPositions, arrays.mPositions, osg::Vec3f(), asItIs);
        fill(into.mTexCoords, arrays.mTexCoords, osg::Vec2f(), asItIs);
        fill(into.mNormals, arrays.mNormals, 0u, [](const osg::Vec3f& normal) { return Shaders::packNormal(normal); });
        fill(into.mColours, arrays.mColours, packColour(osg::Vec3f(1.0f, 1.0f, 1.0f)), packColour);
        std::ranges::copy(arrays.mSecondTexCoords, into.mSecondTexCoords.begin());
        if (bringsIndices)
            std::ranges::copy(arrays.mIndices, into.mIndices.begin());

        // Packed where they are copied, and nought — no tangent — where the mesh brought none. The mesh has tangents
        // where any word is not nought, and not wherever it brought an array: a generator that found no texture
        // coordinates to build from leaves tangents of no length, which pack to none.
        std::uint32_t any = 0;
        if (arrays.mTangents.empty())
            std::ranges::fill(into.mTangents, 0u);
        else
            std::ranges::transform(arrays.mTangents, into.mTangents.begin(), [&](const osg::Vec4f& tangent) {
                const std::uint32_t word = packTangent(tangent);
                any |= word;
                return word;
            });
        range.mTangents = any != 0;

        if (mKeepsDigests)
        {
            MeshGeometry whole = mArrivals.of(mesh);
            whole.mIndices = arrays.mIndices;
            mDigests.resize(size());
            mDigests[mesh] = digestMesh(whole);
        }
    }

    void MeshTable::keepDigests()
    {
        assert(size() == 0 && "digests asked of a table that holds meshes without one");
        mKeepsDigests = true;
    }

    const MeshDigest& MeshTable::getDigest(const Index mesh) const
    {
        assert(mKeepsDigests && "a digest asked of a table that keeps none");
        assert((isLive(mesh) || rowAt(mesh).mRest.mResting) && "the digest of a mesh the table does not hold");
        return mDigests[mesh];
    }

    void MeshTable::notePosed(Index mesh, const osg::BoundingBoxf& bounds)
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

    Index MeshTable::add(
        DeformerTable& deformers, const MeshArrays& arrays, const FoldedShape shape, const MeshKey& key)
    {
        assert(!mKeyIndex.contains(key) && "a key filed twice, which `find` answers first");
        const Index mesh = add(deformers, arrays, shape, sNoIndex);
        rowAt(mesh).mKey = key;
        mKeyIndex.emplace(key, mesh);
        return mesh;
    }

    Index MeshTable::find(const MeshKey& key)
    {
        const auto known = mKeyIndex.find(key);
        if (known == mKeyIndex.end())
            return sNoIndex;

        if (rowAt(known->second).mRest.mResting)
            wake(known->second);
        return known->second;
    }

    void MeshTable::stand(const Index mesh, const std::uint64_t structureBytes)
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

    void MeshTable::fit(const Index mesh, const std::uint64_t bytes)
    {
        assert((isLive(mesh) || rowAt(mesh).mRest.mResting) && "a mesh the table does not hold fitted");
        mResting.fit(mesh, bytes, placeOf());
    }

    bool MeshTable::releaseFitting(DeformerTable& deformers, const std::uint64_t hole)
    {
        settleResting();
        const Index fitting = mResting.firstFitting(hole, placeOf());
        if (fitting == sNoIndex)
            return false;
        release(deformers, fitting);
        return true;
    }

    void MeshTable::drop(DeformerTable& deformers, const Index mesh)
    {
        if (!dropRow(mesh))
            return;

        // **A static mesh the device stands whole rests** (`docs/architecture.md`, "One residency policy for the
        // content"); anything else goes now, a mesh the device has not stood among them.
        const MeshRange& range = rowAt(mesh);
        if (range.mKey.has_value() && range.mStoodBytes > 0)
            rest(mesh);
        else
            release(deformers, mesh);
    }

    void MeshTable::rest(const Index mesh)
    {
        const MeshRange& range = rowAt(mesh);
        mResting.rest(mesh, range.mStoodBytes, placeOf());
        mRestingGeometry.mVertices += range.mVertices.mCount;
        mRestingGeometry.mIndices += range.mIndices.mCount;
    }

    void MeshTable::wake(const Index mesh)
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
        assert(mResting.first() != sNoIndex && "the oldest freed where nothing rests");
        release(deformers, mResting.first());
    }

    void MeshTable::release(DeformerTable& deformers, const Index mesh)
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
