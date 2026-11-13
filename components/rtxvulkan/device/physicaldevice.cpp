#include "physicaldevice.hpp"

#include <algorithm>
#include <array>
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
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/shaders/shared/sets.h>

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

        std::string_view kindOf(const VkPhysicalDeviceType type)
        {
            switch (type)
            {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    return "discrete";
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    return "integrated";
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    return "virtual";
                case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    return "software";
                default:
                    return "other";
            }
        }

        /// A device's kind and its PCI vendor and device identifiers.
        std::string identityOf(const VkPhysicalDeviceProperties& base)
        {
            return std::format("{}, {:04x}:{:04x}", kindOf(base.deviceType), base.vendorID, base.deviceID);
        }

        /// `identityOf` and its Vulkan version, then its driver and video memory where it was asked them:
        /// a device below the renderer's version, or one that answered with an error, was asked its
        /// plain properties alone.
        std::string detailsOf(const VkPhysicalDeviceProperties& base, const VkPhysicalDeviceVulkan12Properties* driver,
            const VkPhysicalDeviceMemoryProperties* memory)
        {
            std::string details = std::format("{}, Vulkan {}", identityOf(base), versionString(base.apiVersion));
            if (driver != nullptr && driver->driverName[0] != '\0')
                details += std::format(", {} {}", driver->driverName, driver->driverInfo);
            if (memory != nullptr && memory->memoryHeapCount > 0)
                details += std::format(", {:.1f} MiB video memory", mebibytes(sumDeviceLocalHeaps(*memory)));
            return details;
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

        std::string listMissingExtensions(std::span<const std::string> offered)
        {
            std::string missing;
            for (const char* const required : getRequiredDeviceExtensions())
                if (!has(offered, required))
                    appendListed(missing, required);

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

            // **The version before anything else is asked**: the structures the questions below chain are
            // Vulkan 1.2's and later, and their image flags 1.1's, which a device below the renderer's
            // version does not define. It is rejected on its version alone, as every candidate is (`profileOf`).
            VkPhysicalDeviceProperties plain{};
            vkGetPhysicalDeviceProperties(handle, &plain);
            if (plain.apiVersion < sApiVersion)
            {
                found.mProperties = std::make_unique<DeviceProperties>(std::span<const std::string>());
                found.mProperties->mProperties2.properties = plain;
                DeviceFeatures none(std::span<const std::string>{});
                found.mProfile = PhysicalDevice::profileOf(*found.mProperties, none, {}, {}, {}, {}, {});
                return found;
            }

            // **The extensions before any structure of theirs is asked**: a structure of an extension
            // the device does not list is no valid member of a query's chain, which a candidate
            // with no ray tracing — an integrated card, lavapipe — is asked as surely as one with.
            const std::vector<std::string> extensions = getDeviceExtensions(handle);

            found.mProperties = std::make_unique<DeviceProperties>(extensions);
            vkGetPhysicalDeviceProperties2(handle, &found.mProperties->mProperties2);
            vkGetPhysicalDeviceMemoryProperties(handle, &found.mProperties->mMemory);

            DeviceFeatures supported(extensions);
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
                    checkVkWithoutDevice(asked, "vkGetPhysicalDeviceImageFormatProperties");
                    images.emplace_back(properties);
                }
            }

            std::vector<VkBool32> presents;
            if (presentsTo)
                for (std::uint32_t family = 0; family < queues.size(); ++family)
                    presents.push_back(presentsTo(handle, family) ? VK_TRUE : VK_FALSE);

            if (std::find(extensions.begin(), extensions.end(), VK_EXT_PCI_BUS_INFO_EXTENSION_NAME) != extensions.end())
            {
                VkPhysicalDevicePCIBusInfoPropertiesEXT bus{};
                bus.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
                VkPhysicalDeviceProperties2 asked{};
                asked.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                asked.pNext = &bus;
                vkGetPhysicalDeviceProperties2(handle, &asked);
                found.mProperties->mPciBus = bus;
            }

            found.mProfile = PhysicalDevice::profileOf(
                *found.mProperties, supported, extensions, queues, formats, images, presents);

            return found;
        }
    }

    PhysicalDevice::Profile PhysicalDevice::profileOf(const DeviceProperties& properties, DeviceFeatures& supported,
        std::span<const std::string> extensions, std::span<const VkQueueFamilyProperties> queues,
        std::span<const VkFormatProperties> formats, std::span<const std::optional<VkImageFormatProperties>> images,
        std::span<const VkBool32> presents)
    {
        Profile profile;
        profile.mDriver = DriverProfile::of(properties.mProperties2.properties.vendorID, properties.mVulkan12.driverID);

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

        if (const std::string missing = listMissingExtensions(extensions); !missing.empty())
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
        // within quads (`ffx_spd.h`), and the trace's stages list their tiles and landings by ballot
        // and count by sum (`bounce.comp`, `visibility.comp`), where Vulkan guarantees basic subgroup
        // operations alone; and every push block is held to Vulkan 1.4's minimum, which a device
        // reporting 1.4 and less breaks.
        constexpr std::array<std::pair<VkSubgroupFeatureFlagBits, std::string_view>, 3> sOperations{ {
            { VK_SUBGROUP_FEATURE_QUAD_BIT, "quad" },
            { VK_SUBGROUP_FEATURE_BALLOT_BIT, "ballot" },
            { VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "arithmetic" },
        } };
        const VkPhysicalDeviceVulkan11Properties& subgroups = properties.mVulkan11;
        for (const auto& [operation, kind] : sOperations)
            if ((subgroups.subgroupSupportedOperations & operation) == 0
                || (subgroups.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
            {
                profile.mObstacle = "no " + std::string(kind) + " subgroup operations in compute shaders";
                return profile;
            }

        // The trace's kernels run at 32 lanes (`SubgroupSize::ThirtyTwo`), which a device has to let a
        // compute stage ask for: Vulkan 1.3 requires the stage, and not the width.
        const VkPhysicalDeviceVulkan13Properties& sizes = properties.mVulkan13;
        if ((sizes.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0 || sizes.minSubgroupSize > 32
            || sizes.maxSubgroupSize < 32)
        {
            profile.mObstacle = "no compute subgroup of 32 lanes to ask for";
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

        // **The texture set's arrays, which a device is asked for and not assumed to hold**: its two
        // arrays of sampled images and its samplers, bound after use (`TextureArray`), in one set and
        // in each stage that reads them. A device short of either failed to make the set's layout, a
        // fault at start, where it is refused here by name.
        const VkPhysicalDeviceVulkan12Properties& bound = properties.mVulkan12;
        constexpr std::uint32_t arrayImages = 2 * Shaders::TEXTURE_SLOTS;
        constexpr std::uint32_t arraySamplers = Shaders::TEXTURE_SAMPLERS;
        if (std::min(bound.maxDescriptorSetUpdateAfterBindSampledImages,
                bound.maxPerStageDescriptorUpdateAfterBindSampledImages)
            < arrayImages)
        {
            profile.mObstacle = std::format("{} sampled images bound after use, under {}",
                std::min(bound.maxDescriptorSetUpdateAfterBindSampledImages,
                    bound.maxPerStageDescriptorUpdateAfterBindSampledImages),
                arrayImages);
            return profile;
        }
        if (std::min(bound.maxDescriptorSetUpdateAfterBindSamplers, bound.maxPerStageDescriptorUpdateAfterBindSamplers)
            < arraySamplers)
        {
            profile.mObstacle = std::format("{} samplers bound after use, under {}",
                std::min(
                    bound.maxDescriptorSetUpdateAfterBindSamplers, bound.maxPerStageDescriptorUpdateAfterBindSamplers),
                arraySamplers);
            return profile;
        }

        // The frame is held to `sLargestFrameSide` where its size is decided, before a device is
        // asked, so every device chosen must make an image that large.
        const std::uint32_t side = properties.mProperties2.properties.limits.maxImageDimension2D;
        if (side < sLargestFrameSide)
        {
            profile.mObstacle = std::format("images of {} pixels a side at most, under {}", side, sLargestFrameSide);
            return profile;
        }

        if (!family.has_value())
            profile.mObstacle = presents.empty()
                ? "no queue family with graphics and compute"
                : "no queue family with graphics and compute that presents to the window";

        return profile;
    }

    bool PhysicalDevice::hasOptionalExtension(const char* name) const
    {
        const std::vector<const char*>& offered = mProfile.mOptionalExtensions;

        return std::any_of(offered.begin(), offered.end(),
            [name](const char* const listed) { return std::strcmp(listed, name) == 0; });
    }

    std::optional<std::size_t> chooseDevice(const std::span<const SurveyedDevice> devices)
    {
        std::optional<std::size_t> chosen;
        for (std::size_t at = 0; at < devices.size(); ++at)
        {
            const SurveyedDevice& device = devices[at];
            if (!device.mObstacle.empty())
                continue;
            if (device.mDiscrete)
                return at;
            if (!chosen.has_value())
                chosen = at;
        }
        return chosen;
    }

    std::string describeSurvey(const std::span<const SurveyedDevice> devices, const std::optional<std::size_t> chosen)
    {
        if (chosen.has_value())
        {
            assert(*chosen < devices.size() && "a choice past the devices");
            assert(devices[*chosen].mObstacle.empty() && "a device chosen that something stops");
        }

        std::string out;
        for (std::size_t at = 0; at < devices.size(); ++at)
        {
            const SurveyedDevice& device = devices[at];
            const std::string verdict = [&]() -> std::string {
                if (!device.mObstacle.empty())
                    return "refused, " + device.mObstacle;
                assert(chosen.has_value() && "a device nothing stops beside no choice");
                if (*chosen == at)
                    return "chosen";
                return devices[*chosen].mDiscrete && !device.mDiscrete ? "passed over for a discrete device"
                                                                       : "passed over for one listed before it";
            }();
            out += std::format("{}  {}: {}", at == 0 ? "" : "\n", device.mName, verdict);
            // The chosen device's details are its description's, which a renderer made on it logs whole.
            if (chosen != at)
                out += std::format(" ({})", device.mDetails);
        }
        return out;
    }

    PhysicalDevice PhysicalDevice::select(VkInstance instance, const PresentQuery& presents)
    {
        // **The loader answers `VK_ERROR_INITIALIZATION_FAILED` where none of its drivers found a
        // device** (`setup_loader_term_phys_devs`), as with a driver installed for a card the
        // machine lacks: a machine to skip, as an empty list is, and no fault.
        std::uint32_t counted = 0;
        if (vkEnumeratePhysicalDevices(instance, &counted, nullptr) == VK_ERROR_INITIALIZATION_FAILED)
            throw Unsupported("no Vulkan device is installed");

        const std::vector<VkPhysicalDevice> handles = enumerateVk<VkPhysicalDevice>(
            "vkEnumeratePhysicalDevices", [&](std::uint32_t* count, VkPhysicalDevice* into) {
                return vkEnumeratePhysicalDevices(instance, count, into);
            });
        if (handles.empty())
            throw Unsupported("no Vulkan device is installed");

        std::vector<Candidate> candidates(handles.size());
        std::vector<SurveyedDevice> surveyed(handles.size());
        for (std::size_t at = 0; at < handles.size(); ++at)
        {
            // **A device that answers a question with an error is rejected, and the rest asked**:
            // a broken second driver or a software rasterizer short of memory ended selection for
            // every card in the machine, and as a fault to report where it is a machine to skip.
            try
            {
                candidates[at] = examine(handles[at], presents);
            }
            catch (const DeviceError& failure)
            {
                VkPhysicalDeviceProperties plain{};
                vkGetPhysicalDeviceProperties(handles[at], &plain);
                surveyed[at] = SurveyedDevice{ .mName = plain.deviceName,
                    .mDetails = detailsOf(plain, nullptr, nullptr),
                    .mObstacle = std::string("answered a question with an error, ") + failure.what(),
                    .mDiscrete = plain.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU };
                continue;
            }

            const DeviceProperties& found = *candidates[at].mProperties;
            const VkPhysicalDeviceProperties& base = found.mProperties2.properties;
            surveyed[at] = SurveyedDevice{ .mName = base.deviceName,
                .mDetails = detailsOf(base, &found.mVulkan12, &found.mMemory),
                .mObstacle = candidates[at].mProfile.mObstacle,
                .mDiscrete = base.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU };
        }

        const std::optional<std::size_t> chosen = chooseDevice(surveyed);
        const std::string survey = describeSurvey(surveyed, chosen);
        if (!chosen.has_value())
            throw Unsupported("no Vulkan device meets this renderer's requirements:\n" + survey);
        Log(Debug::Info) << "Vulkan devices\n" << survey;

        PhysicalDevice best;
        best.mHandle = handles[*chosen];
        best.mProperties = std::move(candidates[*chosen].mProperties);
        best.mProfile = std::move(candidates[*chosen].mProfile);

        // Every report from here on names the card and the driver it ran on.
        const DeviceProperties& properties = *best.mProperties;
        Crash::annotate("device", properties.mProperties2.properties.deviceName);
        Crash::annotate("driver",
            std::string(properties.mVulkan12.driverName) + " " + std::string(properties.mVulkan12.driverInfo));

        return best;
    }

    double PhysicalDevice::timestampMs(const std::uint64_t ticks) const
    {
        return static_cast<double>(ticks)
            * static_cast<double>(mProperties->mProperties2.properties.limits.timestampPeriod) / 1.0e6;
    }

    CardAddress PhysicalDevice::getAddress() const
    {
        CardAddress address{ .mVendor = mProperties->mProperties2.properties.vendorID, .mPci = std::nullopt };
        if (const std::optional<VkPhysicalDevicePCIBusInfoPropertiesEXT>& bus = mProperties->mPciBus)
            address.mPci = PciAddress{
                .mDomain = bus->pciDomain, .mBus = bus->pciBus, .mDevice = bus->pciDevice, .mFunction = bus->pciFunction
            };
        return address;
    }

    std::string PhysicalDevice::describe() const
    {
        const VkPhysicalDeviceProperties& base = mProperties->mProperties2.properties;
        const VkPhysicalDeviceAccelerationStructurePropertiesKHR& as = mProperties->mAccelerationStructure;

        std::ostringstream out;
        out << "device:            " << base.deviceName << " (" << identityOf(base);
        if (const std::optional<VkPhysicalDevicePCIBusInfoPropertiesEXT>& bus = mProperties->mPciBus)
            out << std::format(
                ", PCI {:04x}:{:02x}:{:02x}.{:x}", bus->pciDomain, bus->pciBus, bus->pciDevice, bus->pciFunction);
        out << ")\n"
            << "driver:            " << mProperties->mVulkan12.driverName << ' ' << mProperties->mVulkan12.driverInfo
            << '\n'
            << "Vulkan:            " << versionString(base.apiVersion) << '\n'
            << "device-local heap: " << std::format("{:.1f}", mebibytes(sumDeviceLocalHeaps(mProperties->mMemory)))
            << " MiB\n"
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
