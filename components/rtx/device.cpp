#include "device.hpp"

#include <string>

#include "error.hpp"
#include "instance.hpp"

namespace Rtx
{
    namespace
    {
        template <class T>
        void load(VkDevice device, T& out, const char* name)
        {
            out = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
            if (out == nullptr)
                throw Error(
                    std::string("the driver advertises the extension providing ") + name + " but does not dispatch it");
        }
    }

    Device::Device(
        const Instance& instance, PhysicalDevice&& physicalDevice, const std::vector<const char*>& extraExtensions)
        : mPhysicalDevice(std::move(physicalDevice))
    {
        std::vector<const char*> extensions;
        for (const char* const name : getRequiredDeviceExtensions())
            extensions.push_back(name);
        for (const char* const name : mPhysicalDevice.getAvailableOptionalExtensions())
            extensions.push_back(name);
        for (const char* const name : extraExtensions)
            extensions.push_back(name);

        DeviceFeatures features;
        requestRequiredFeatures(features);

        const float priority = 1.0f;
        const VkDeviceQueueCreateInfo queue{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = mPhysicalDevice.getQueueFamily(),
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };

        const VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features.mFeatures2,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue,
            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            // Superseded by the VkPhysicalDeviceFeatures2 in the chain, and the two cannot both be set.
            .pEnabledFeatures = nullptr,
        };

        checkVk(vkCreateDevice(mPhysicalDevice.getHandle(), &createInfo, nullptr, &mHandle), "vkCreateDevice");

        vkGetDeviceQueue(mHandle, mPhysicalDevice.getQueueFamily(), 0, &mQueue);

        load(mHandle, mFunctions.mGetAccelerationStructureBuildSizes, "vkGetAccelerationStructureBuildSizesKHR");
        load(mHandle, mFunctions.mCreateAccelerationStructure, "vkCreateAccelerationStructureKHR");
        load(mHandle, mFunctions.mDestroyAccelerationStructure, "vkDestroyAccelerationStructureKHR");
        load(mHandle, mFunctions.mCmdBuildAccelerationStructures, "vkCmdBuildAccelerationStructuresKHR");
        load(mHandle, mFunctions.mCmdCopyAccelerationStructure, "vkCmdCopyAccelerationStructureKHR");
        load(mHandle, mFunctions.mCmdWriteAccelerationStructuresProperties,
            "vkCmdWriteAccelerationStructuresPropertiesKHR");
        load(mHandle, mFunctions.mGetAccelerationStructureDeviceAddress, "vkGetAccelerationStructureDeviceAddressKHR");
        load(mHandle, mFunctions.mCreateRayTracingPipelines, "vkCreateRayTracingPipelinesKHR");
        load(mHandle, mFunctions.mGetRayTracingShaderGroupHandles, "vkGetRayTracingShaderGroupHandlesKHR");
        load(mHandle, mFunctions.mCmdTraceRays, "vkCmdTraceRaysKHR");
        load(mHandle, mFunctions.mCreateMicromap, "vkCreateMicromapEXT");
        load(mHandle, mFunctions.mDestroyMicromap, "vkDestroyMicromapEXT");
        load(mHandle, mFunctions.mGetMicromapBuildSizes, "vkGetMicromapBuildSizesEXT");
        load(mHandle, mFunctions.mCmdBuildMicromaps, "vkCmdBuildMicromapsEXT");

        if (instance.getValidationLog() != nullptr)
            mSetObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                vkGetDeviceProcAddr(mHandle, "vkSetDebugUtilsObjectNameEXT"));
    }

    Device::~Device()
    {
        if (mHandle != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(mHandle);
            vkDestroyDevice(mHandle, nullptr);
        }
    }

    void Device::waitIdle() const
    {
        checkVk(vkDeviceWaitIdle(mHandle), "vkDeviceWaitIdle");
    }

    void Device::setNameImpl(VkObjectType type, std::uint64_t handle, const char* name) const
    {
        if (mSetObjectName == nullptr)
            return;

        const VkDebugUtilsObjectNameInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = type,
            .objectHandle = handle,
            .pObjectName = name,
        };
        checkVk(mSetObjectName(mHandle, &info), "vkSetDebugUtilsObjectNameEXT");
    }
}
