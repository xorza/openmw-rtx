#include "skintables.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <osg/Vec3f>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/scene/deformertable.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/skinning.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>

namespace Rtx
{
    namespace
    {
        /// Every index below `count`, refilled into `into`.
        std::span<const Index> everyBelow(std::size_t count, std::vector<Index>& into)
        {
            into.resize(count);
            for (std::size_t at = 0; at < count; ++at)
                into[at] = static_cast<Index>(at);

            return into;
        }
    }

    SkinTables::SkinTables(const Device& device, Batch& batch, const SceneDesc& scene, const std::uint32_t slots)
        : mBindPositions(device, BufferKind::DeviceLocal, sTableFilledUsage, "bind positions")
        , mBindNormals(device, BufferKind::DeviceLocal, sTableFilledUsage, "bind normals")
        , mBindTangents(device, BufferKind::DeviceLocal, sTableFilledUsage, "bind tangents")
        , mRuns(device, BufferKind::DeviceLocal, sTableFilledUsage, "rig runs")
        , mInfluences(device, BufferKind::DeviceLocal, sTableFilledUsage, "rig influences")
        , mMorphOffsets(device, BufferKind::DeviceLocal, sTableFilledUsage, "morph offsets")
        , mPoses([&](FrameSlot) { return GrowableBuffer(device, BufferKind::HostWritten, sTableFilledUsage, "poses"); })
    {
        mPoses.open(slots);

        // Every table exists from here, whether or not anything has been written to it: `outgrow`
        // makes a table that is empty whatever it is asked for, so a scene with no actor in it
        // still has a buffer at every address a dispatch could be handed.
        extend(batch, scene);
    }

    void SkinTables::extend(Batch& batch, const SceneDesc& scene)
    {
        const DeformerTable& deformers = scene.deformers();

        // Grown to what the scene reaches, and written whole where a growth moved it. The
        // arrivals are what a frame with an actor walking in costs; a table made again is what a
        // cell full of them costs, once per doubling.
        const VkDeviceSize bind = VkDeviceSize{ deformers.getBindVertexCount() } * sizeof(osg::Vec3f);
        const VkDeviceSize bindWords = VkDeviceSize{ deformers.getBindVertexCount() } * sizeof(std::uint32_t);
        // Each grown whether or not another moved, so three calls and not a short-circuit.
        const bool positionsMoved = mBindPositions.outgrow(bind);
        const bool normalsMoved = mBindNormals.outgrow(bind);
        const bool tangentsMoved = mBindTangents.outgrow(bindWords);
        const bool bindMoved = positionsMoved || normalsMoved || tangentsMoved;
        writeBind(batch, scene, scene.meshes().getArrived(), bindMoved);

        const Moved moved{
            .mRuns = mRuns.outgrow(deformers.getRuns().size() * sizeof(std::uint32_t)),
            .mInfluences = mInfluences.outgrow(deformers.getInfluences().size() * sizeof(Shaders::GpuInfluence)),
            .mOffsets = mMorphOffsets.outgrow(deformers.getMorphOffsets().size() * sizeof(osg::Vec3f)),
        };
        writeDeformers(batch, scene, deformers.getArrived(), moved);

        // The arrivals' poses into the first copy alone. Every other pose of a copy reaches it in
        // the placement that dispatches over it, and the other copies owe the arrivals theirs.
        for (GrowableBuffer& poses : mPoses.live())
            poses.outgrow(deformers.getPoses().size() * sizeof(PoseWord));
        writePoses(batch, scene, scene.meshes().getArrived());

        orderStagedWrites(batch);
    }

    void SkinTables::writeBind(
        Batch& batch, const SceneDesc& scene, const std::span<const Index> meshes, const bool whole)
    {
        const std::span<const MeshRange> ranges = scene.meshes().getRows();
        for (const Index index : whole ? everyBelow(ranges.size(), mEvery) : meshes)
        {
            const MeshRange& mesh = ranges[index];
            if (!mesh.deforms() || mesh.mVertices.mCount == 0)
                continue;

            const VkDeviceSize at = VkDeviceSize{ mesh.mBindOffset } * sizeof(osg::Vec3f);
            stageInto(batch, mBindPositions.get(), at, std::as_bytes(scene.meshes().getMeshPositions(index)));
            stageInto(batch, mBindNormals.get(), at, std::as_bytes(mesh.mVertices.in(scene.meshes().getNormals())));
            stageInto(batch, mBindTangents.get(), VkDeviceSize{ mesh.mBindOffset } * sizeof(std::uint32_t),
                std::as_bytes(mesh.mVertices.in(scene.meshes().getTangents())));
        }
    }

    void SkinTables::writeDeformers(
        Batch& batch, const SceneDesc& scene, const std::span<const Index> arrived, const Moved moved)
    {
        const DeformerTable& deformers = scene.deformers();
        const std::span<const Deformer> table = deformers.getDeformers();
        const bool whole = moved.mRuns || moved.mInfluences || moved.mOffsets;

        for (const Index index : whole ? everyBelow(table.size(), mEvery) : arrived)
        {
            // A freed slot deforms nothing and holds no run to write. An arrival stages every run
            // it holds; a row walked for a table that moved stages the runs into that table, and
            // the other tables keep what they hold.
            const Deformer& deformer = table[index];
            const bool fresh = !whole || std::find(arrived.begin(), arrived.end(), index) != arrived.end();

            if (!deformer.mRuns.empty() && (fresh || moved.mRuns))
                stageInto(batch, mRuns.get(), VkDeviceSize{ deformer.mRuns.mOffset } * sizeof(std::uint32_t),
                    std::as_bytes(deformer.mRuns.in(deformers.getRuns())));

            if (!deformer.mInfluences.empty() && (fresh || moved.mInfluences))
                stageInto(batch, mInfluences.get(),
                    VkDeviceSize{ deformer.mInfluences.mOffset } * sizeof(Shaders::GpuInfluence),
                    std::as_bytes(deformer.mInfluences.in(deformers.getInfluences())));

            if (!deformer.mOffsets.empty() && (fresh || moved.mOffsets))
                stageInto(batch, mMorphOffsets.get(), VkDeviceSize{ deformer.mOffsets.mOffset } * sizeof(osg::Vec3f),
                    std::as_bytes(deformer.mOffsets.in(deformers.getMorphOffsets())));
        }
    }

    void SkinTables::writePoses(Batch& batch, const SceneDesc& scene, const std::span<const Index> meshes)
    {
        const std::span<const MeshRange> ranges = scene.meshes().getRows();
        for (const Index index : meshes)
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

    VkDeviceAddress SkinTables::writePose(const SceneDesc& scene, const FrameSlot slot, const Index mesh)
    {
        const MeshRange& range = scene.meshes().getRows()[mesh];
        mPoses.at(slot).get().writeAt(VkDeviceSize{ range.mPoseOffset } * sizeof(PoseWord), scene.getMeshPose(mesh));

        return getPose(range, slot);
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
        return mBindNormals.get().addressFor() + VkDeviceSize{ mesh.mBindOffset } * sizeof(osg::Vec3f);
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
