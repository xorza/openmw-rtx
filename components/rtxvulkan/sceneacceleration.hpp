#pragma once

#include <cstdint>
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
        /// instance buffer to be built from.
        SceneAcceleration(const Device& device, CommandPool& pool, const SceneDesc& scene);
        ~SceneAcceleration();

        SceneAcceleration(const SceneAcceleration&) = delete;
        SceneAcceleration& operator=(const SceneAcceleration&) = delete;

        /// Rebuilds the bottom-level structure of every mesh `scene.getDeformed()` names, and
        /// re-uploads the vertices they were built from.
        ///
        /// **What a skinned body is.** Its triangles never change and its vertices change every
        /// frame, so the mesh keeps its slice of the shared position buffer and only the contents
        /// of that slice — and the structure over it — are made again. A rebuild rather than a
        /// refit: `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR` costs every static mesh in
        /// the cell a larger structure and a slower trace to make a few dozen actors cheaper to
        /// animate, which is M12's measurement to take and not an assumption to build on.
        ///
        /// Does nothing where nothing deformed, which is every frame of a world with no actor in it.
        void refitMeshes(CommandPool& pool, const SceneDesc& scene, GpuTimer* timer);

        /// Rebuilds the top level over `scene`'s instances, keeping every bottom-level structure.
        ///
        /// **What a frame does when the world has moved.** A crate's geometry does not change when
        /// the crate does, and its bottom-level structure is the expensive half; the top level is a
        /// list of transforms and is rebuilt per frame in every renderer that does this.
        ///
        /// `scene` must name the same meshes in the same order — the instances index into the
        /// structures this already holds.
        void placeInstances(CommandPool& pool, const SceneDesc& scene, GpuTimer* timer);

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
        VkDeviceSize getStructureBytes() const { return mStructureBytes; }

    private:
        void buildBottomLevel(CommandPool& pool, const SceneDesc& scene);
        void buildTopLevel(CommandPool& pool, const SceneDesc& scene, GpuTimer* timer);

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
        VkAccelerationStructureKHR mTopLevel = VK_NULL_HANDLE;

        /// What each mesh's build asked for, so a rebuild does not have to ask the driver again.
        std::vector<VkDeviceSize> mBuildScratch;

        /// Kept across frames rather than made per refit: a device allocation on the frame path is a
        /// stall, and this settles at the high-water mark of whatever the world is showing. It never
        /// shrinks, which is what makes it settle at all.
        Buffer mRefitScratch;

        // Refilled per refit. The build reads `pGeometries` through a pointer, so the geometries are
        // sized before any build info names one.
        std::vector<VkAccelerationStructureGeometryKHR> mRefitGeometries;
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> mRefitBuilds;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> mRefitRanges;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> mRefitRangePointers;

        // Refilled per placement rather than reallocated: a scene is tens of thousands of these.
        std::vector<InstanceRecord> mRecordScratch;
        std::vector<VkAccelerationStructureInstanceKHR> mRowScratch;

        std::uint32_t mInstanceCount = 0;
        std::uint32_t mCutoutInstanceCount = 0;
        VkDeviceSize mStructureBytes = 0;
    };
}
