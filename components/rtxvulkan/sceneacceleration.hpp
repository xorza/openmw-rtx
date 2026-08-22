#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/instancerecord.hpp>

#include "buffer.hpp"
#include "hostbuffer.hpp"

namespace Rtx
{
    class CommandPool;
    class GpuTimer;
    class Device;
    class SceneDesc;

    /// The neutral transform in Vulkan's storage.
    ///
    /// `VkTransformMatrixKHR` is three rows of four, which is exactly what `Transform3x4` holds, so
    /// this restates the rows and changes nothing. The transposition that matters happened in
    /// `toTransform3x4`, once, where a backend cannot get it wrong on its own.
    VkTransformMatrixKHR toVulkanTransform(const Transform3x4& transform);

    /// Every acceleration structure a scene needs, built once.
    ///
    /// One bottom-level structure per mesh, all of them inside a single buffer at offsets, and one
    /// top-level structure over the instances. Per-mesh buffers would be the obvious shape and would
    /// spend a device allocation on each of a cell's several hundred meshes; the scene description is
    /// flat for the same reason.
    class SceneAcceleration
    {
    public:
        /// `scene` must place at least one instance: a top-level structure over nothing has no
        /// instance buffer to be built from. `records` are `scene`'s rows, made by the caller for
        /// the reason `place` gives.
        SceneAcceleration(
            const Device& device, CommandPool& pool, const SceneDesc& scene, std::span<const InstanceRecord> records);
        ~SceneAcceleration();

        SceneAcceleration(const SceneAcceleration&) = delete;
        SceneAcceleration& operator=(const SceneAcceleration&) = delete;

        /// Rebuilds what a moved world changed: every deformed mesh's structure, then the top level.
        ///
        /// **One submit for both, because the device is idle across a fence.** These were two
        /// `submitAndWait` calls, and the second could not begin recording until the first had
        /// finished on the queue — a round trip through the driver in the middle of the frame for a
        /// dependency a pipeline barrier already expresses. They go in one command buffer with that
        /// barrier between them.
        ///
        /// The deformed half is what a skinned body is: its triangles never change and its vertices
        /// change every frame, so the mesh keeps its slice of the shared position buffer and only
        /// the contents of that slice — and the structure over it — are made again. A rebuild rather
        /// than a refit: `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR` costs every static
        /// mesh in the cell a larger structure and a slower trace to make a few dozen actors cheaper
        /// to animate, which is M12's measurement to take and not an assumption to build on. It is
        /// skipped outright where nothing deformed, which is every frame of a world with no actor.
        ///
        /// **`records` is handed in rather than made here**, because `SceneBuffers` needs the same
        /// rows and building them twice was thousands of matrix inversions a frame done again for
        /// the same answer. `scene` must name the same meshes in the same order — the instances
        /// index into the structures this already holds.
        void place(CommandPool& pool, const SceneDesc& scene, std::span<const InstanceRecord> records, GpuTimer* timer);

        VkAccelerationStructureKHR getTopLevel() const { return mTopLevel; }

        /// The index buffer the structures were built from.
        ///
        /// A shader needs the same indices at a hit, to find which three vertices it landed between.
        /// They are here rather than in `SceneBuffers` because the build had to have them first, and
        /// uploading a cell's worth of them twice is a megabyte for nothing.
        VkBuffer getIndices() const { return mIndices.getHandle(); }
        std::uint32_t getInstanceCount() const { return mInstanceCount; }

        /// How many of those instances traversal has to stop and ask about.
        ///
        /// The cost of the cutout, as a number: every one of these is a candidate loop and a texture
        /// fetch where an opaque instance is a hit. Reported so that a material change that marks
        /// half a cell non-opaque shows up as a number before it shows up as a frame time.
        std::uint32_t getCutoutInstanceCount() const { return mCutoutInstanceCount; }

        /// Bytes held by the structures themselves, not counting the geometry they were built from.
        VkDeviceSize getStructureBytes() const { return mBottomLevelBytes + mTopLevelBytes; }

