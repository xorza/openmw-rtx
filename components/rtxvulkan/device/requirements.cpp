#include "requirements.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>
#include <components/rtxvulkan/shaders/shared/tone.h>

namespace Rtx
{
    namespace
    {
        template <class T>
        void chain(void*& next, T& structure, VkStructureType type)
        {
            structure.sType = type;
            structure.pNext = next;
            next = &structure;
        }

        constexpr std::array<const char*, 8> sRequiredDeviceExtensions{
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            // The launches the fog and the sprites trace from, a ray generation stage each that
            // casts its rays as ray queries (`TracePipeline`).
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            // Occupancy is a register count the driver's own compiler owns and no offline tool has.
            // Required, because a renderer that quietly reported nothing would be a fallback path.
            VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
            // A clock a shader reads that runs at one rate whatever the card is clocked at, which
            // is what `stress.comp` holds a queue against. Required and not optional, because a
            // hold that fell back to a count would be the one that misses on the first frames of
            // every run — the frames the hold is for.
            VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
        };

        constexpr std::array sFaultReport{ VK_EXT_DEVICE_FAULT_EXTENSION_NAME };
        constexpr std::array sMemoryBudget{ VK_EXT_MEMORY_BUDGET_EXTENSION_NAME };
        constexpr std::array sMemoryPriority{ VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME };
        constexpr std::array sPageableMemory{ VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME };
        constexpr std::array sPresentFences{ VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME };
        constexpr std::array sPresentIds{ VK_KHR_PRESENT_ID_EXTENSION_NAME };
        constexpr std::array sPresentWait{ VK_KHR_PRESENT_WAIT_EXTENSION_NAME };
        constexpr std::array sCheckpoints{ VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME };
        constexpr std::array sBufferMarkers{ VK_AMD_BUFFER_MARKER_EXTENSION_NAME };

        /// What the registry says a present fence rests on beside core 1.4: the swapchain, which a
        /// device takes only where its instance has a surface, and the instance's half of swapchain
        /// maintenance.
        constexpr std::array sMaintainedSwapchain{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        };

