#include "devicescene.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>

#include "placing.hpp"
#include "scenepasses.hpp"
#include "skinpass.hpp"
#include "whenroomless.hpp"

namespace Rtx
{
    namespace
    {
        /// The rows both halves are built from, made before either: `makeInstanceRecords` fills a
        /// vector, and a member is initialised from a value.
        std::vector<InstanceRecord> recordsOf(const SceneDesc& scene)
        {
            std::vector<InstanceRecord> records;
            makeInstanceRecords(scene, records);
            return records;
        }

        std::vector<Index> everyMeshOf(const SceneDesc& scene)
        {
            std::vector<Index> every;
            everyIndexBelow(scene.meshes().getRows().size(), every);
            return every;
        }
    }

    DeviceScene::DeviceScene(const Device& device, Batch& batch, const ScenePasses& passes, TextureArrival& arrival,
        const SceneDesc& scene, std::span<const TextureData> textures, const std::uint32_t anisotropy,
        const SceneRoom& room, const BuildCompaction compaction)
        : mPasses(passes)
        , mRecords(recordsOf(scene))
        , mEveryMesh(everyMeshOf(scene))
        , mAcceleration(device, batch, scene, mEveryMesh, sFrameSlots, room.mPlacements)
        , mBuffers(device, batch, scene, mEveryMesh, mRecords, sFrameSlots, room)
        , mSkinTables(device, batch, scene, mEveryMesh, sFrameSlots, room)
        , mTextures(device, batch, passes.mTextureLayout, passes.mTextures, arrival,
              static_cast<std::uint32_t>(scene.textures().getRows().size()), anisotropy)
    {
        // Posed before it is built: the structures are built over the first copy of the positions
        // and a skinned body's bind pose is not where the body is. Into every other copy as well,
        // which the trace reads a body's motion against, or every body would be thrown from its bind
        // pose on the first frame traced off the first copy. Every body owed, not the walk's deformed
        // ones alone: the walk names a pose only where it changed, so a scene built again while its
        // bodies stand still would name none.
        Uploads uploads(batch);
        mAcceleration.getPoses().write(mEveryMesh);
        for (std::uint32_t copy = 0; copy < mAcceleration.getPoses().count(); ++copy)
            mPasses.mSkin.record(batch.getCommands(), skinning(scene, FrameSlot{ copy }), uploads);
        mAcceleration.build(batch, uploads, scene, mEveryMesh, mRecords, mBuffers.getPositions(), mPasses.mTopLevelPack,
            compaction, mAnswer.mRefused);

        // **Waited on once more where the structures settled**: the last run's loose block went back
        // to the storage under the copies' submit, and its memory is buried under the next, which
        // this is. The textures below are then held to what the tight structures leave, and not to
        // a loose block nothing holds any more.
        if (compaction == BuildCompaction::Settled)
            batch.flush();
        mTextures.write(batch, mPasses.mTextures, arrival, textures, scene.textures(), mAnswer,
            whenRoomless(device, scene), nullptr);
        mBuiltMeshes = scene.meshes().getRevision();
        mReleasedMeshes = scene.meshes().getFreedCount();
        mReleasedTextures = scene.textures().getFreedCount();
        mBuiltStructure = scene.getStructureRevision();
        mBuiltFrom = scene.getIdentity();
        mCounts = scene.placements().getCounts();

        // The first copy's set, which the first frame binds before any placement pays it.
        mTextures.sync(FrameSlot{}, uploads);

        // And the ground that arrived flattened, off that copy: a scene built from nothing is
        // traced before any placement, and a composite stood empty is undefined until baked.
        bakeGround(batch.getCommands(), FrameSlot{}, nullptr, uploads);
    }

    void DeviceScene::releaseFreed(const SceneDesc& scene)
    {
        if (const std::uint64_t freed = scene.meshes().getFreedCount(); freed != mReleasedMeshes)
        {
            mAcceleration.release(scene.meshes().getFreed());
            mReleasedMeshes = freed;
        }

        // The slots keep their place, as they do in the scene's own table, and no live material names a freed one.
        if (const std::uint64_t freed = scene.textures().getFreedCount(); freed != mReleasedTextures)
        {
            mTextures.drop(scene.textures().getFreed());
            mReleasedTextures = freed;
        }
    }

