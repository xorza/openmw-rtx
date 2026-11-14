#include "skintables.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/scene/deformertable.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/skinning.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>

namespace Rtx
{
    SkinTables::SkinTables(const Device& device, Batch& batch, const SceneDesc& scene,
        const std::span<const Index> everyMesh, const std::uint32_t slots, const SceneRoom& room)
        : mBindPositions(device, BufferKind::DeviceLocal, sTableKeptUsage, "bind positions")
        , mBindNormals(device, BufferKind::DeviceLocal, sTableKeptUsage, "bind normals")
        , mBindTangents(device, BufferKind::DeviceLocal, sTableKeptUsage, "bind tangents")
        , mRuns(device, BufferKind::DeviceLocal, sTableKeptUsage, "rig runs")
        , mInfluences(device, BufferKind::DeviceLocal, sTableKeptUsage, "rig influences")
        , mMorphOffsets(device, BufferKind::DeviceLocal, sTableKeptUsage, "morph offsets")
        , mPoses([&](FrameSlot) { return GrowableBuffer(device, BufferKind::HostWritten, sTableFilledUsage, "poses"); })
    {
        mPoses.open(slots);

        // Every table exists from here, whether or not anything is written to it: a table made
        // empty is a byte, so a scene with no actor in it still has a buffer at every address a
        // dispatch could be handed. At the room or what the scene reaches, so nothing is copied.
        const SceneRoom reach = reachOf(scene.deformers());
        grow(batch,
            SceneRoom{ .mRigRuns = std::max(room.mRigRuns, reach.mRigRuns),
                .mInfluences = std::max(room.mInfluences, reach.mInfluences),
                .mOffsets = std::max(room.mOffsets, reach.mOffsets),
                .mPoseWords = std::max(room.mPoseWords, reach.mPoseWords),
                .mBindVertices = std::max(room.mBindVertices, reach.mBindVertices) });

        // Whole: the arrivals since the last clear are not every row a world made again holds.
        std::vector<Index> everyDeformer;
        everyIndexBelow(scene.deformers().getRows().size(), everyDeformer);
        write(batch, scene, everyMesh, everyDeformer, scene.meshes().getArrived());
        orderStagedWrites(batch);
    }

    void SkinTables::extend(Batch& batch, const SceneDesc& scene)
    {
        grow(batch, reachOf(scene.deformers()));
        write(batch, scene, scene.meshes().getArrived(), scene.deformers().getArrived(), scene.meshes().getArrived());
        orderStagedWrites(batch);
    }

    SceneRoom SkinTables::reachOf(const DeformerTable& deformers)
    {
        return SceneRoom{ .mRigRuns = static_cast<std::uint32_t>(deformers.getRuns().size()),
            .mInfluences = static_cast<std::uint32_t>(deformers.getInfluences().size()),
            .mOffsets = static_cast<std::uint32_t>(deformers.getMorphOffsets().size()),
            .mPoseWords = static_cast<std::uint32_t>(deformers.getPoses().size()),
            .mBindVertices = deformers.getBindVertexCount() };
    }

    void SkinTables::grow(Batch& batch, const SceneRoom& reach)
    {
        const VkDeviceSize bindVertices = reach.mBindVertices;
        mBindPositions.outgrowKeeping(batch, bindVertices * sizeof(osg::Vec3f));
        mBindNormals.outgrowKeeping(batch, bindVertices * sizeof(std::uint32_t));
        mBindTangents.outgrowKeeping(batch, bindVertices * sizeof(std::uint32_t));
        mRuns.outgrowKeeping(batch, VkDeviceSize{ reach.mRigRuns } * sizeof(std::uint32_t));
        mInfluences.outgrowKeeping(batch, VkDeviceSize{ reach.mInfluences } * sizeof(Shaders::GpuInfluence));
        mMorphOffsets.outgrowKeeping(batch, VkDeviceSize{ reach.mOffsets } * sizeof(osg::Vec3f));

        // Every other pose of a copy reaches it in the placement that dispatches over it, so a pose
        // table is not kept across a growth.
        for (GrowableBuffer& poses : mPoses.live())
            poses.outgrow(VkDeviceSize{ reach.mPoseWords } * sizeof(PoseWord));
    }

