#include "physicaldevice.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volk.h>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

#include "requirements.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        std::vector<std::string> getDeviceExtensions(VkPhysicalDevice device)
        {
            const std::vector<VkExtensionProperties> properties = enumerateVk<VkExtensionProperties>(
                "vkEnumerateDeviceExtensionProperties", [&](std::uint32_t* count, VkExtensionProperties* into) {
                    return vkEnumerateDeviceExtensionProperties(device, nullptr, count, into);
                });

            std::vector<std::string> names;
            names.reserve(properties.size());
            for (const VkExtensionProperties& extension : properties)
                names.emplace_back(extension.extensionName);
            return names;
        }

        VkDeviceSize sumDeviceLocalHeaps(const VkPhysicalDeviceMemoryProperties& memory)
        {
            VkDeviceSize total = 0;
            for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
                if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    total += memory.memoryHeaps[i].size;
            return total;
        }

        bool has(std::span<const std::string> names, std::string_view name)
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        void appendListed(std::string& list, std::string_view item)
        {
            if (!list.empty())
                list += ", ";
            list += item;
        }

        /// @param driver what the device says of its driver: an extension its driver is only too old
        ///        for is named with the release that has it and the one that does not.
        std::string listMissingExtensions(
            std::span<const std::string> offered, const VkPhysicalDeviceVulkan12Properties& driver)
        {
            std::string missing;
            for (const RequiredExtension& required : getRequiredDeviceExtensions())
            {
                if (has(offered, required.mName))
                    continue;

                appendListed(missing, required.mName);
                const auto floor = std::ranges::find(required.mFloors, driver.driverID, &DriverFloor::mDriver);
                if (floor != required.mFloors.end())
                    missing += std::format(" ({} {} or later; this one is {})", releaseSeriesOf(driver.driverID),
                        floor->mRelease, driver.driverInfo);
            }

            return missing;
        }

        std::string listMissingFeatures(DeviceFeatures& supported)
        {
            std::vector<std::string_view> lacking;
            findMissingFeatures(supported, lacking);

            std::string missing;
            for (const std::string_view feature : lacking)
                appendListed(missing, feature);

            return missing;
        }

        /// Appends what each required format the device falls short on was for.
        std::string listMissingFormats(std::span<const VkFormatProperties> offered)
        {
            const std::span<const RequiredFormat> required = getRequiredFormats();
            assert(offered.size() == required.size() && "the format answers are read in the table's order");

            std::string missing;
            for (std::size_t at = 0; at < required.size(); ++at)
                if ((offered[at].optimalTilingFeatures & required[at].mFeatures) != required[at].mFeatures)
                    appendListed(missing, required[at].mFor);

            return missing;
        }

        /// Appends what each required texture image the device does not take was for.
        std::string listMissingImages(std::span<const std::optional<VkImageFormatProperties>> taken)
        {
            const std::span<const RequiredImage> required = getRequiredTextureImages();
            assert(taken.size() == required.size() && "the image answers are read in the table's order");

            std::string missing;
            for (std::size_t at = 0; at < required.size(); ++at)
                if (!taken[at].has_value())
                    appendListed(missing, required[at].mFor);

            return missing;
        }

        /// The largest heap holding a memory type a `Buffer::hostWritten` could come out of — the
        /// largest and not the sum, because a buffer goes in one heap.
        VkDeviceSize hostWrittenBytes(const VkPhysicalDeviceMemoryProperties& memory)
        {
            VkDeviceSize most = 0;
            for (std::uint32_t type = 0; type < memory.memoryTypeCount; ++type)
                if ((memory.memoryTypes[type].propertyFlags & sHostWritten) == sHostWritten)
                    most = std::max(most, memory.memoryHeaps[memory.memoryTypes[type].heapIndex].size);

            return most;
        }

        /// The queue family that can do everything this renderer submits, and presents to the window
        /// where there is one, where there is such a family. Graphics and compute and no more: a
        /// family that does either does transfers whether or not it says so, which Vulkan leaves it
        /// free not to.
        std::optional<std::uint32_t> findQueueFamily(
            std::span<const VkQueueFamilyProperties> queues, std::span<const VkBool32> presents)
        {
            constexpr VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            for (std::uint32_t at = 0; at < queues.size(); ++at)
                if ((queues[at].queueFlags & wanted) == wanted && (presents.empty() || presents[at] == VK_TRUE))
                    return at;

            return std::nullopt;
        }

        /// Everything a device says about itself, and what this renderer makes of it.
        struct Candidate
        {
            std::unique_ptr<DeviceProperties> mProperties;
            PhysicalDevice::Profile mProfile;
        };

        Candidate examine(VkPhysicalDevice handle, const PhysicalDevice::PresentQuery& presentsTo)
        {
            Candidate found;
            found.mProperties = std::make_unique<DeviceProperties>();
            vkGetPhysicalDeviceProperties2(handle, &found.mProperties->mProperties2);
            vkGetPhysicalDeviceMemoryProperties(handle, &found.mProperties->mMemory);

            DeviceFeatures supported;
            vkGetPhysicalDeviceFeatures2(handle, &supported.mFeatures2);

            // Through the two-call enumeration every other list query goes through, with the
            // success the entry point does not return supplied for it.
            const std::vector<VkQueueFamilyProperties> queues = enumerateVk<VkQueueFamilyProperties>(
                "vkGetPhysicalDeviceQueueFamilyProperties", [&](std::uint32_t* count, VkQueueFamilyProperties* into) {
                    vkGetPhysicalDeviceQueueFamilyProperties(handle, count, into);
                    return VK_SUCCESS;
                });

            std::vector<VkFormatProperties> formats;
            for (const RequiredFormat& required : getRequiredFormats())
                vkGetPhysicalDeviceFormatProperties(handle, required.mFormat, &formats.emplace_back());

            // A combination the device does not take is an answer, and every other failure is the
            // driver's.
            std::vector<std::optional<VkImageFormatProperties>> images;
            for (const RequiredImage& required : getRequiredTextureImages())
            {
                VkImageFormatProperties properties{};
                const VkResult asked = vkGetPhysicalDeviceImageFormatProperties(handle, required.mFormat,
                    VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, required.mUsage, required.mFlags, &properties);
                if (asked == VK_ERROR_FORMAT_NOT_SUPPORTED)
                    images.emplace_back();
                else
                {
                    checkVk(asked, "vkGetPhysicalDeviceImageFormatProperties");
                    images.emplace_back(properties);
                }
            }

            std::vector<VkBool32> presents;
            if (presentsTo)
                for (std::uint32_t family = 0; family < queues.size(); ++family)
                    presents.push_back(presentsTo(handle, family) ? VK_TRUE : VK_FALSE);

            found.mProfile = PhysicalDevice::profileOf(
                *found.mProperties, supported, getDeviceExtensions(handle), queues, formats, images, presents);

            return found;
        }
    }

    PhysicalDevice::Profile PhysicalDevice::profileOf(const DeviceProperties& properties, DeviceFeatures& supported,
        std::span<const std::string> extensions, std::span<const VkQueueFamilyProperties> queues,
        std::span<const VkFormatProperties> formats, std::span<const std::optional<VkImageFormatProperties>> images,
        std::span<const VkBool32> presents)
    {
        Profile profile;
        profile.mHostWrittenBytes = hostWrittenBytes(properties.mMemory);

        profile.mTextureSide = properties.mProperties2.properties.limits.maxImageDimension2D;
        for (const std::optional<VkImageFormatProperties>& taken : images)
            if (taken.has_value())
                profile.mTextureSide
                    = std::min({ profile.mTextureSide, taken->maxExtent.width, taken->maxExtent.height });

        assert((presents.empty() || presents.size() == queues.size()) && "an answer for every family or for none");
        const std::optional<std::uint32_t> family = findQueueFamily(queues, presents);
        if (family.has_value())
        {
            profile.mQueueFamily = *family;
            profile.mTimestampBits = queues[*family].timestampValidBits;
        }

        for (const OptionalExtensions& option : getOptionalExtensions())
            for (const char* const name : option.mExtensions)
                if (has(extensions, name))
                    profile.mOptionalExtensions.push_back(name);

        // In the order a reader would want to be told. A device short of the version cannot be
        // asked the rest of these questions meaningfully, and naming one missing feature of a card
        // that reports Vulkan 1.2 sends the reader after the wrong thing.
        if (properties.mProperties2.properties.apiVersion < sApiVersion)
        {
            profile.mObstacle = "reports Vulkan " + versionString(properties.mProperties2.properties.apiVersion);
            return profile;
        }

        if (const std::string missing = listMissingExtensions(extensions, properties.mVulkan12); !missing.empty())
        {
            profile.mObstacle = "missing extensions: " + missing;
            return profile;
        }

        // A window is shown through a swapchain, which a device that cannot make one would refuse at
        // its creation, after every other answer here said yes.
        if (!presents.empty() && !has(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            profile.mObstacle = std::string("missing extensions: ") + VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            return profile;
        }

        if (const std::string missing = listMissingFeatures(supported); !missing.empty())
        {
            profile.mObstacle = "missing features: " + missing;
            return profile;
        }

        if (const std::string missing = listMissingFormats(formats); !missing.empty())
        {
            profile.mObstacle = "missing format features for " + missing;
            return profile;
        }

        if (const std::string missing = listMissingImages(images); !missing.empty())
        {
            profile.mObstacle = "no texture images for " + missing;
            return profile;
        }

        // **What the shaders lean on that no feature names.** The upscaler's luma pyramid swaps
        // within quads (`ffx_spd.h`), where Vulkan guarantees basic subgroup operations alone; and
        // every push block is held to Vulkan 1.4's minimum, which a device reporting 1.4 and less
        // breaks.
        const VkPhysicalDeviceVulkan11Properties& subgroups = properties.mVulkan11;
        if ((subgroups.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT) == 0
            || (subgroups.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
        {
            profile.mObstacle = "no quad subgroup operations in compute shaders";
            return profile;
        }

        // Every module declares it (`pinFloatArithmetic`), and a device that does not honour it
        // could fold every guard against a value that is not a number. For 32-bit floats, the one
        // width a shipped module has: a module with another asks for a property this does not
        // require, which validation names at the pipeline.
        if (properties.mVulkan12.shaderSignedZeroInfNanPreserveFloat32 != VK_TRUE)
        {
            profile.mObstacle = "no preservation of signed zeros, infinities and NaNs in 32-bit floats";
            return profile;
        }

        const std::uint32_t pushed = properties.mProperties2.properties.limits.maxPushConstantsSize;
        if (pushed < sPushConstantsFloor)
        {
            profile.mObstacle = std::format("push constants of {} bytes, under {}", pushed, sPushConstantsFloor);
            return profile;
        }

        if (!family.has_value())
        {
            profile.mObstacle = presents.empty()
                ? "no queue family with graphics and compute"
                : "no queue family with graphics and compute that presents to the window";
            return profile;
        }

        // Nothing here stages, so a device the host cannot write into cannot run this at all. How
        // much room there is behind the type is answered rather than refused.
        if (profile.mHostWrittenBytes == 0)
        {
            profile.mObstacle = "no memory type the host writes into and the device reads";
            return profile;
        }

        return profile;
    }

    bool PhysicalDevice::hasOptionalExtension(const char* name) const
    {
        const std::vector<const char*>& offered = mProfile.mOptionalExtensions;

        return std::any_of(offered.begin(), offered.end(),
            [name](const char* const listed) { return std::strcmp(listed, name) == 0; });
    }

    PhysicalDevice PhysicalDevice::select(VkInstance instance, const PresentQuery& presents)
    {
        const std::vector<VkPhysicalDevice> handles = enumerateVk<VkPhysicalDevice>(
            "vkEnumeratePhysicalDevices", [&](std::uint32_t* count, VkPhysicalDevice* into) {
                return vkEnumeratePhysicalDevices(instance, count, into);
            });
        if (handles.empty())
            throw Unsupported("no Vulkan device is installed");

        std::string rejections;
        PhysicalDevice best;
        bool bestIsDiscrete = false;

        for (const VkPhysicalDevice handle : handles)
        {
            Candidate found = examine(handle, presents);
            if (!found.mProfile.mObstacle.empty())
            {
                rejections += "\n  ";
                rejections += found.mProperties->mProperties2.properties.deviceName;
                rejections += ": ";
                rejections += found.mProfile.mObstacle;
                continue;
            }

            const bool discrete
                = found.mProperties->mProperties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (best.mHandle != VK_NULL_HANDLE && (bestIsDiscrete || !discrete))
                continue;

            best.mHandle = handle;
            best.mProperties = std::move(found.mProperties);
            best.mProfile = std::move(found.mProfile);
            bestIsDiscrete = discrete;
        }

        if (best.mHandle == VK_NULL_HANDLE)
            throw Unsupported("no Vulkan device meets this renderer's requirements:" + rejections);

        // Every report from here on names the card and the driver it ran on.
        const DeviceProperties& chosen = *best.mProperties;
        Crash::annotate("device", chosen.mProperties2.properties.deviceName);
        Crash::annotate(
            "driver", std::string(chosen.mVulkan12.driverName) + " " + std::string(chosen.mVulkan12.driverInfo));

        return best;
    }

    std::string PhysicalDevice::describe() const
    {
        const VkPhysicalDeviceProperties& base = mProperties->mProperties2.properties;
        const VkPhysicalDeviceAccelerationStructurePropertiesKHR& as = mProperties->mAccelerationStructure;

        std::ostringstream out;
        out << "device:            " << base.deviceName << '\n'
            << "driver:            " << mProperties->mVulkan12.driverName << ' ' << mProperties->mVulkan12.driverInfo
            << '\n'
            << "Vulkan:            " << versionString(base.apiVersion) << '\n'
            << "device-local heap: " << sumDeviceLocalHeaps(mProperties->mMemory) / (1024 * 1024)
            << " MiB\n"
            // The heap a table the frame rewrites has to fit in, which on a card without
            // resizable BAR is a couple of hundred megabytes of the line above rather than all of
            // it. Printed beside it because the two look alike and only one of them bounds a scene.
            << "host-written:      " << mProfile.mHostWrittenBytes / (1024 * 1024) << " MiB\n"
            << "queue family:      " << mProfile.mQueueFamily << '\n'
            << "subgroup size:     " << mProperties->mVulkan11.subgroupSize << '\n';

        out << "timestamps:        ";
        if (mProfile.mTimestampBits == 0)
            out << "not on this queue\n";
        else
            out << mProfile.mTimestampBits << " bits at " << base.limits.timestampPeriod << " ns a tick\n";

        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& pipeline = mProperties->mRayTracingPipeline;

        out << "\nray tracing\n"
            << "  max geometry count:           " << as.maxGeometryCount << '\n'
            << "  max instance count:           " << as.maxInstanceCount << '\n'
            << "  max primitive count:          " << as.maxPrimitiveCount << '\n'
            << "  shader group handle:          " << pipeline.shaderGroupHandleSize << " bytes, aligned "
            << pipeline.shaderGroupHandleAlignment << ", based " << pipeline.shaderGroupBaseAlignment << '\n'
            << "  max ray dispatch:             " << pipeline.maxRayDispatchInvocationCount << '\n';

        out << "\noptional extensions present\n";
        if (mProfile.mOptionalExtensions.empty())
            out << "  (none)\n";
        for (const char* const name : mProfile.mOptionalExtensions)
            out << "  " << name << '\n';

        return out.str();
    }
}
