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

namespace Rtx
{
    namespace
    {
        /// The rows both halves are built from, made before either: `makeInstanceRecords` fills a
        /// vector, and a member is initialised from a value.
        RowVector<InstanceRecord, PlacementIndex> recordsOf(const SceneDesc& scene)
        {
            RowVector<InstanceRecord, PlacementIndex> records;
            makeInstanceRecords(scene, records);
            return records;
        }

        std::vector<MeshIndex> everyMeshOf(const SceneDesc& scene)
        {
            std::vector<MeshIndex> every;
            everyIndexBelow(scene.meshes().getRows().size(), every);
            return every;
        }
    }

    DeviceScene::DeviceScene(const Device& device, Batch& batch, const ScenePasses& passes, TextureArrival& arrival,
        const SceneDesc& scene, const std::uint32_t anisotropy, const SceneRoom& room, const BuildCompaction compaction)
        : mPasses(passes)
        , mRecords(recordsOf(scene))
        , mEveryMesh(everyMeshOf(scene))
        , mAcceleration(device, batch, scene, mEveryMesh, sFrameSlots, room.mPlacements)
        , mBuffers(device, batch, scene, mEveryMesh, mRecords.rows(), sFrameSlots, room)
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
        mAcceleration.build(batch, uploads, scene, mEveryMesh, mRecords.rows(), mBuffers.getPositions(),
            mPasses.mTopLevelPack, compaction, mAnswer.mRefused);

        // **Waited on once more where the structures settled**: the last run's loose block went back to the storage
        // under the copies' submit, and its memory is buried under the next, which this is.
        if (compaction == BuildCompaction::Settled)
            batch.flush();

        // Every texture the table holds draws the stand-in until residency stands it.
        standInTaken(scene);
        mBuiltMeshes = scene.meshes().getRevision();
        mReleasedMeshes = scene.meshes().getFreedCount();
        mBuiltStructure = scene.getStructureRevision();
        mBuiltFrom = scene.getIdentity();
        mCounts = scene.placements().getCounts();

        // The first copy's set, which the first frame binds before any placement pays it.
        mTextures.sync(FrameSlot{}, uploads);

        // And the ground that arrived flattened, off that copy: a scene built from nothing is
        // traced before any placement, and a composite stood empty is undefined until baked.
        bakeGround(batch.getCommands(), FrameSlot{}, nullptr, uploads);
    }

    void DeviceScene::standInTaken(const SceneDesc& scene)
    {
        // By the table's revision and not its arrivals, which a caller that has not compacted cannot read: a walk of
        // the rows on a hand-over a slot was taken in, which every slot standing already passes over.
        if (scene.textures().getRevision() == mStoodInRevision)
            return;
        mStoodInRevision = scene.textures().getRevision();
        mEveryTexture.clear();
        for (std::uint32_t at = 0; at < scene.textures().getRows().size(); ++at)
            if (scene.textures().isLive(TextureIndex(at)))
                mEveryTexture.push_back(TextureIndex(at));
        mTextures.standIn(mEveryTexture);
    }

    void DeviceScene::releaseFreed(const SceneDesc& scene)
    {
        if (const std::uint64_t freed = scene.meshes().getFreedCount(); freed != mReleasedMeshes)
        {
            mAcceleration.release(scene.meshes().getFreed());
            mReleasedMeshes = freed;
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

    void DeviceScene::extend(const Extending& extending, const SceneDesc& scene, std::span<const ResidencyOp> ops)
    {
        assert(scene.getIdentity() == mBuiltFrom && "an extension of a scene this slot was not built from");

        releaseFreed(scene);

        // **The buffers stage both kinds of arrival**: a material's layer and mask runs arrive without a mesh where
        // the mesh it stands on is one the scene holds already — a cell's flat ground, which one geometry is one mesh
        // of (`SceneDesc::addMesh`).
        Batch& batch = extending.mBatch;
        GpuTimer* const timer = extending.mTimer;
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

        // Residency's operations, and then every slot the table took since that stands nothing yet.
        mTextures.execute(TextureArray::Executing{ .mBatch = batch,
                              .mPasses = mPasses.mTextures,
                              .mArrival = extending.mArrival,
                              .mArena = extending.mArena,
                              .mTimer = timer },
            ops);
        standInTaken(scene);

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

        const bool built = mAcceleration.place(scene, mRecords.rows(), mChangedRecords, mPasses.mTopLevelPack, placing);

        // Nothing to report, because nothing here is recorded into the placement: the tables are
        // host-written, and the uploads' order ahead of the trace makes them visible to it. Only
        // what a moving world changed — rebuilding all of it is tens of milliseconds on a
        // nine-by-nine region.
        mBuffers.place(scene, mRecords.rows(), mChangedRecords, placing.mSlot, placing.mUploads);

        // The ground that arrived flattened here, after the tables its stack is in are written
        // and the set its layers are in is synced: the trace behind this samples it as a file.
        const bool baked = bakeGround(placing.mCommands, placing.mSlot, placing.mTimer, placing.mUploads);

        mCounts = scene.placements().getCounts();

        return posed || built || baked;
    }

    void DeviceScene::takeAnswer(DeviceAnswer& into)
    {
        mAnswer.handTo(into);
        mAcceleration.takeAnswer(into);
    }

    void DeviceScene::measureStars(Shaders::StarField& stars) const
    {
        stars.mExtent = stars.mTexture == Shaders::NO_TEXTURE ? Shaders::uvec2(0, 0)
                                                              : mTextures.getExtent(TextureIndex(stars.mTexture));
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
        stats.mTightness = mAcceleration.getTightness();
        stats.mStructureMovedBytes = mAcceleration.getMovedOutBytes();
        stats.mRebuilt = mAcceleration.getRebuildCount();

        stats.mStructureBytes = mAcceleration.getStructureBytes();
        stats.mStructureLiveBytes = mAcceleration.getStructureLiveBytes();

        const TexturesHeld& textures = mTextures.getHeld();
        stats.mTextureCount = textures.mCount;
        stats.mTextureBytes = textures.mBytes + textures.mCanvasBytes;
        stats.mTextureBytesBySource = textures.mBySource;
        stats.mTextureBytesBySource.mGroundComposites += textures.mCanvasBytes;
    }
}