    void SkinTables::write(Batch& batch, const SceneDesc& scene, const std::span<const Index> meshes,
        const std::span<const Index> deformers, const std::span<const Index> posed)
    {
        const std::span<const MeshRange> ranges = scene.meshes().getRows();
        for (const Index index : meshes)
        {
            const MeshRange& mesh = ranges[index];
            if (!mesh.deforms() || mesh.mVertices.mCount == 0)
                continue;

            stageInto(batch, mBindPositions.get(), VkDeviceSize{ mesh.mBindOffset } * sizeof(osg::Vec3f),
                std::as_bytes(scene.meshes().getMeshPositions(index)));
            stageInto(batch, mBindNormals.get(), VkDeviceSize{ mesh.mBindOffset } * sizeof(std::uint32_t),
                std::as_bytes(mesh.mVertices.in(scene.meshes().getNormals())));
            stageInto(batch, mBindTangents.get(), VkDeviceSize{ mesh.mBindOffset } * sizeof(std::uint32_t),
                std::as_bytes(mesh.mVertices.in(scene.meshes().getTangents())));
        }

        // A freed slot deforms nothing and holds no run to write.
        const DeformerTable& table = scene.deformers();
        const std::span<const Deformer> rows = table.getRows();
        for (const Index index : deformers)
        {
            const Deformer& deformer = rows[index];
            if (!deformer.mRuns.empty())
                stageInto(batch, mRuns.get(), VkDeviceSize{ deformer.mRuns.mOffset } * sizeof(std::uint32_t),
                    std::as_bytes(deformer.mRuns.in(table.getRuns())));
            if (!deformer.mInfluences.empty())
                stageInto(batch, mInfluences.get(),
                    VkDeviceSize{ deformer.mInfluences.mOffset } * sizeof(Shaders::GpuInfluence),
                    std::as_bytes(deformer.mInfluences.in(table.getInfluences())));
            if (!deformer.mOffsets.empty())
                stageInto(batch, mMorphOffsets.get(), VkDeviceSize{ deformer.mOffsets.mOffset } * sizeof(osg::Vec3f),
                    std::as_bytes(deformer.mOffsets.in(table.getMorphOffsets())));
        }

        // The arrivals' poses into the first copy alone. Every other pose of a copy reaches it in the
        // placement that dispatches over it, and the other copies owe the arrivals theirs.
        for (const Index index : posed)
        {
            const MeshRange& mesh = ranges[index];
            if (!mesh.deforms())
                continue;

            stageInto(batch, mPoses.at(FrameSlot{}).get(), VkDeviceSize{ mesh.mPoseOffset } * sizeof(PoseWord),
                std::as_bytes(scene.getMeshPose(index)));
        }
    }

    void SkinTables::finishReads(const FrameSlot slot) const
    {
        mPoses.at(slot).get().waitIdle("an arrival's pose over the poses a placement writes");
    }

    void SkinTables::writePoses(
        const SceneDesc& scene, const FrameSlot slot, const std::span<const Index> meshes, Uploads& uploads)
    {
        const std::span<const MeshRange> rows = scene.meshes().getRows();
        mPoseRuns.clear();
        for (const Index mesh : meshes)
        {
            const VkDeviceSize at = VkDeviceSize{ rows[mesh].mPoseOffset } * sizeof(PoseWord);
            mPoseRuns.push_back(CopyRun{ .mFrom = at, .mTo = at, .mSize = scene.getMeshPose(mesh).size_bytes() });
        }

        // In the order they stand, so neighbours meet: the meshes come in the order they are posed.
        std::ranges::sort(mPoseRuns, {}, &CopyRun::mFrom);
        mJoinedRuns.clear();
        for (const CopyRun& run : mPoseRuns)
        {
            assert((mJoinedRuns.empty() || run.mFrom >= mJoinedRuns.back().mFrom + mJoinedRuns.back().mSize)
                && "two meshes posed into one pose's words, or one mesh named twice");
            appendRun(mJoinedRuns, run);
        }

        uploads.writeRuns(mPoses.at(slot).get(), std::as_bytes(scene.deformers().getPoses()), mJoinedRuns);
    }

    VkDeviceAddress SkinTables::getPose(const MeshRange& mesh, const FrameSlot slot) const
    {
        const VkDeviceAddress address
            = mPoses.at(slot).get().addressFor() + VkDeviceSize{ mesh.mPoseOffset } * sizeof(PoseWord);
        assert(
            address % Shaders::BONE_ALIGN == 0 && "a run of rows the kernel's reference claims more of than is true");
        return address;
    }

    VkDeviceAddress SkinTables::getBindPositions(const MeshRange& mesh) const
    {
        return mBindPositions.get().addressFor() + VkDeviceSize{ mesh.mBindOffset } * sizeof(osg::Vec3f);
    }

    VkDeviceAddress SkinTables::getBindNormals(const MeshRange& mesh) const
    {
        return mBindNormals.get().addressFor() + VkDeviceSize{ mesh.mBindOffset } * sizeof(std::uint32_t);
    }

    VkDeviceAddress SkinTables::getBindTangents(const MeshRange& mesh) const
    {
        return mBindTangents.get().addressFor() + VkDeviceSize{ mesh.mBindOffset } * sizeof(std::uint32_t);
    }

    VkDeviceAddress SkinTables::getRuns(const Deformer& rig) const
    {
        return mRuns.get().addressFor() + VkDeviceSize{ rig.mRuns.mOffset } * sizeof(std::uint32_t);
    }

    VkDeviceAddress SkinTables::getInfluences(const Deformer& rig) const
    {
        return mInfluences.get().addressFor() + VkDeviceSize{ rig.mInfluences.mOffset } * sizeof(Shaders::GpuInfluence);
    }

    VkDeviceAddress SkinTables::getMorphOffsets(const Deformer& morph) const
    {
        return mMorphOffsets.get().addressFor() + VkDeviceSize{ morph.mOffsets.mOffset } * sizeof(osg::Vec3f);
    }

    VkDeviceSize SkinTables::getBytes() const
    {
        VkDeviceSize total = mBindPositions.get().getSize() + mBindNormals.get().getSize()
            + mBindTangents.get().getSize() + mRuns.get().getSize() + mInfluences.get().getSize()
            + mMorphOffsets.get().getSize();
        for (const GrowableBuffer& poses : mPoses.live())
            total += poses.get().getSize();

        return total;
    }
}