    private:
        void buildBottomLevel(CommandPool& pool, const SceneDesc& scene);

        /// Fills the refit build infos and sizes the scratch.
        ///
        /// Leaves `mRefitBuilds` holding exactly this frame's rebuilds and nothing else, which is
        /// what both the caller and `recordRefit` read: a count returned beside a vector that still
        /// held the last frame's entries would be two answers to one question.
        void prepareRefit(const SceneDesc& scene);

        /// Everything the top-level build needs before a command buffer exists: the instance rows,
        /// the buffer they are written to, the structure itself and the scratch to build it in.
        void prepareTopLevel(const SceneDesc& scene, std::span<const InstanceRecord> records);

        void recordRefit(VkCommandBuffer commands, GpuTimer* timer);
        void recordTopLevel(VkCommandBuffer commands, GpuTimer* timer);

        const Device& mDevice;

        /// Host-written, because a skinned body rewrites its own slice every frame and the build
        /// that reads it runs in the same submit — a host write before a submit needs no barrier.
        HostBuffer mPositions;

        Buffer mIndices;
        Buffer mBottomLevelStorage;
        Buffer mTopLevelStorage;

        /// The rows the top level is built from. Rewritten whole every frame, so it is written where
        /// the builder reads it rather than staged into place.
        HostBuffer mInstances;

        std::vector<VkAccelerationStructureKHR> mBottomLevel;

        /// Each of those structures' device address, asked for once when it was made.
        ///
        /// **Not once per instance per frame, which is what this replaced.** A handle lasts from one
        /// `setScene` to the next and its address with it, so a nine-by-nine exterior was making
        /// fifty thousand driver calls a frame to be told the same fifty thousand numbers.
        std::vector<VkDeviceAddress> mBottomLevelAddresses;

        VkAccelerationStructureKHR mTopLevel = VK_NULL_HANDLE;

        /// What each mesh's build asked for, so a rebuild does not have to ask the driver again.
        std::vector<VkDeviceSize> mBuildScratch;

        /// Kept across frames rather than made per refit: a device allocation on the frame path is a
        /// stall, and this settles at the high-water mark of whatever the world is showing. It never
        /// shrinks, which is what makes it settle at all.
        Buffer mRefitScratch;

        /// The top level's build scratch, which was made and freed on every frame that moved.
        ///
        /// **`vkAllocateMemory` twice on every frame that moves** — this and the storage buffer
        /// beside it — where the driver's allocator is exactly the thing a frame budget cannot see
        /// into. Both grow to the high-water mark and stay.
        Buffer mTopLevelScratch;

        /// The top-level build, prepared before a command buffer exists and recorded into one after.
        ///
        /// Members rather than locals because `pGeometries` is a pointer the build info keeps: the
        /// geometry has to outlive the preparation that named it. The build range does not — it is
        /// `mInstanceCount` and nothing else, so `recordTopLevel` makes its own.
        VkAccelerationStructureGeometryKHR mTopLevelGeometry{};
        VkAccelerationStructureBuildGeometryInfoKHR mTopLevelBuild{};

        // Refilled per refit. The build reads `pGeometries` through a pointer, so the geometries are
        // sized before any build info names one.
        std::vector<VkAccelerationStructureGeometryKHR> mRefitGeometries;
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> mRefitBuilds;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> mRefitRanges;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> mRefitRangePointers;

        // Refilled per placement rather than reallocated: a scene is tens of thousands of these.
        std::vector<VkAccelerationStructureInstanceKHR> mRowScratch;

        std::uint32_t mInstanceCount = 0;
        std::uint32_t mCutoutInstanceCount = 0;

        /// **Two totals, each assigned, because one accumulated.** The bottom levels are made once
        /// and the top level again every frame that moves, so adding both to one figure reported a
        /// scene that grew by its own top level sixty times a second.
        VkDeviceSize mBottomLevelBytes = 0;
        VkDeviceSize mTopLevelBytes = 0;
    };
}
