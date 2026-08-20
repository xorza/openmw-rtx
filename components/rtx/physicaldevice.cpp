#include "physicaldevice.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <string_view>

#include "error.hpp"

namespace Rtx
{
    namespace
    {
        std::vector<std::string> getDeviceExtensions(VkPhysicalDevice device)
        {
            std::uint32_t count = 0;
            checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
                "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> properties(count);
            checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()),
                "vkEnumerateDeviceExtensionProperties");

            std::vector<std::string> names;
            names.reserve(properties.size());
            for (const VkExtensionProperties& extension : properties)
                names.emplace_back(extension.extensionName);
            return names;
        }

        bool has(const std::vector<std::string>& names, std::string_view name)
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        /// The queue family that can do everything this renderer submits.
        ///
        /// Returns `-1` when there is none, which disqualifies the device.
        int findQueueFamily(VkPhysicalDevice device)
        {
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

            constexpr VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
            for (std::uint32_t i = 0; i < count; ++i)
                if ((families[i].queueFlags & wanted) == wanted)
                    return static_cast<int>(i);

            return -1;
        }

        VkDeviceSize sumDeviceLocalHeaps(VkPhysicalDevice device)
        {
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(device, &memory);

            VkDeviceSize total = 0;
            for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
                if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    total += memory.memoryHeaps[i].size;
            return total;
        }

        /// Why a candidate was rejected, or empty when it was not.
        std::string disqualify(VkPhysicalDevice handle, const DeviceProperties& properties)
        {
            if (properties.mProperties2.properties.apiVersion < sApiVersion)
                return "reports Vulkan "
                    + std::to_string(VK_API_VERSION_MAJOR(properties.mProperties2.properties.apiVersion)) + '.'
                    + std::to_string(VK_API_VERSION_MINOR(properties.mProperties2.properties.apiVersion));

            const std::vector<std::string> available = getDeviceExtensions(handle);
            std::string missing;
            for (const char* const required : getRequiredDeviceExtensions())
                if (!has(available, required))
                {
                    if (!missing.empty())
                        missing += ", ";
                    missing += required;
                }
            if (!missing.empty())
                return "missing extensions: " + missing;

            DeviceFeatures supported;
            vkGetPhysicalDeviceFeatures2(handle, &supported.mFeatures2);

            std::vector<std::string_view> missingFeatures;
            findMissingFeatures(supported, missingFeatures);
            if (!missingFeatures.empty())
            {
                std::string names;
                for (const std::string_view feature : missingFeatures)
                {
                    if (!names.empty())
                        names += ", ";
                    names += feature;
                }
                return "missing features: " + names;
            }

            if (findQueueFamily(handle) < 0)
                return "no queue family with graphics, compute and transfer";

            return {};
        }
    }

    PhysicalDevice PhysicalDevice::select(VkInstance instance)
    {
        std::uint32_t count = 0;
        checkVk(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (count == 0)
            throw Error("no Vulkan device is installed");

        std::vector<VkPhysicalDevice> handles(count);
        checkVk(vkEnumeratePhysicalDevices(instance, &count, handles.data()), "vkEnumeratePhysicalDevices");

        std::string rejections;
        PhysicalDevice best;
        bool bestIsDiscrete = false;

        for (const VkPhysicalDevice handle : handles)
        {
            auto properties = std::make_unique<DeviceProperties>();
            vkGetPhysicalDeviceProperties2(handle, &properties->mProperties2);

            const std::string reason = disqualify(handle, *properties);
            if (!reason.empty())
            {
                rejections += "\n  ";
                rejections += properties->mProperties2.properties.deviceName;
                rejections += ": ";
                rejections += reason;
                continue;
            }

            const bool discrete
                = properties->mProperties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (best.mHandle != VK_NULL_HANDLE && (bestIsDiscrete || !discrete))
                continue;

            const std::vector<std::string> available = getDeviceExtensions(handle);
            std::vector<const char*> optional;
            for (const char* const name : getOptionalDeviceExtensions())
                if (has(available, name))
                    optional.push_back(name);

            best.mHandle = handle;
            best.mProperties = std::move(properties);
            best.mQueueFamily = static_cast<std::uint32_t>(findQueueFamily(handle));
            best.mOptionalExtensions = std::move(optional);
            best.mDeviceLocalMemory = sumDeviceLocalHeaps(handle);
            bestIsDiscrete = discrete;
        }

        if (best.mHandle == VK_NULL_HANDLE)
            throw Error("no Vulkan device meets this renderer's requirements:" + rejections);

        return best;
    }

    std::string PhysicalDevice::describe() const
    {
        const VkPhysicalDeviceProperties& base = mProperties->mProperties2.properties;

        std::string out;
        out += "device:            ";
        out += base.deviceName;
        out += "\ndriver:            ";
        out += mProperties->mVulkan12.driverName;
        out += ' ';
        out += mProperties->mVulkan12.driverInfo;
        out += "\nVulkan:            " + std::to_string(VK_API_VERSION_MAJOR(base.apiVersion)) + '.'
            + std::to_string(VK_API_VERSION_MINOR(base.apiVersion)) + '.'
            + std::to_string(VK_API_VERSION_PATCH(base.apiVersion));
        out += "\ndevice-local heap: " + std::to_string(mDeviceLocalMemory / (1024 * 1024)) + " MiB";
        out += "\nqueue family:      " + std::to_string(mQueueFamily);
        out += "\nsubgroup size:     " + std::to_string(mProperties->mVulkan11.subgroupSize);

        out += "\n\nray tracing\n";
        out += "  shader group handle size:     "
            + std::to_string(mProperties->mRayTracingPipeline.shaderGroupHandleSize) + " bytes\n";
        out += "  shader group base alignment:  "
            + std::to_string(mProperties->mRayTracingPipeline.shaderGroupBaseAlignment) + " bytes\n";
        out += "  max ray recursion depth:      "
            + std::to_string(mProperties->mRayTracingPipeline.maxRayRecursionDepth) + '\n';
        out += "  max ray dispatch invocations: "
            + std::to_string(mProperties->mRayTracingPipeline.maxRayDispatchInvocationCount) + '\n';
        out += "  max geometry count:           " + std::to_string(mProperties->mAccelerationStructure.maxGeometryCount)
            + '\n';
        out += "  max instance count:           " + std::to_string(mProperties->mAccelerationStructure.maxInstanceCount)
            + '\n';
        out += "  max primitive count:          "
            + std::to_string(mProperties->mAccelerationStructure.maxPrimitiveCount) + '\n';
        out += "  micromap subdivision levels:  "
            + std::to_string(mProperties->mOpacityMicromap.maxOpacity2StateSubdivisionLevel) + " (2-state), "
            + std::to_string(mProperties->mOpacityMicromap.maxOpacity4StateSubdivisionLevel) + " (4-state)\n";

        // A driver may expose the extension and then decline to reorder, which would make every
        // measurement of SER meaningless. The hint is the only way to tell.
        out += "  invocation reorder:           ";
        out += mProperties->mInvocationReorder.rayTracingInvocationReorderReorderingHint
                == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT
            ? "reorders"
            : "accepts the call and does nothing";

        out += "\n\noptional extensions present\n";
        if (mOptionalExtensions.empty())
            out += "  (none)\n";
        for (const char* const name : mOptionalExtensions)
        {
            out += "  ";
            out += name;
            out += '\n';
        }

        return out;
    }
}