    Skinning DeviceScene::skinning(const SceneDesc& scene, const FrameSlot slot, GpuTimer* const timer)
    {
        return Skinning{
            .mScene = scene,
            .mSlot = slot,
            .mTables = mSkinTables,
            .mPoses = mAcceleration.getPoses(),
            .mNormals = mBuffers.getPosedNormals(),
            .mTangents = mBuffers.getPosedTangents(),
            .mTimer = timer,
        };
    }

    void DeviceScene::describeTables(const FrameSlot slot, Shaders::GpuTables& tables) const
    {
        mBuffers.describeTables(slot, tables);
        tables.mIndexBlocks = mAcceleration.getIndexBlocks();
        tables.mPoseBlocks = mAcceleration.getPoseBlocks(slot);
        tables.mPreviousPoseBlocks = mAcceleration.getPreviousPoseBlocks(slot);
        tables.mTextureTexels = mTextures.getTexelsAddress(slot);
    }

    bool DeviceScene::bakeGround(
        const VkCommandBuffer commands, const FrameSlot slot, GpuTimer* const timer, Uploads& uploads)
    {
        // The tables the bake reads, written ahead of it.
        uploads.order();

        Shaders::GpuTables tables{};
        describeTables(slot, tables);
        return mTextures.bakeComposites(commands, mPasses.mGround, slot, tables, timer);
    }

    void DeviceScene::defragment(Batch& batch, TextureArrival& arrival, GpuTimer* const timer)
    {
        mTextures.defragment(batch, arrival, mPasses.mTextures, timer, TextureArray::DefragmentAsk{});
    }

    void DeviceScene::makeTextureRoom(Batch& batch, TextureArrival& arrival, const VkDeviceSize bytes)
    {
        mTextures.makeRoom(arrival, TextureArray::RoomAsk{ .mBytes = bytes, .mKeep = {}, .mCopyMost = bytes },
            TextureArray::noWants(), mAnswer);
        arrival.record(batch, mPasses.mTextures, nullptr);
    }

    void DeviceScene::extend(Batch& batch, TextureArrival& arrival, const SceneDesc& scene,
        std::span<const TextureData> arrived, const WhenRoomless whenRoomless, GpuTimer* const timer)
    {
        assert(scene.getIdentity() == mBuiltFrom && "an extension of a scene this slot was not built from");

        releaseFreed(scene);

        // **The buffers stage both kinds of arrival**: a material's layer and mask runs arrive without a mesh where
        // the mesh it stands on is one the scene holds already — a cell's flat ground, which one geometry is one mesh
        // of (`SceneDesc::addMesh`).
        const bool meshesArrived = scene.meshes().getRevision() != mBuiltMeshes;
        if (meshesArrived || scene.materials().getRunRevision() != mBuiltStructure.mMaterialRuns)
            mBuffers.extend(batch, scene);
        if (meshesArrived)
        {
            mSkinTables.extend(batch, scene);
            mAcceleration.extend(batch, scene);

            // Posed before it is built, as the constructor does, into every copy, the first of which
            // the build reads — and only the meshes that arrived, over the rows `SkinTables::extend`
            // staged. `SkinPass::recordArrived` says why it may not be every mesh the copy owes.
            mPasses.mSkin.recordArrived(batch.getCommands(), skinning(scene, FrameSlot{}), scene.meshes().getArrived());
            mAcceleration.buildArrived(batch, scene, mBuffers.getPositions(), timer, mAnswer.mRefused);
            mBuiltMeshes = scene.meshes().getRevision();
        }

        mTextures.write(batch, mPasses.mTextures, arrival, arrived, scene.textures(), mAnswer, whenRoomless, timer);

        mBuiltStructure = scene.getStructureRevision();
    }

