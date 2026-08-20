#ifndef OPENMW_COMPONENTS_RTX_SCENEACCELERATION_H
#define OPENMW_COMPONENTS_RTX_SCENEACCELERATION_H

#include <cstdint>
#include <vector>

#include <osg/Matrixf>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;
    class SceneDesc;

    /// OpenSceneGraph's transform as Vulkan's.
    ///
    /// OSG multiplies a row vector on the left, so its translation is the last *row*; Vulkan's
    /// instance transform multiplies a column vector on the right, so its translation is the last
    /// *column* and the rotation is transposed. Getting this wrong mirrors the world about its
    /// diagonal, which is subtle enough on symmetrical architecture to survive a look.
    VkTransformMatrixKHR toVulkanTransform(const osg::Matrixf& matrix);

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

        VkAccelerationStructureKHR getTopLevel() const { return mTopLevel; }
        std::uint32_t getInstanceCount() const { return mInstanceCount; }

        /// Bytes held by the structures themselves, not counting the geometry they were built from.
        VkDeviceSize getStructureBytes() const { return mStructureBytes; }

    private:
        void buildBottomLevel(CommandPool& pool, const SceneDesc& scene);
        void buildTopLevel(CommandPool& pool, const SceneDesc& scene);

        const Device& mDevice;

        Buffer mPositions;
        Buffer mIndices;
        Buffer mBottomLevelStorage;
        Buffer mTopLevelStorage;
        Buffer mInstances;

        std::vector<VkAccelerationStructureKHR> mBottomLevel;
        VkAccelerationStructureKHR mTopLevel = VK_NULL_HANDLE;

        std::uint32_t mInstanceCount = 0;
        VkDeviceSize mStructureBytes = 0;
    };
}

#endif
