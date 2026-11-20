#include "bottomlevelstore.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>
#include <variant>

#include <volk.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    namespace
    {
        /// After one run of builds, and after one copy: the structures for whatever reads them, as
        /// `barrierAfterBuild` orders them, and the scratch for the next run's build, which writes it again.
        void barrierAfterRun(const VkCommandBuffer commands)
        {
            handOver(commands,
                BufferUse{ VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
                        | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR },
                BufferUse{ VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR
                        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
                        | VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                        | VK_ACCESS_2_TRANSFER_WRITE_BIT });
        }

        /// The triangles of a mesh of `vertices` described for a question of sizes: what the driver sizes a build by
        /// is its counts and formats, which `describeTriangles` states for a mesh with its addresses.
        VkAccelerationStructureGeometryKHR describeCounts(const std::uint32_t vertices)
        {
            MeshRange counted;
            counted.mVertices.mCount = vertices;
            return describeTriangles(counted, 0, 0);
        }
    }

    VkBuildAccelerationStructureFlagsKHR BottomLevelStore::buildFlagsOf(const StructureMotion motion)
    {
        // ALLOW_DATA_ACCESS is what lets a candidate's corners be fetched off the structure
        // (`GL_EXT_ray_tracing_position_fetch`), for the test that takes or passes it.
        constexpr VkBuildAccelerationStructureFlagsKHR every = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
            | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR;

        // A mesh that deforms is refit, and an update has to be asked of the build that allows it. One that does
        // not may be asked what it would come to tight, for a fraction of a per cent against the half it gives
        // back; a refit writes back into the slack, so never both.
        return every
            | (motion == StructureMotion::Refit ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
                                                : VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
    }

    ContentPrice BottomLevelStore::price(const Device& device, const StructureShape& shape)
    {
        assert(shape.mTriangles > 0 && "a structure priced over no triangles, which no structure is made of");
        const VkAccelerationStructureGeometryKHR geometry = describeCounts(shape.mVertices);
        const VkAccelerationStructureBuildGeometryInfoKHR build{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = buildFlagsOf(shape.mRefits ? StructureMotion::Refit : StructureMotion::Still),
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = 1,
            .pGeometries = &geometry,
            .ppGeometries = nullptr,
            .scratchData = {},
        };
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructureSize = 0,
            .updateScratchSize = 0,
            .buildScratchSize = 0,
        };
        vkGetAccelerationStructureBuildSizesKHR(
            device.getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &shape.mTriangles, &sizes);
        return ContentPrice{ .mBytes = sizes.accelerationStructureSize, .mAlignment = sStructureOffsetAlignment };
    }

    VkDeviceSize BottomLevelStore::getRefitScratchBytes() const
    {
        const VkDeviceSize alignment = mDevice.getPhysicalDevice().getStructureScratchAlignment();

        VkDeviceSize updates = 0;
        VkDeviceSize rebuild = 0;
        for (const MeshIndex mesh : mRefittable.getSlots())
        {
            const VkDeviceSize update = alignUp(mRows[mesh].mUpdateScratch, alignment);
            const VkDeviceSize build = alignUp(mRows[mesh].mBuildScratch, alignment);
            updates += update;
            rebuild = std::max(rebuild, build > update ? build - update : 0);
        }
        return updates > 0 ? updates + rebuild : 0;
    }

    MeshIndex BottomLevelStore::pickRebuild(const std::span<const MeshIndex> posed, const std::uint64_t placement)
    {
        MeshIndex picked = sNoRow;
        for (const MeshIndex mesh : posed)
        {
            assert(isUpdatable(mesh) && "a mesh posed that was not built to be refitted");

            const std::uint64_t builtAt = mRows[mesh].mRebuiltAt;
            if (builtAt < placement && (picked == sNoRow || builtAt < mRows[picked].mRebuiltAt))
                picked = mesh;
        }
        if (picked != sNoRow)
            mRows[picked].mRebuiltAt = placement;
        return picked;
    }

    BottomLevelStore::BottomLevelStore(const Device& device, const VkDeviceSize buildRoom)
        : mDevice(device)
        , mBuildRoom(buildRoom)
        , mScratch(device, BufferKind::DeviceLocal, sScratchUsage, "build scratch", GrowableBuffer::Growth::Exact)
    {
    }

    void BottomLevelStore::grow(const std::size_t meshes)
    {
        assert(meshes >= mRows.size() && "the scene's mesh table shrank under the structures");
        mRows.resize(meshes);
        mRefittable.grow(meshes);
    }

    void BottomLevelStore::retire(const MeshIndex slot)
    {
        Row& row = mRows[slot];
        if (row.mStructure.isEmpty())
            return;

        if (row.mCompaction == Compaction::Answered && shrinks(row))
        {
            mCompactableNow -= row.mSize;
            mCompactableTight -= row.mTightSize;
        }
        assert(mLiveBytes >= row.mSize && "a structure retired past what stands");
        mLiveBytes -= row.mSize;

        // Buried under the next submit, which covers whatever earlier frame still traces it through the top level it
        // was named in; its slot is residency's to hand out again once those frames are done.
        const std::uint64_t rebuiltAt = row.mRebuiltAt;
        row = Row{};
        row.mRebuiltAt = rebuiltAt;
        if (mRefittable.has(slot))
            mRefittable.remove(slot);
        mChanged.addMakingRoom(slot);
    }

    void BottomLevelStore::release(const std::span<const MeshIndex> meshes)
    {
        for (const MeshIndex mesh : meshes)
            // A slot this never held: a scene can add a mesh and sweep it in the same window, before anything was
            // handed over to build it.
            if (mRows.holds(mesh))
                retire(mesh);
        mRefittable.compact();
    }

    void BottomLevelStore::execute(
        const Executing& executing, const SceneDesc& scene, const std::span<const ResidencyOp> ops)
    {
        const Crash::NoteScope noted("standing bottom-level structures, {} operations", ops.size());

        grow(scene.meshes().getRows().size());

        // **Drops, then copies, then builds**: a copy reads what stood before this hand-over, never what it builds,
        // since residency moves and copies only what stood; a build lands in a slot no copy reads.
        mBuilds.clear();
        bool copied = false;
        for (const ResidencyOp& op : ops)
        {
            if (const auto* dropped = std::get_if<DropStructure>(&op))
                drop(*dropped);
            else if (const auto* built = std::get_if<BuildStructure>(&op))
                mBuilds.push_back(*built);
        }
        for (const ResidencyOp& op : ops)
            if (const auto* copying = std::get_if<CopyStructure>(&op))
            {
                // A barrier between two copies, since a structure moved out of a draining slab may be the one its
                // compaction reads in the same hand-over.
                if (std::exchange(copied, true))
                    barrierAfterRun(executing.mBatch.getCommands());
                copy(executing.mBatch.getCommands(), executing.mArena, *copying);
            }
        if (copied)
            barrierAfterRun(executing.mBatch.getCommands());
        mRefittable.compact();

        if (!mBuilds.empty())
            build(executing, scene);
        askWhatCompactionWouldSave(executing.mBatch.getCommands());
    }

    void BottomLevelStore::drop(const DropStructure& op)
    {
        // A mesh the scene freed went with its release already, and its slot may stand another tenant's since.
        if (mRows.holds(op.mMesh) && !mRows[op.mMesh].mStructure.isEmpty() && mRows[op.mMesh].mSlot == op.mFrom)
            retire(op.mMesh);
    }

    void BottomLevelStore::copy(const VkCommandBuffer commands, const ContentArena& arena, const CopyStructure& op)
    {
        Row& row = mRows[op.mMesh];
        assert(!row.mStructure.isEmpty() && "a copy of a structure that stands nowhere");
        assert(row.mSlot == op.mFrom && "a copy from a slot the structure does not stand in");
        assert(row.mGeneration == op.mGeneration && "a copy of another tenant's structure");

        const bool compact = op.mKind == StructureCopy::Compact;
        assert((!compact || row.mCompaction == Compaction::Answered) && "made tight before its size was answered");
        const VkDeviceSize size = compact && row.mTightSize > 0 ? row.mTightSize : row.mSize;
        AccelerationStructure made
            = AccelerationStructure::bottomLevel(mDevice, arena.getBuffer(op.mTo.mBlock), op.mTo.mOffset, size);
        const VkCopyAccelerationStructureInfoKHR info{
            .sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
            .pNext = nullptr,
            .src = row.mStructure.getHandle(),
            .dst = made.getHandle(),
            .mode
            = compact ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR,
        };
        vkCmdCopyAccelerationStructureKHR(commands, &info);

        // Replaced though the copy reads it: the old structure buries itself under the submit this is recorded into,
        // so what the burial covers is both this read and whatever earlier frame still traces it.
        if (compact && shrinks(row))
        {
            mCompactableNow -= row.mSize;
            mCompactableTight -= row.mTightSize;
        }
        if (!compact)
            mMovedOut += size;
        mLiveBytes = mLiveBytes - row.mSize + size;
        row.mStructure = std::move(made);
        row.mSlot = op.mTo.mSlot;
        row.mSize = size;
        if (compact)
            row.mCompaction = Compaction::Tight;
        mChanged.addMakingRoom(op.mMesh);
    }

    void BottomLevelStore::build(const Executing& executing, const SceneDesc& scene)
    {
        const GpuZone timed(executing.mTimer, executing.mBatch.getCommands(), FrameZone::Blas);
        const VkDeviceSize scratchAlignment = mDevice.getPhysicalDevice().getStructureScratchAlignment();
        mBuild.sizeTo(mBuilds.size());
        mBuilding.assign(mBuilds.size(), BuildRow{});

        for (std::size_t at = 0; at < mBuilds.size(); ++at)
        {
            const BuildStructure& op = mBuilds[at];
            const MeshRange& mesh = scene.meshes().getRows()[op.mMesh];
            assert(mesh.getTriangleCount() > 0 && "a structure built over no triangles");

            // A slot built again arrives holding different geometry: whatever stood there is buried first.
            retire(op.mMesh);

            // A deforming mesh is built over the pose `SkinPass` wrote into the first copy, a static one over the
            // positions a hit reads its corners from, so structure and hit hold the same numbers. A run never
            // straddles a block, so its first vertex's address covers it. Indices are mesh-local, so each structure
            // is handed its slice of the shared buffers and addresses vertex zero as its own first vertex.
            const VkDeviceAddress vertices = mesh.deforms() ? executing.mPoses.addressOf(mesh.mBindOffset)
                                                            : executing.mPositions.addressOf(mesh.mVertices.mOffset);
            mBuild.mGeometries[at]
                = describeTriangles(mesh, vertices, executing.mIndices.addressOf(mesh.mIndices.mOffset));
            mBuild.mBuilds[at] = VkAccelerationStructureBuildGeometryInfoKHR{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .pNext = nullptr,
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .flags = buildFlagsOf(mesh.deforms() ? StructureMotion::Refit : StructureMotion::Still),
                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .srcAccelerationStructure = VK_NULL_HANDLE,
                .dstAccelerationStructure = VK_NULL_HANDLE,
                .geometryCount = 1,
                .pGeometries = &mBuild.mGeometries[at],
                .ppGeometries = nullptr,
                .scratchData = {},
            };
            const std::uint32_t triangles = mesh.getTriangleCount();
            VkAccelerationStructureBuildSizesInfoKHR sizes{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
                .pNext = nullptr,
                .accelerationStructureSize = 0,
                .updateScratchSize = 0,
                .buildScratchSize = 0,
            };
            vkGetAccelerationStructureBuildSizesKHR(mDevice.getHandle(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &mBuild.mBuilds[at], &triangles, &sizes);
            assert(sizes.accelerationStructureSize <= op.mBytes && "a structure larger than residency priced it at");
            mBuilding[at].mSize = sizes.accelerationStructureSize;
            mBuilding[at].mScratch = alignUp(sizes.buildScratchSize, scratchAlignment);
            mBuild.mRanges[at] = VkAccelerationStructureBuildRangeInfoKHR{
                .primitiveCount = triangles, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0
            };

            // Kept so a refit of this one mesh asks the driver nothing again: the same geometry describes it. The
            // build's own scratch beside it, for the rota that builds a refitted structure whole again.
            Row& row = mRows[op.mMesh];
            row.mUpdatable = mesh.deforms();
            row.mUpdateScratch = row.mUpdatable ? sizes.updateScratchSize : 0;
            row.mBuildScratch = sizes.buildScratchSize;
            row.mRebuiltAt = executing.mPlacement;
        }

        // **In runs whose scratch fits the build room**, each built over the room the run before used: one run over
        // every mesh held the sum of their scratch as essential memory. A mesh larger than the room is a run of its
        // own, and the room is then what it needs.
        const VkDeviceSize mostScratch = splitIntoRuns(
            mBuilds.size(), mBuildRoom, [&](const std::size_t at) { return std::optional(mBuilding[at].mScratch); },
            [&](const std::size_t at, const std::uint64_t offset) { mBuilding[at].mScratchOffset = offset; }, mRunEnds);
        mScratch.growTo(mostScratch);
        const VkDeviceAddress scratchAddress = mScratch.get().addressFor();

        const VkCommandBuffer commands = executing.mBatch.getCommands();
        std::size_t begin = 0;
        for (const std::size_t end : mRunEnds)
        {
            mLiveBuilds.clear();
            mBuild.mRangePointers.clear();
            for (std::size_t at = begin; at < end; ++at)
            {
                const BuildStructure& op = mBuilds[at];
                Row& row = mRows[op.mMesh];
                row.mStructure = AccelerationStructure::bottomLevel(
                    mDevice, executing.mArena.getBuffer(op.mLoose.mBlock), op.mLoose.mOffset, mBuilding[at].mSize);
                row.mSlot = op.mLoose.mSlot;
                row.mGeneration = op.mGeneration;
                row.mSize = mBuilding[at].mSize;
                row.mTriangles = mBuild.mRanges[at].primitiveCount;
                mLiveBytes += row.mSize;
                mChanged.addMakingRoom(op.mMesh);

                // Built loose, and a mesh that refits keeps its slack: a refit writes back into it.
                if (row.mUpdatable)
                {
                    row.mCompaction = Compaction::Kept;
                    mRefittable.add(op.mMesh);
                }
                else
                {
                    row.mCompaction = Compaction::Loose;
                    mLoose.push_back(op.mMesh);
                }

                mBuild.mBuilds[at].dstAccelerationStructure = row.mStructure.getHandle();
                mBuild.mBuilds[at].scratchData.deviceAddress = scratchAddress + mBuilding[at].mScratchOffset;
                mLiveBuilds.push_back(mBuild.mBuilds[at]);
                mBuild.mRangePointers.push_back(&mBuild.mRanges[at]);
            }
            begin = end;

            vkCmdBuildAccelerationStructuresKHR(commands, static_cast<std::uint32_t>(mLiveBuilds.size()),
                mLiveBuilds.data(), mBuild.mRangePointers.data());
            barrierAfterRun(commands);
        }
    }

    void BottomLevelStore::askWhatCompactionWouldSave(const VkCommandBuffer commands)
    {
        if (mLoose.empty())
            return;

        const auto held = static_cast<std::uint32_t>(mRows.size());
        if (held > mCompactablePool)
        {
            // Twice what it held, so a route's arrivals make a pool a logarithmic number of times
            // rather than one per cell.
            const std::uint32_t wanted = std::max(held, 2 * mCompactablePool);

            // The pool this replaces may be named by a batch the queue has not reached, and every
            // answer it was to carry is lost with it: whoever was asked through it is asked again
            // through the new one, below.
            const VkQueryPoolCreateInfo create{
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                .queryCount = wanted,
                .pipelineStatistics = 0,
            };
            mCompactable = QueryPool::make(mDevice, vkCreateQueryPool, create, "vkCreateQueryPool");
            mCompactablePool = wanted;

            for (std::size_t at = 0; at < mAsked.size(); ++at)
                if (isOutstanding(mAsked.at(at)))
                {
                    const MeshIndex slot = mAsked.at(at).mSlot;
                    mRows[slot].mCompaction = Compaction::Loose;
                    mLoose.push_back(slot);
                }
        }

        // One reset and one write per run of consecutive slots, which is what a cell's arrivals are (the scene hands
        // out slots in order). Each query is reset before it is written because the slot may have been asked about
        // before, for a structure that has since gone. The value the batch rides is the pool's next submit: a batch
        // is flushed into it or deferred ahead of it, and nothing else takes a value in between.
        const std::uint64_t rides = mDevice.getTimeline().getNext();
        std::ranges::sort(mLoose);
        std::uint32_t first = 0;
        mAskScratch.clear();
        for (const MeshIndex slot : mLoose)
        {
            // A slot named twice is asked once: the first naming leaves it asked.
            Row& row = mRows[slot];
            if (row.mCompaction != Compaction::Loose)
                continue;

            if (!mAskScratch.empty() && slot.at() != first + mAskScratch.size())
                askRun(commands, first);
            if (mAskScratch.empty())
                first = slot.at();
            mAskScratch.push_back(row.mStructure.getHandle());

            row.mCompaction = Compaction::Asked;
            row.mAskedAt = rides;
            mAsked.push(Ask{ .mSlot = slot, .mAt = rides });
        }
        askRun(commands, first);
        mLoose.clear();
    }

    void BottomLevelStore::askRun(const VkCommandBuffer commands, const std::uint32_t first)
    {
        if (mAskScratch.empty())
            return;

        const auto count = static_cast<std::uint32_t>(mAskScratch.size());
        vkCmdResetQueryPool(commands, mCompactable.get(), first, count);
        vkCmdWriteAccelerationStructuresPropertiesKHR(commands, count, mAskScratch.data(),
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, mCompactable.get(), first);
        mAskScratch.clear();
    }

    void BottomLevelStore::readAnswers(const SceneSlot scene, std::vector<TightSize>& into)
    {
        while (!mAsked.empty())
        {
            const Ask& oldest = mAsked.at(0);

            // The questions are in the order they were asked, so what is ready is a prefix.
            if (!mDevice.getTimeline().hasFinished(oldest.mAt))
                break;

            if (!isOutstanding(oldest))
            {
                mAsked.pop(1);
                continue;
            }

            // The run of consecutive slots asked together, read as one range. Cut where a slot was
            // asked again since, whose query a later batch may still be writing.
            std::size_t count = 1;
            while (count < mAsked.size() && mAsked.at(count).mAt == oldest.mAt
                && mAsked.at(count).mSlot.at() == mAsked.at(count - 1).mSlot.at() + 1
                && isOutstanding(mAsked.at(count)))
                ++count;

            mReadScratch.resize(count);
            const VkResult read = vkGetQueryPoolResults(mDevice.getHandle(), mCompactable.get(), oldest.mSlot.at(),
                static_cast<std::uint32_t>(count), count * sizeof(VkDeviceSize), mReadScratch.data(),
                sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT);

            // Asked again next frame where the answers are simply not there yet. Anything else is a fault, and a
            // lost device shows up here first.
            if (read == VK_NOT_READY)
                break;
            checkVk(mDevice, read, "vkGetQueryPoolResults");

            for (std::size_t at = 0; at < count; ++at)
            {
                const MeshIndex slot = mAsked.at(at).mSlot;
                Row& row = mRows[slot];
                row.mCompaction = Compaction::Answered;
                row.mTightSize = mReadScratch[at];
                mTightness.add(row.mTriangles, row.mSize, row.mTightSize);
                if (shrinks(row))
                {
                    mCompactableNow += row.mSize;
                    mCompactableTight += row.mTightSize;
                }

                // No smaller, or no size said: what it stands as is what it comes to, and the loose slot is where
                // residency keeps it.
                into.push_back(TightSize{ .mScene = scene,
                    .mMesh = slot,
                    .mGeneration = row.mGeneration,
                    .mBytes = shrinks(row) ? row.mTightSize : row.mSize });
            }
            mAsked.pop(count);
        }
        mAsked.settle();
    }
}
