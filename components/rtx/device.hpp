#ifndef OPENMW_COMPONENTS_RTX_DEVICE_H
#define OPENMW_COMPONENTS_RTX_DEVICE_H

#include <cstdint>
#include <memory>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include "physicaldevice.hpp"

namespace Rtx
{
    class Instance;
    class PipelineCache;

    /// Entry points that come from the required extensions rather than from core Vulkan.
    ///
    /// Resolved once at device creation and missing-checked there, so a driver that advertises an
    /// extension it cannot actually dispatch fails at startup instead of at the first frame that
    /// needed it.
    struct DeviceFunctions
    {
        PFN_vkGetAccelerationStructureBuildSizesKHR mGetAccelerationStructureBuildSizes = nullptr;
        PFN_vkCreateAccelerationStructureKHR mCreateAccelerationStructure = nullptr;
        PFN_vkDestroyAccelerationStructureKHR mDestroyAccelerationStructure = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR mCmdBuildAccelerationStructures = nullptr;
        PFN_vkCmdCopyAccelerationStructureKHR mCmdCopyAccelerationStructure = nullptr;
        PFN_vkCmdWriteAccelerationStructuresPropertiesKHR mCmdWriteAccelerationStructuresProperties = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR mGetAccelerationStructureDeviceAddress = nullptr;

        PFN_vkCreateRayTracingPipelinesKHR mCreateRayTracingPipelines = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR mGetRayTracingShaderGroupHandles = nullptr;
        PFN_vkCmdTraceRaysKHR mCmdTraceRays = nullptr;

        PFN_vkCreateMicromapEXT mCreateMicromap = nullptr;
        PFN_vkDestroyMicromapEXT mDestroyMicromap = nullptr;
        PFN_vkGetMicromapBuildSizesEXT mGetMicromapBuildSizes = nullptr;
        PFN_vkCmdBuildMicromapsEXT mCmdBuildMicromaps = nullptr;
    };

    /// A logical device, its single queue, and the extension entry points.
    class Device
    {
    public:
        /// @param instance must outlive the device. Not held: a `VkDevice` does not reference its
        ///        instance, but every entry point reached through it does.
        /// @param extraExtensions device extensions beyond the required and optional lists — the
        ///        swapchain, when there is a window.
        Device(const Instance& instance, PhysicalDevice&& physicalDevice,
            const std::vector<const char*>& extraExtensions = {});
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        VkDevice getHandle() const { return mHandle; }
        VkQueue getQueue() const { return mQueue; }
        std::uint32_t getQueueFamily() const { return mPhysicalDevice.getQueueFamily(); }
        const PhysicalDevice& getPhysicalDevice() const { return mPhysicalDevice; }
        const DeviceFunctions& getFunctions() const { return mFunctions; }

        /// Handed to every `vkCreate*Pipelines` on this device, so that a shader is compiled once
        /// per change rather than once per pipeline.
        ///
        /// Out of line because the cache is only forward-declared here: it carries `<filesystem>`,
        /// and this header is included by two dozen others that have no use for it.
        VkPipelineCache getPipelineCache() const;

        /// Attaches a name to a Vulkan object so captures and validation messages name it.
        ///
        /// Compiled to nothing in release: an unreadable capture is a debugging session that does
        /// not happen, and a released build has no captures.
        void setName([[maybe_unused]] VkObjectType type, [[maybe_unused]] std::uint64_t handle,
            [[maybe_unused]] const char* name) const
        {
#ifdef OPENMW_RTX_DEBUG_NAMES
            setNameImpl(type, handle, name);
#endif
        }

        /// Blocks until the queue has finished everything. For tearing down and for resizing, not
        /// for pacing a frame.
        void waitIdle() const;

    private:
        void setNameImpl(VkObjectType type, std::uint64_t handle, const char* name) const;

        PhysicalDevice mPhysicalDevice;
        VkDevice mHandle = VK_NULL_HANDLE;
        VkQueue mQueue = VK_NULL_HANDLE;
        DeviceFunctions mFunctions;
        PFN_vkSetDebugUtilsObjectNameEXT mSetObjectName = nullptr;

        // Last, so that it is torn down first: saving it reads from the device, which the members
        // above are still holding open at that point.
        std::unique_ptr<PipelineCache> mPipelineCache;
    };
}

#endif