    bool DeviceScene::place(const SceneDesc& scene, const Placing& placing)
    {
        assert(scene.getIdentity() == mBuiltFrom && "a placement of a scene this slot was not built from");

        // What the scene let go of, given back here too: walking away from a ring frees its meshes
        // and nothing arrives to take them over until the next ring, so a frame that only places
        // is the one that must not hold their structures.
        releaseFreed(scene);

        // Once, for the slots that changed, and both halves read it: a nine-by-nine exterior is
        // fifty thousand rows with a matrix inverse apiece, and a frame changes a hundred.
        updateInstanceRecords(scene, mRecords, mChangedRecords);

        // The descriptors this copy's set owes, now that nothing on the queue reads it.
        mTextures.sync(placing.mSlot, placing.mUploads);

        // The pose first, because the refit reads it. Every skinned body and morphed face this
        // copy owes is computed into it here, and the barrier the pass ends in is what the refit
        // and the trace wait on.
        const bool posed
            = mPasses.mSkin.record(placing.mCommands, skinning(scene, placing.mSlot, placing.mTimer), placing.mUploads);

        // What waited for room, built where the room is back, ahead of the refit and the top level that name it. The
        // memory a burial gives back comes back a frame or two after the textures that rested went, so every
        // placement asks again until it comes or is no longer coming.
        mAcceleration.buildWaiting(placing.mWritten, scene, mBuffers.getPositions(), placing.mSlot, mAnswer.mRefused);

        const bool built = mAcceleration.place(scene, mRecords, mChangedRecords, mPasses.mTopLevelPack, placing);

        // Nothing to report, because nothing here is recorded into the placement: the tables are
        // host-written, and the uploads' order ahead of the trace makes them visible to it. Only
        // what a moving world changed — rebuilding all of it is tens of milliseconds on a
        // nine-by-nine region.
        mBuffers.place(scene, mRecords, mChangedRecords, placing.mSlot, placing.mUploads);

        // The ground that arrived flattened here, after the tables its stack is in are written
        // and the set its layers are in is synced: the trace behind this samples it as a file.
        const bool baked = bakeGround(placing.mCommands, placing.mSlot, placing.mTimer, placing.mUploads);

        mCounts = scene.placements().getCounts();

        return posed || built || baked;
    }

    void DeviceScene::takeAnswer(DeviceAnswer& into)
    {
        into.mRefused.insert(into.mRefused.end(), mAnswer.mRefused.begin(), mAnswer.mRefused.end());
        into.mShort.insert(into.mShort.end(), mAnswer.mShort.begin(), mAnswer.mShort.end());
        into.mWants.insert(into.mWants.end(), mAnswer.mWants.begin(), mAnswer.mWants.end());
        into.mTextureFits.insert(into.mTextureFits.end(), mAnswer.mTextureFits.begin(), mAnswer.mTextureFits.end());
        mAnswer.clear();
        mAcceleration.takeAnswer(into);
    }

    void DeviceScene::measureStars(Shaders::StarField& stars) const
    {
        stars.mExtent
            = stars.mTexture == Shaders::NO_TEXTURE ? Shaders::uvec2(0, 0) : mTextures.getExtent(stars.mTexture);
    }

    void DeviceScene::finishReads(const FrameSlot slot) const
    {
        mBuffers.finishReads(slot);
        mAcceleration.finishReads(slot);
        mSkinTables.finishReads(slot);
        mTextures.finishReads(slot);
    }

    SceneHeld DeviceScene::describe() const
    {
        return SceneHeld{
            .mIdentity = mBuiltFrom,
            .mStructureRevision = mBuiltStructure,
        };
    }

    void DeviceScene::readStats(SceneStats& stats) const
    {
        stats.mInstances = mCounts;
        stats.mInstanceSlots = mAcceleration.getInstanceSlots();
        stats.mTableBytes = mBuffers.getBytes() + mSkinTables.getBytes();
        stats.mGeometryBytes = mBuffers.getGeometryBytes() + mAcceleration.getIndexBytes();
        stats.mWorkBytes = mAcceleration.getWorkBytes();

        // Read every placement and not with the rest of the report, because a placement is
        // where the answer lands: the queries a build wrote are read some placements later, so a
        // pair read at the build would be the nought that stands between the question and its
        // answer. `BottomLevelStore::getCompactableBytes` says why it is not asked for sooner.
        stats.mCompactableBytes = mAcceleration.getCompactableBytes();
        stats.mCompactableNowBytes = mAcceleration.getCompactableNowBytes();
        stats.mRebuilt = mAcceleration.getRebuildCount();

        stats.mStructureBytes = mAcceleration.getStructureBytes();
        stats.mStructureLiveBytes = mAcceleration.getStructureLiveBytes();

        const TexturesHeld& textures = mTextures.getHeld();
        stats.mTextureCount = textures.mCount;
        stats.mTextureBytes = textures.mBytes + textures.mCanvasBytes;
        stats.mTextureBytesBySource = textures.mBySource;
        stats.mTextureBytesBySource.mGroundComposites += textures.mCanvasBytes;
        stats.mReducedTextureCount = textures.mReduced;
    }
}