        /// What the registry says a present's id rests on, and a wait for one beside it.
        constexpr std::array sSwapchain{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        constexpr std::array sIdentifiedSwapchain{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME };

        template <class T>
        VkBaseOutStructure& asBase(T& structure)
        {
            return *reinterpret_cast<VkBaseOutStructure*>(&structure);
        }

        constexpr OptionalFeature sFaultFeature{
            .mStructure = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mFault); },
            .mField = [](OptionalFeatures& features) -> VkBool32& { return features.mFault.deviceFault; },
        };
        constexpr OptionalFeature sPriorityFeature{
            .mStructure = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mPriority); },
            .mField = [](OptionalFeatures& features) -> VkBool32& { return features.mPriority.memoryPriority; },
        };
        constexpr OptionalFeature sPageableFeature{
            .mStructure = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mPageable); },
            .mField
            = [](OptionalFeatures& features) -> VkBool32& { return features.mPageable.pageableDeviceLocalMemory; },
        };
        constexpr OptionalFeature sPresentFencesFeature{
            .mStructure
            = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mPresentFences); },
            .mField
            = [](OptionalFeatures& features) -> VkBool32& { return features.mPresentFences.swapchainMaintenance1; },
        };

        constexpr OptionalFeature sPresentIdsFeature{
            .mStructure
            = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mPresentIds); },
            .mField = [](OptionalFeatures& features) -> VkBool32& { return features.mPresentIds.presentId; },
        };
        constexpr OptionalFeature sPresentWaitFeature{
            .mStructure
            = [](OptionalFeatures& features) -> VkBaseOutStructure& { return asBase(features.mPresentWait); },
            .mField = [](OptionalFeatures& features) -> VkBool32& { return features.mPresentWait.presentWait; },
        };

        constexpr std::array sOptionalExtensions{
            // Turns a device loss from "the driver said no" into where it faulted: the addresses,
            // how precisely they are known, and what the vendor adds. `Device::describeFault` is
            // what reads it.
            OptionalExtensions{ .mOption = DeviceOption::FaultReport,
                .mExtensions = sFaultReport,
                .mNeeds = {},
                .mFeature = &sFaultFeature },
            // What the driver says is left, which the heap's own size does not. A budget moves
            // with whatever else is on the card, and it is the figure a residency decision belongs
            // against — most of all on a card whose host-visible heap is a couple of hundred
            // megabytes. `MemoryAllocator` is what reads it.
            OptionalExtensions{ DeviceOption::MemoryBudget, sMemoryBudget, {} },
            // A priority on each allocation, which a driver that has to move video memory out
            // under pressure moves the lowest of first: content below what the frame holds —
            // `MemoryAllocator` says why content as a whole and not by its use.
            OptionalExtensions{ .mOption = DeviceOption::MemoryPriority,
                .mExtensions = sMemoryPriority,
                .mNeeds = {},
                .mFeature = &sPriorityFeature },
            // The driver told that the application knows its memory can be paged, which is where
            // those priorities are what the operating system pages by. After the priority, which it
            // rests on.
            OptionalExtensions{ .mOption = DeviceOption::PageableMemory,
                .mExtensions = sPageableMemory,
                .mNeeds = sMemoryPriority,
                .mFeature = &sPageableFeature },
            // A fence the presentation engine signals, which is the only thing that says it has
            // finished with an image. `Presenter` retires its semaphores and its swapchain against
            // one where the driver has it, and against a device-idle where it does not.
            OptionalExtensions{ .mOption = DeviceOption::PresentFences,
                .mExtensions = sPresentFences,
                .mNeeds = sMaintainedSwapchain,
                .mFeature = &sPresentFencesFeature },
            // An id on each present, which is what a wait for one names. Read only for the wait.
            OptionalExtensions{ .mOption = DeviceOption::PresentIds,
                .mExtensions = sPresentIds,
                .mNeeds = sSwapchain,
                .mFeature = &sPresentIdsFeature },
            // A wait until a present is on the screen, which is what holds a frame's input under
            // FIFO to two frames behind what is shown, as DXVK's maximum frame latency does
            // (`Swapchain::awaitLatency`). Without it, nothing bounds the presents queued.
            OptionalExtensions{ .mOption = DeviceOption::PresentWait,
                .mExtensions = sPresentWait,
                .mNeeds = sIdentifiedSwapchain,
                .mFeature = &sPresentWaitFeature },
            // A marker the queue remembers passing, so a device loss names the last zone each
            // stage reached rather than an address: `GpuTimer::open` sets one per zone in a build
            // that names things, and `Device::describeFault` reads them back.
            OptionalExtensions{ DeviceOption::Checkpoints, sCheckpoints, {}, sDebugNames },
            // The same for the drivers that have no checkpoints: AMD's own and RADV. A number
            // written at a stage and not a pointer, so the host keeps what each number names
            // (`MarkerRing`).
            OptionalExtensions{ DeviceOption::BufferMarkers, sBufferMarkers, {}, sDebugNames },
        };

        /// A table out of `DeviceOption`'s order is an option read as another.
        constexpr bool everyOptionInItsPlace()
        {
            for (std::size_t at = 0; at < sOptionalExtensions.size(); ++at)
                if (static_cast<std::size_t>(sOptionalExtensions[at].mOption) != at)
                    return false;

            return sOptionalExtensions.size() == sDeviceOptions;
        }

        static_assert(everyOptionInItsPlace(), "the optional extensions are not in the order of their options");

        constexpr std::array sRequiredDeviceFeatures{
            RequiredFeature{
                "shaderInt64", +[](DeviceFeatures& f) -> VkBool32& { return f.mFeatures2.features.shaderInt64; } },

            // What lets a surface the eye sees be read along its footprint's long axis rather than
            // blurred to it: `textureGrad` through a sampler with anisotropy on (`makeFootprintSampler`).
            RequiredFeature{ "samplerAnisotropy",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mFeatures2.features.samplerAnisotropy; } },

            // What lets `composite.comp` read one binding that is two formats. The bounce it
            // composites is the trace's own channel where nothing denoised the frame and the
            // cascade's where something did, and those are `rgba32f` and `rgba16f` — so the shader
            // states no format at all and the load converts from whatever the view holds.
            RequiredFeature{ "shaderStorageImageReadWithoutFormat",
                +[](DeviceFeatures& f) -> VkBool32& {
                    return f.mFeatures2.features.shaderStorageImageReadWithoutFormat;
                } },

            // And the store's half: the trace writes the two radiance channels and the composite
            // writes the frame at whichever width the run chose — `Rtx::RadianceWidth` — so those
            // three declare no format either.
            RequiredFeature{ "shaderStorageImageWriteWithoutFormat",
                +[](DeviceFeatures& f) -> VkBool32& {
                    return f.mFeatures2.features.shaderStorageImageWriteWithoutFormat;
                } },

            RequiredFeature{ "bufferDeviceAddress",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.bufferDeviceAddress; } },

            // What lets the interface's and the debug lines' stages count into the census
            // (`census.glsl`), as every other stage does: an atomic outside a compute or a trace.
            RequiredFeature{ "vertexPipelineStoresAndAtomics",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mFeatures2.features.vertexPipelineStoresAndAtomics; } },
            RequiredFeature{ "fragmentStoresAndAtomics",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mFeatures2.features.fragmentStoresAndAtomics; } },

            // The queue's one clock, `Rtx::Timeline`.
            RequiredFeature{
                "timelineSemaphore", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.timelineSemaphore; } },
            RequiredFeature{
                "descriptorIndexing", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.descriptorIndexing; } },
            RequiredFeature{ "runtimeDescriptorArray",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.runtimeDescriptorArray; } },
            RequiredFeature{ "descriptorBindingPartiallyBound",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.descriptorBindingPartiallyBound; } },
            RequiredFeature{ "descriptorBindingSampledImageUpdateAfterBind",
                +[](DeviceFeatures& f) -> VkBool32& {
                    return f.mVulkan12.descriptorBindingSampledImageUpdateAfterBind;
                } },
            RequiredFeature{ "shaderSampledImageArrayNonUniformIndexing",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.shaderSampledImageArrayNonUniformIndexing; } },
            RequiredFeature{
                "scalarBlockLayout", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan12.scalarBlockLayout; } },

            RequiredFeature{
                "synchronization2", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan13.synchronization2; } },
            RequiredFeature{ "maintenance4", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan13.maintenance4; } },
            // What pins the trace's kernels to 32 lanes, the width AMD advises for ray queries on RDNA
            // (`SubgroupSize`). Vulkan 1.3 makes it mandatory.
            RequiredFeature{ "subgroupSizeControl",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan13.subgroupSizeControl; } },
            // What makes a trace workgroup of 32 lanes one subgroup and not two partial ones, which
            // the stages' ballots count it as (`SubgroupSize::ThirtyTwo`). Vulkan 1.3 makes it
            // mandatory as well.
            RequiredFeature{ "computeFullSubgroups",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan13.computeFullSubgroups; } },
            // The GUI is the only thing here that rasterises, and it does so without a render pass
            // or a framebuffer object.
            RequiredFeature{
                "dynamicRendering", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan13.dynamicRendering; } },

            RequiredFeature{ "maintenance5", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan14.maintenance5; } },
            RequiredFeature{
                "pushDescriptor", +[](DeviceFeatures& f) -> VkBool32& { return f.mVulkan14.pushDescriptor; } },

            RequiredFeature{ "accelerationStructure",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mAccelerationStructure.accelerationStructure; } },
            RequiredFeature{ "rayQuery", +[](DeviceFeatures& f) -> VkBool32& { return f.mRayQuery.rayQuery; } },
            RequiredFeature{ "rayTracingPositionFetch",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mPositionFetch.rayTracingPositionFetch; } },
            RequiredFeature{ "rayTracingMaintenance1",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mRayTracingMaintenance1.rayTracingMaintenance1; } },
            RequiredFeature{ "rayTracingPipeline",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mRayTracingPipeline.rayTracingPipeline; } },
            // What `VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR` is gated on: the promise
            // every launch makes that no procedural geometry is anywhere in the scene.
            RequiredFeature{ "rayTraversalPrimitiveCulling",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mRayTracingPipeline.rayTraversalPrimitiveCulling; } },
            RequiredFeature{ "pipelineExecutableInfo",
                +[](DeviceFeatures& f) -> VkBool32& { return f.mPipelineExecutable.pipelineExecutableInfo; } },
            RequiredFeature{
                "shaderDeviceClock", +[](DeviceFeatures& f) -> VkBool32& { return f.mShaderClock.shaderDeviceClock; } },
        };
    }

    namespace
    {
        constexpr std::array sRequiredFormats{
            // The shading estimate is written by a dispatch straight into the sixteen-bit map the
            // trace samples — `ShadingPass` — and a sixteen-bit unorm as a storage image is one of
            // the features Vulkan leaves optional.
            RequiredFormat{
                toVulkanFormat(SHADING_MAP_FORMAT), VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "the shading map's dispatch" },
            // And a normal map's spread into its byte, the same way — `NormalSpreadPass`.
            RequiredFormat{ toVulkanFormat(NORMAL_SPREAD_FORMAT), VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
                "the normal spread's dispatch" },
            // And the curve's sixteen-bit picture of a summed frame, the same way — `TONE_DEEP_FORMAT`.
            RequiredFormat{ toVulkanFormat(TONE_DEEP_FORMAT), VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
                "the summed picture's sixteen bits" },
        };
    }

    std::span<const RequiredFormat> getRequiredFormats()
    {
        return sRequiredFormats;
    }

    std::span<const RequiredImage> getRequiredTextureImages()
    {
        static const std::vector<RequiredImage> images = [] {
            std::vector<RequiredImage> made;
            const auto add = [&](VkFormat format, VkImageUsageFlags usage, VkImageCreateFlags flags, std::string what) {
                const bool held = std::ranges::any_of(made, [&](const RequiredImage& image) {
                    return image.mFormat == format && image.mUsage == usage && image.mFlags == flags;
                });
                if (!held)
                    made.push_back(RequiredImage{ format, usage, flags, std::move(what) });
            };

            for (std::size_t at = 0; at < sTextureFormatCount; ++at)
            {
                const auto format = static_cast<TextureFormat>(at);
                if (!isUploadable(format))
                    continue;
                const std::string named(traitsOf(format).mName);
                add(toVulkanFormat(format), sUploadedTextureUsage, 0, "files in " + named);
                add(withoutCurve(toVulkanFormat(format)), sUploadedTextureUsage, 0, "chains completed from " + named);
            }

            const VkFormat written = toVulkanFormat(TEXTURE_WRITTEN_FORMAT);
            add(withCurve(written), sWrittenTextureUsage,
                VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
                "textures written under a curve");
            add(written, sWrittenTextureUsage, 0, "textures written");
            return made;
        }();
        return images;
    }

    DeviceFeatures::DeviceFeatures()
    {
        void* next = nullptr;
        chain(next, mShaderClock, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR);
        chain(next, mPipelineExecutable, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR);
        chain(next, mRayTracingPipeline, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
        chain(next, mRayTracingMaintenance1, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR);
        chain(next, mPositionFetch, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR);
        chain(next, mRayQuery, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
        chain(next, mAccelerationStructure, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
        chain(next, mVulkan14, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES);
        chain(next, mVulkan13, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        chain(next, mVulkan12, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        chain(next, mFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    }

    OptionalFeatures::OptionalFeatures()
    {
        // Each is chained where the device took its option, and not here: a structure in the chain
        // whose extension is not enabled is one the driver was never told to expect.
        mFault.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        mPriority.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
        mPageable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
        mPresentFences.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
        mPresentIds.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        mPresentWait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
    }

    DeviceProperties::DeviceProperties()
    {
        void* next = nullptr;
        chain(next, mRayTracingPipeline, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
        chain(next, mAccelerationStructure, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
        chain(next, mVulkan13, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES);
        chain(next, mVulkan12, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
        chain(next, mVulkan11, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);
        chain(next, mProperties2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    }

    std::span<const char* const> getRequiredDeviceExtensions()
    {
        return sRequiredDeviceExtensions;
    }

    std::span<const OptionalExtensions> getOptionalExtensions()
    {
        return sOptionalExtensions;
    }

    std::string versionString(std::uint32_t version)
    {
        return std::to_string(VK_API_VERSION_MAJOR(version)) + '.' + std::to_string(VK_API_VERSION_MINOR(version)) + '.'
            + std::to_string(VK_API_VERSION_PATCH(version));
    }

    std::span<const RequiredFeature> getRequiredDeviceFeatures()
    {
        return sRequiredDeviceFeatures;
    }

    void requestRequiredFeatures(DeviceFeatures& features)
    {
        for (const RequiredFeature& required : sRequiredDeviceFeatures)
            required.mField(features) = VK_TRUE;
    }

    void findMissingFeatures(DeviceFeatures& supported, std::vector<std::string_view>& missing)
    {
        for (const RequiredFeature& required : sRequiredDeviceFeatures)
            if (required.mField(supported) == VK_FALSE)
                missing.push_back(required.mName);
    }
}
