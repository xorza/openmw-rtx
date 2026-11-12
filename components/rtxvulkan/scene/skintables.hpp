#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/deformertable.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneroom.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>

namespace Rtx
{
    class Batch;
    class Device;
    class Uploads;

    /// What one scene's deforming meshes are posed from: their bind poses, their rigs and morphs,
    /// and the poses a frame writes. The bind poses live here and not in the shared vertex blocks,
    /// because those are the pose's destination and what it reads from must be somewhere the frame
    /// never writes — a table of the deforming meshes alone, indexed by `MeshRange::mBindOffset`.
    /// Plain buffers made at the scene's room and grown by doubling past it, because the kernel is
    /// handed each run's address in its push constants; a growth copies the table on the queue and
    /// buries the one it displaced, so an arrival stages only itself. Device memory, written on the
    /// queue. The poses are per frame in flight and host-written, because `SkinPass::record` writes
    /// and reads them in one placement, and never rewritten on growth — one table for both kinds,
    /// in the words `Rtx::PoseWord` lays either in.
    class SkinTables
    {
    public:
        /// @param batch what the first arrival rides, which is every deforming mesh the scene holds.
        /// @param everyMesh every mesh slot the scene holds, `DeviceScene::mEveryMesh`.
        /// @param slots how many frames may be posing this scene at once.
        /// @param room the bind vertices, runs, influences, offsets and pose words every table is
        ///        made with room for.
        SkinTables(const Device& device, Batch& batch, const SceneDesc& scene, std::span<const Index> everyMesh,
            std::uint32_t slots, const SceneRoom& room);

        /// Takes in what the scene says arrived: the bind poses of the deforming meshes, the
        /// deformers, and the arrived meshes' poses into the first copy, which is the one an
        /// arrival is posed into. Through `batch` and not from the host: a run an arrival was given
        /// may be one a departed mesh held, which a frame on the queue can still be reading — a copy
        /// recorded now runs behind that frame, where a host write would land under it. Ends in the
        /// barrier the dispatch over the arrivals needs.
        void extend(Batch& batch, const SceneDesc& scene);

        /// Waits until nothing on the queue reads or writes `slot`'s poses, ahead of a placement
        /// that writes them from the host. The reader is usually the placement before last's
        /// dispatch, long finished; after an arrival it is `extend`'s staged poses and the dispatch
        /// over them. Each copy carries the value, so the wait is for that submit and not for the
        /// frame behind it.
        void finishReads(FrameSlot slot) const;

        /// Writes the poses of `meshes` into `slot`'s copy through `uploads`, for dispatches recorded
        /// after their `order` to read where `getPose` says: a placement's copy, whose last reader
        /// the caller waited for, which a mapped write asserts. One write, since a pose stands in
        /// the copy where it stands among the scene's: poses adjacent there are one run, and a
        /// crowd is a handful of copy regions and not one copy a body.
        void writePoses(const SceneDesc& scene, FrameSlot slot, std::span<const Index> meshes, Uploads& uploads);

        /// The meshes a placement poses, cleared and refilled by `SkinPass::record` and never freed:
        /// what it writes the poses of before one `order`, and then dispatches over.
        std::vector<Index>& getPosing() { return mPosing; }

        /// Where `mesh`'s pose sits in `slot`'s copy, for a dispatch over a pose `extend` staged.
        VkDeviceAddress getPose(const MeshRange& mesh, FrameSlot slot) const;

        /// Where `mesh`'s bind pose starts, in each of the three bind tables.
        VkDeviceAddress getBindPositions(const MeshRange& mesh) const;
        VkDeviceAddress getBindNormals(const MeshRange& mesh) const;
        VkDeviceAddress getBindTangents(const MeshRange& mesh) const;

        /// Where a rig's runs and influences start, and where a morph's offsets do.
        VkDeviceAddress getRuns(const Deformer& rig) const;
        VkDeviceAddress getInfluences(const Deformer& rig) const;
        VkDeviceAddress getMorphOffsets(const Deformer& morph) const;

        VkDeviceSize getBytes() const;

    private:
        /// How far the scene's runs reach, in the room's elements.
        static SceneRoom reachOf(const DeformerTable& deformers);

        /// Grows each table to `reach`, the source tables keeping what they hold.
        void grow(Batch& batch, const SceneRoom& reach);

        /// Stages the bind poses of `meshes`, the runs of `deformers`, and the poses of `posed` into
        /// the first copy. Nothing is ordered here.
        void write(Batch& batch, const SceneDesc& scene, std::span<const Index> meshes,
            std::span<const Index> deformers, std::span<const Index> posed);

        GrowableBuffer mBindPositions;
        GrowableBuffer mBindNormals;
        GrowableBuffer mBindTangents;
        GrowableBuffer mRuns;
        GrowableBuffer mInfluences;
        GrowableBuffer mMorphOffsets;

        PerSlot<GrowableBuffer> mPoses;

        std::vector<Index> mPosing;

        /// Cleared and refilled by `writePoses`, never freed: a run a pose, then the runs they join into.
        std::vector<CopyRun> mPoseRuns;
        std::vector<CopyRun> mJoinedRuns;
    };
}
