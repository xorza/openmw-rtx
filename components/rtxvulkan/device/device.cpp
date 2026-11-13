#include "device.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/platform/gpu.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

#include "buffermarkers.hpp"
#include "commands.hpp"
#include "extensionfilter.hpp"
#include "graveyard.hpp"
#include "instance.hpp"
#include "notfinitecensus.hpp"
#include "pipelinecache.hpp"
#include "pipelinecode.hpp"
#include "requirements.hpp"
#include "result.hpp"
#include "timeline.hpp"
#include "zonecounters.hpp"

namespace Rtx
{
    namespace
    {
        /// Which stage a checkpoint was reported for, for the handful a queue reports on and
        /// the number for the rest.
        std::string checkpointStageName(const VkPipelineStageFlagBits stage)
        {
            switch (stage)
            {
                case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
                    return "the top of the pipe";
                case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
                    return "the bottom of the pipe";
                case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
                    return "the compute stage";
                case VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR:
                    return "the ray tracing stage";
                case VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR:
                    return "the structure build stage";
                case VK_PIPELINE_STAGE_TRANSFER_BIT:
                    return "the transfer stage";
                default:
                    return std::format("stage {:#x}", static_cast<std::uint32_t>(stage));
            }
        }

        /// What a faulting address was being used for, as the header spells it.
        std::string_view faultAddressTypeName(VkDeviceFaultAddressTypeEXT type)
        {
            switch (type)
            {
                case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
                    return "a fault at no address";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
                    return "an invalid read";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
                    return "an invalid write";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
                    return "an invalid execute";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
                    return "an instruction pointer the driver could not place";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
                    return "an instruction pointer outside any shader";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
                    return "the instruction that faulted";
                default:
                    return "an address of unrecognised type";
            }
        }

        void requireDispatch(VkDevice device, const char* name)
        {
            if (vkGetDeviceProcAddr(device, name) == nullptr)
                throw Unsupported(
                    std::string("the driver advertises the extension providing ") + name + " but does not dispatch it");
        }

        template <class T>
        void load(VkDevice device, T& out, const char* name)
        {
            requireDispatch(device, name);
            out = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
        }
    }

    Device::Device(const Instance& instance, PhysicalDevice&& physicalDevice, const ShaderSet& shaders,
        const PipelineCacheSpec& cache, const DeviceReport& report)
        : mPhysicalDevice(std::move(physicalDevice))
        , mInstance(instance.getHandle())
        , mWatchesSubmits(instance.watchesSubmits())
        , mShaders(shaders)
        , mReport(report)
    {
        const Crash::NoteScope noted("making the device");
        // `ShaderSet::find` and the census's words both read the modules in name order, each name once.
        Crash::contract(
            std::ranges::adjacent_find(mShaders.mModules, std::ranges::greater_equal{}, &ShaderModule::mName)
                == mShaders.mModules.end(),
            "a shader set out of name order, or naming a module twice");
        if (!mReport.mCodeFolder.empty())
            mCode = std::make_unique<PipelineCodeFolder>(mReport.mCodeFolder);

        // **What else needs the device is asked before it exists**, since an extension cannot be enabled
        // after `vkCreateDevice`. **Only what the device offers is enabled**: an extension the driver
        // lists but the card lacks makes `vkCreateDevice` fail. Asked of the device itself, as
        // `hasOptionalExtension` knows only the renderer's own optional list.
        std::vector<std::string> askedExtensions;
        if (mReport.mAskedExtensions != nullptr)
        {
            const std::vector<VkExtensionProperties> offered = enumerateVk<VkExtensionProperties>(
                "vkEnumerateDeviceExtensionProperties", [&](std::uint32_t* count, VkExtensionProperties* into) {
                    return vkEnumerateDeviceExtensionProperties(mPhysicalDevice.getHandle(), nullptr, count, into);
                });
            std::vector<std::string_view> offeredNames;
            offeredNames.reserve(offered.size());
            for (const VkExtensionProperties& extension : offered)
                offeredNames.emplace_back(extension.extensionName);
            askedExtensions = filterOfferedExtensions(mReport.mAskedExtensions(instance.getHandle(),
                                                          mPhysicalDevice.getHandle(), mPhysicalDevice.getDriver()),
                offeredNames);
        }

        const std::span<const char* const> required = getRequiredDeviceExtensions();
        std::vector<const char*> extensions(required.begin(), required.end());
        for (const std::string& extension : askedExtensions)
            if (std::ranges::none_of(
                    extensions, [&](const char* held) { return std::strcmp(held, extension.c_str()) == 0; }))
                extensions.push_back(extension.c_str());

        // The swapchain rests on the surface, which the instance loads for a window and never
        // headless: one fact, stated where the instance was made, and read here rather than said
        // to the device a second time.
        if (instance.hasExtension(VK_KHR_SURFACE_EXTENSION_NAME))
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        // **An option is taken whole or not at all**: where the device offers every extension of it, has
        // the feature it is no use without, and every extension it needs is the instance's. Its feature
        // is asked before anything is enabled and its needs are met after, so an option offered without
        // its feature enables nothing and takes down what rests on it. What a driver offers is not what a
        // device may enable: a present fence is offered to a device with no window, and rests on a
        // swapchain it has none of.
        std::array<bool, sDeviceOptions> taken{};
        for (const OptionalExtensions& option : getOptionalExtensions())
            taken[static_cast<std::size_t>(option.mOption)]
                = std::all_of(option.mExtensions.begin(), option.mExtensions.end(),
                    [&](const char* const name) { return mPhysicalDevice.hasOptionalExtension(name); });
        taken[static_cast<std::size_t>(DeviceOption::PerformanceQuery)] &= mReport.mCounters;
        taken[static_cast<std::size_t>(DeviceOption::PresentFencesExt)]
            &= !taken[static_cast<std::size_t>(DeviceOption::PresentFences)];
        taken[static_cast<std::size_t>(DeviceOption::Checkpoints)] &= mReport.mCheckpoints;
        taken[static_cast<std::size_t>(DeviceOption::BufferMarkers)] &= mReport.mMarkers;
        taken[static_cast<std::size_t>(DeviceOption::UncachedMarkers)] &= mReport.mMarkers;
        const auto has = [&](const DeviceOption option) { return taken[static_cast<std::size_t>(option)]; };

        // Only what is still taken is chained, into the query and into the creation alike.
        OptionalFeatures optional;
        VkBaseOutStructure* asked = nullptr;
        const auto chain = [&asked](VkBaseOutStructure& structure) {
            structure.pNext = asked;
            asked = &structure;
        };

        for (const OptionalExtensions& option : getOptionalExtensions())
            if (option.mFeature != nullptr && has(option.mOption))
                chain(option.mFeature->mStructure(optional));

        if (asked != nullptr)
        {
            VkPhysicalDeviceFeatures2 offered{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = asked, .features = {}
            };
            vkGetPhysicalDeviceFeatures2(mPhysicalDevice.getHandle(), &offered);

            // The vendor binary is not asked for: nothing here could read it, and a feature enabled
            // for nothing is a feature to explain.
            optional.mFault.deviceFaultVendorBinary = VK_FALSE;
        }

        for (const OptionalExtensions& option : getOptionalExtensions())
            if (option.mFeature != nullptr && has(option.mOption) && option.mFeature->mField(optional) != VK_TRUE)
                taken[static_cast<std::size_t>(option.mOption)] = false;

        dropUnmetNeeds(getOptionalExtensions(), taken, [&](const char* const name) {
            return instance.hasExtension(name)
                || std::any_of(extensions.begin(), extensions.end(),
                    [&](const char* const held) { return std::strcmp(held, name) == 0; });
        });

        for (const OptionalExtensions& option : getOptionalExtensions())
            if (has(option.mOption))
                extensions.insert(extensions.end(), option.mExtensions.begin(), option.mExtensions.end());
        // What was taken, where `PhysicalDevice::describe` says what was offered.
        Log(Debug::Info) << "Device options taken: " << describeTaken(taken);

        DeviceFeatures features;
        requestRequiredFeatures(features);

        // **A query reset from the host, for the counters alone**, which a command buffer that begins
        // a performance query may not reset (`GpuTimer::beginFrame`). Every 1.2 device has it, so
        // asking needs no check; asked only here, so a device that reads no counter is made as before.
        if (has(DeviceOption::PerformanceQuery))
            features.mVulkan12.hostQueryReset = VK_TRUE;

        // The same chain again, of what was taken, in front of the features the renderer requires.
        asked = reinterpret_cast<VkBaseOutStructure*>(&features.mFeatures2);
        for (const OptionalExtensions& option : getOptionalExtensions())
            if (option.mFeature != nullptr && has(option.mOption))
                chain(option.mFeature->mStructure(optional));

        const bool describesFault = has(DeviceOption::FaultReport);
        mPresentFences = has(DeviceOption::PresentFences) || has(DeviceOption::PresentFencesExt);
        mPresentWait = has(DeviceOption::PresentIds) && has(DeviceOption::PresentWait);

        const float priority = 1.0f;
        const VkDeviceQueueCreateInfo queue{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = mPhysicalDevice.getQueueFamily(),
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };

        const VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = asked,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            // Superseded by the VkPhysicalDeviceFeatures2 in the chain, and the two cannot both be set.
            .pEnabledFeatures = nullptr,
        };

        VkDevice created = VK_NULL_HANDLE;
        checkVkWithoutDevice(
            vkCreateDevice(mPhysicalDevice.getHandle(), &createInfo, nullptr, &created), "vkCreateDevice");
        mHandle = Root<VkDevice, vkDestroyDevice>(created);

        // From here on a throw — a driver that advertises an extension it cannot dispatch, which
        // a load below reports — destroys the members already made, in reverse, and the device's
        // own handle last of all, which is what `LogicalDevice` is for.
        vkGetDeviceQueue(mHandle.get(), mPhysicalDevice.getQueueFamily(), 0, &mQueue);

        // The required extensions' entry points are volk's, as every other call is, and asked of
        // the device once here, so a driver that advertises an extension it cannot dispatch fails
        // at start-up and not at its first structure.
        for (const char* const name : { "vkGetAccelerationStructureBuildSizesKHR", "vkCreateAccelerationStructureKHR",
                 "vkDestroyAccelerationStructureKHR", "vkCmdBuildAccelerationStructuresKHR",
                 "vkCmdWriteAccelerationStructuresPropertiesKHR", "vkCmdCopyAccelerationStructureKHR",
                 "vkGetAccelerationStructureDeviceAddressKHR", "vkCreateRayTracingPipelinesKHR",
                 "vkGetRayTracingShaderGroupHandlesKHR", "vkCmdTraceRaysKHR", "vkGetRayTracingShaderGroupStackSizeKHR",
                 "vkGetPipelineExecutablePropertiesKHR", "vkGetPipelineExecutableStatisticsKHR",
                 "vkGetPipelineExecutableInternalRepresentationsKHR" })
            requireDispatch(mHandle.get(), name);

        if (describesFault)
            load(mHandle.get(), mGetDeviceFaultInfo, "vkGetDeviceFaultInfoEXT");

        if (has(DeviceOption::Checkpoints))
        {
            load(mHandle.get(), mCmdSetCheckpoint, "vkCmdSetCheckpointNV");
            load(mHandle.get(), mGetQueueCheckpointData, "vkGetQueueCheckpointDataNV");
        }

        PFN_vkCmdWriteBufferMarkerAMD markerWrite = nullptr;
        if (has(DeviceOption::BufferMarkers))
            load(mHandle.get(), markerWrite, "vkCmdWriteBufferMarkerAMD");

        if (instance.hasDebugUtils())
        {
            mSetObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                vkGetDeviceProcAddr(mHandle.get(), "vkSetDebugUtilsObjectNameEXT"));
            mBeginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
                vkGetDeviceProcAddr(mHandle.get(), "vkCmdBeginDebugUtilsLabelEXT"));
            mEndLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
                vkGetDeviceProcAddr(mHandle.get(), "vkCmdEndDebugUtilsLabelEXT"));
        }

        mPipelineCache = std::make_unique<PipelineCache>(
            mHandle.get(), mPhysicalDevice.getProperties().mProperties2.properties, cache, mShaders.mDigest);
        mMemory = std::make_unique<MemoryAllocator>(*this, instance.getHandle(), mPhysicalDevice.getHandle(),
            mPhysicalDevice.getProperties().mMemory,
            MemoryExtensions{ .mBudget = has(DeviceOption::MemoryBudget),
                .mPriority = has(DeviceOption::MemoryPriority),
                .mDeviceCoherent = has(DeviceOption::UncachedMarkers) });
        mTimeline = std::make_unique<Timeline>(*this);
        mGraveyard = std::make_unique<Graveyard>(*this);
        // `new` and not `make_unique`, which the pool's private constructor does not admit.
        mPool.reset(new CommandPool(*this));

        if (markerWrite != nullptr)
            mMarkers = std::make_unique<BufferMarkers>(*this, markerWrite, has(DeviceOption::UncachedMarkers));

        if (shaders.mCensus)
            mCensus = std::make_unique<NotFiniteCensus>(*this, mShaders);

        if (has(DeviceOption::PerformanceQuery))
            mZoneCounters = std::make_unique<ZoneCounters>(*this);
        else if (mReport.mCounters)
            Log(Debug::Warning) << "zone counters: the driver offers no VK_KHR_performance_query";
    }

    VkDevice vulkanHandleOf(const Device& device)
    {
        return device.getHandle();
    }

    Device::~Device()
    {
        // Through the wrapper and so through `tearDown`, which is what keeps the fault description
        // a lost device carries: the raw call answered with a number nobody read. The members
        // then go in the order they are declared for.
        tearDown("the device would not finish before it was destroyed", [&] { waitIdle(); });
    }

    MemoryAllocator& Device::getMemory() const
    {
        return *mMemory;
    }

    VkPipelineCache Device::getPipelineCache() const
    {
        return mPipelineCache->getHandle();
    }

    void Device::savePipelineCache()
    {
        mPipelineCache->save();
    }

    namespace
    {
        void raise(std::optional<std::uint64_t>& most, const std::uint64_t value)
        {
            most = std::max(most.value_or(0), value);
        }

        std::uint64_t unsignedOf(const VkPipelineExecutableStatisticKHR& statistic)
        {
            switch (statistic.format)
            {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                    return statistic.value.i64 > 0 ? static_cast<std::uint64_t>(statistic.value.i64) : 0;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                    return statistic.value.u64;
                default:
                    return 0;
            }
        }
    }

    std::uint32_t Device::executablesOf(const VkPipeline pipeline) const
    {
        const VkPipelineInfoKHR asked{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
        };

        std::uint32_t executables = 0;
        checkVk(*this, vkGetPipelineExecutablePropertiesKHR(mHandle.get(), &asked, &executables, nullptr),
            "vkGetPipelineExecutablePropertiesKHR");
        return executables;
    }

    std::vector<VkPipelineExecutableStatisticKHR> Device::statisticsOf(
        const VkPipeline pipeline, const std::uint32_t executable) const
    {
        const VkPipelineExecutableInfoKHR which{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
            .executableIndex = executable,
        };

        return enumerateVk<VkPipelineExecutableStatisticKHR>(
            *this, "vkGetPipelineExecutableStatisticsKHR",
            [&](std::uint32_t* count, VkPipelineExecutableStatisticKHR* into) {
                return vkGetPipelineExecutableStatisticsKHR(mHandle.get(), &which, count, into);
            },
            VkPipelineExecutableStatisticKHR{ .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR,
                .pNext = nullptr,
                .name = {},
                .description = {},
                .format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR,
                .value = {} });
    }

    CompiledUse Device::compiledUseOf(const VkPipeline pipeline, const VkPipelineBindPoint point) const
    {
        CompiledUse use;
        if (point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            use.mStackBytes = vkGetRayTracingShaderGroupStackSizeKHR(
                mHandle.get(), pipeline, 0, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
        if (mReport.mStatistics == PipelineStatistics::Skipped)
            return use;

        const VkPipelineInfoKHR asked{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
        };
        const std::vector<VkPipelineExecutablePropertiesKHR> executables
            = enumerateVk<VkPipelineExecutablePropertiesKHR>(
                *this, "vkGetPipelineExecutablePropertiesKHR",
                [&](std::uint32_t* count, VkPipelineExecutablePropertiesKHR* into) {
                    return vkGetPipelineExecutablePropertiesKHR(mHandle.get(), &asked, count, into);
                },
                VkPipelineExecutablePropertiesKHR{ .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR,
                    .pNext = nullptr,
                    .stages = 0,
                    .name = {},
                    .description = {},
                    .subgroupSize = 0 });
        const StatisticNames& names = mPhysicalDevice.getDriver().mStatistics;
        for (std::uint32_t executable = 0; executable < executables.size(); ++executable)
        {
            use.mSubgroupSize = std::max(use.mSubgroupSize.value_or(0), executables[executable].subgroupSize);
            for (const VkPipelineExecutableStatisticKHR& statistic : statisticsOf(pipeline, executable))
            {
                const std::string_view name = statistic.name;
                const std::uint64_t value = unsignedOf(statistic);
                if (name == names.mScratch)
                    raise(use.mScratchBytes, value & names.mScratchBits);
                else if (name == names.mRegisters)
                    raise(use.mRegisters, value);
                else if (name == names.mShared)
                    raise(use.mSharedBytes, value);
            }
        }
        return use;
    }

    void Device::reportPipeline(VkPipeline pipeline, std::string_view name, const std::optional<double> compileMs) const
    {
        if (compileMs.has_value())
            Log(Debug::Verbose) << "pipeline " << name << ": compiled in " << *compileMs << " ms";
        if (mReport.mStatistics == PipelineStatistics::Skipped && mCode == nullptr)
            return;

        const std::uint32_t executables = executablesOf(pipeline);
        const std::uint32_t copy = mCode != nullptr ? mCode->claim(name) : 0;

        // Said once per pipeline rather than left as a missing line: NVIDIA's compiler reports no executable
        // for a ray tracing pipeline and no internal representation for any, so a reader could not otherwise
        // tell it from a call nobody made. Nsight Graphics is where that is read.
        if (executables == 0)
        {
            Log(Debug::Verbose) << "pipeline " << name << ": the driver reports no executable";
            return;
        }

        for (std::uint32_t executable = 0; executable < executables; ++executable)
        {
            if (mCode != nullptr)
                writeCode(pipeline, name, copy, executable, executables);
            if (mReport.mStatistics == PipelineStatistics::Skipped)
                continue;

            const std::vector<VkPipelineExecutableStatisticKHR> statistics = statisticsOf(pipeline, executable);

            // Whatever the driver chose to say, and not a list this side picked: the names are the
            // compiler's own, so a fixed set of fields would go empty on the next driver.
            // `openmw-rtxtool` prints them verbatim.
            std::string line;
            for (const VkPipelineExecutableStatisticKHR& statistic : statistics)
            {
                if (!line.empty())
                    line += ", ";
                line += statistic.name;
                line += ' ';

                switch (statistic.format)
                {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                        line += statistic.value.b32 != VK_FALSE ? "yes" : "no";
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                        line += std::to_string(statistic.value.i64);
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                        line += std::to_string(statistic.value.u64);
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                        line += std::to_string(statistic.value.f64);
                        break;
                    default:
                        line += '?';
                        break;
                }
            }

            Log(Debug::Verbose) << "pipeline " << name << ": " << line;

            // **A spill said where a player's log keeps it**: a kernel that spills runs at the speed of its
            // scratch memory, which took a frame on an integrated card from milliseconds to the better part
            // of a second (`docs/research/2026-10-21-amd-integrated-stage-times.md`).
            const StatisticNames& names = mPhysicalDevice.getDriver().mStatistics;
            for (const VkPipelineExecutableStatisticKHR& statistic : statistics)
            {
                const std::uint64_t scratch = unsignedOf(statistic) & names.mScratchBits;
                if (!names.mScratch.empty() && std::string_view(statistic.name) == names.mScratch && scratch > 0)
                    Log(Debug::Warning) << "pipeline " << name << " uses " << scratch
                                        << " bytes of scratch memory a lane";
            }
        }
    }

    void Device::writeCode(const VkPipeline pipeline, const std::string_view name, const std::uint32_t copy,
        const std::uint32_t executable, const std::uint32_t executables) const
    {
        const VkPipelineExecutableInfoKHR which{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
            .executableIndex = executable,
        };
        const auto represent = [&](std::uint32_t* count, VkPipelineExecutableInternalRepresentationKHR* into) {
            return vkGetPipelineExecutableInternalRepresentationsKHR(mHandle.get(), &which, count, into);
        };

        // Asked twice: once for the forms and the size of each, and again with room for the bodies of
        // the text forms alone, which are all that is written. **A form not asked for keeps a null
        // `pData`**, which the second call answers with its size and no body: AMD's Windows driver
        // answers `VK_INCOMPLETE` for a ray tracing launch's two binary forms whatever room they are
        // given (`docs/research/2026-11-13-amd-pipeline-code-forms.md`).
        std::vector<VkPipelineExecutableInternalRepresentationKHR> forms
            = enumerateVk<VkPipelineExecutableInternalRepresentationKHR>(*this,
                "vkGetPipelineExecutableInternalRepresentationsKHR", represent,
                VkPipelineExecutableInternalRepresentationKHR{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR,
                    .pNext = nullptr,
                    .name = {},
                    .description = {},
                    .isText = VK_FALSE,
                    .dataSize = 0,
                    .pData = nullptr });
        std::vector<std::string> bodies(forms.size());
        for (std::size_t form = 0; form < forms.size(); ++form)
        {
            if (forms[form].isText == VK_FALSE)
                continue;
            bodies[form].resize(forms[form].dataSize);
            forms[form].pData = bodies[form].data();
        }
        auto count = static_cast<std::uint32_t>(forms.size());

        // Still incomplete with the room the driver said its text needs is the driver's fault, and the
        // code a diagnostic: left out with a warning, so the run that asked for it goes on.
        const VkResult filled = represent(&count, forms.data());
        if (filled == VK_INCOMPLETE)
        {
            Log(Debug::Warning) << "pipeline " << name << ": its driver answered VK_INCOMPLETE for its code with the "
                                << "room it said the code needs, so its code is left out";
            return;
        }
        checkVk(*this, filled, "vkGetPipelineExecutableInternalRepresentationsKHR");

        std::vector<PipelineCodeForm> text;
        for (std::size_t form = 0; form < forms.size(); ++form)
        {
            if (forms[form].isText == VK_FALSE)
                continue;

            // A text form's size counts its terminator.
            std::string_view body(bodies[form].data(), forms[form].dataSize);
            while (!body.empty() && body.back() == '\0')
                body.remove_suffix(1);
            text.push_back(
                PipelineCodeForm{ .mName = forms[form].name, .mDescription = forms[form].description, .mText = body });
        }
        if (!text.empty())
            mCode->write(name, copy, executable, executables, text);
    }

    void Device::waitFor(const std::uint64_t value, const char* const what) const
    {
        mTimeline->waitFor(value, what);
        collect();
    }

    void Device::waitIdle() const
    {
        // **The queue's last submit waited for on the clock first**, under its patience, so a device that
        // stopped answering ends naming the wait (`Timeline::waitFor`), where `vkDeviceWaitIdle`, which has
        // no timeout, hung silently. The idle after it waits out the presentation engine's own work.
        if (const std::uint64_t submitted = mTimeline->getNext() - 1; submitted > 0)
            mTimeline->waitFor(submitted, "the queue going idle");

        const VkResult result = vkDeviceWaitIdle(mHandle.get());

        // Marked whatever the wait answered. A lost device ends the process below; any other
        // refusal throws, and left unmarked, the graveyard's own assert that the queue is idle
        // would fire in its destructor on the way out of that throw and name itself in its place.
        mTimeline->markIdle();
        checkVk(*this, result, "vkDeviceWaitIdle");
        collect();
    }

    void Device::collect() const
    {
        mGraveyard->collect();
        mPool->collect();
    }

    void Device::collectIdle() const
    {
        mGraveyard->collectIdle();
        mPool->collectIdle();
    }

    void Device::writeMarker(const VkCommandBuffer commands, const Checkpoint* const checkpoint) const
    {
        mMarkers->mark(commands, checkpoint);
    }

    std::string Device::describeMarkers() const
    {
        if (mMarkers == nullptr)
            return {};

        const BufferMarkers::Passed passed = mMarkers->read();
        return describePassedStage(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, passed.mTop)
            + describePassedStage(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, passed.mBottom);
    }

    std::string Device::describeCheckpoints() const
    {
        if (mMarkers != nullptr)
            return describeMarkers();

        if (mGetQueueCheckpointData == nullptr)
            return {};

        const std::vector<VkCheckpointDataNV> passed = enumerateVk<VkCheckpointDataNV>(
            "vkGetQueueCheckpointDataNV",
            [&](std::uint32_t* count, VkCheckpointDataNV* into) {
                mGetQueueCheckpointData(mQueue, count, into);
                return VK_SUCCESS;
            },
            VkCheckpointDataNV{ .sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV,
                .pNext = nullptr,
                .stage = VK_PIPELINE_STAGE_NONE,
                .pCheckpointMarker = nullptr });
        if (passed.empty())
            return "\nthe queue passed no checkpoint";

        std::string report;
        for (std::size_t at = 0; at < passed.size(); ++at)
        {
            const auto* checkpoint = static_cast<const Checkpoint*>(passed[at].pCheckpointMarker);
            if (checkpoint == nullptr)
                continue;

            report += describePassedStage(passed[at].stage, checkpoint);
        }

        return report;
    }

    std::string describeDeviceFault(const std::string_view description,
        const std::span<const VkDeviceFaultAddressInfoEXT> addresses,
        const std::span<const VkDeviceFaultVendorInfoEXT> vendor)
    {
        std::string report = "\ndevice fault: ";
        report += description.substr(0, description.find('\0'));
        for (const VkDeviceFaultAddressInfoEXT& address : addresses)
            report += std::format("\n  {} at {:#x}, known to within {:#x}", faultAddressTypeName(address.addressType),
                address.reportedAddress, address.addressPrecision);
        for (const VkDeviceFaultVendorInfoEXT& said : vendor)
            report += std::format("\n  {} (vendor code {:#x}, data {:#x})",
                std::string_view(said.description, std::char_traits<char>::length(said.description)),
                said.vendorFaultCode, said.vendorFaultData);
        return report;
    }

    std::string describePassedStage(const VkPipelineStageFlagBits stage, const Checkpoint* const checkpoint)
    {
        return checkpoint == nullptr
            ? std::format("\n  {} passed no checkpoint the host still names", checkpointStageName(stage))
            : std::format("\n  {} last passed `{}` of {} {}", checkpointStageName(stage), checkpoint->mName,
                  (checkpoint->mFrame & sPictureCheckpoint) != 0 ? "picture" : "frame",
                  checkpoint->mFrame & ~sPictureCheckpoint);
    }

    std::string Device::describeInFlight() const
    {
        return Rtx::describeInFlight(mTimeline->getNext() - 1, mTimeline->getKnownFinished(), mMemory->report());
    }

    std::string Device::describeFault() const
    {
        if (mGetDeviceFaultInfo == nullptr)
            return describeCheckpoints();

        constexpr const char* sUnsaid = "\nthe driver would not say where the device faulted";

        VkDeviceFaultCountsEXT counts{ .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
            .pNext = nullptr,
            .addressInfoCount = 0,
            .vendorInfoCount = 0,
            .vendorBinarySize = 0 };
        if (mGetDeviceFaultInfo(mHandle.get(), &counts, nullptr) != VK_SUCCESS)
            return sUnsaid + describeCheckpoints();

        std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
        std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);

        // Nought, because the feature that provides the binary was not enabled.
        counts.vendorBinarySize = 0;

        VkDeviceFaultInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
            .pNext = nullptr,
            .description = {},
            .pAddressInfos = addresses.data(),
            .pVendorInfos = vendor.data(),
            .pVendorBinaryData = nullptr,
        };

        // `VK_INCOMPLETE` is the driver having more to say than the counts it gave a moment ago
        // allowed for, and what it did say is still worth reading.
        const VkResult result = mGetDeviceFaultInfo(mHandle.get(), &counts, &info);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            return sUnsaid + describeCheckpoints();

        return describeDeviceFault(info.description, std::span(addresses).first(counts.addressInfoCount),
                   std::span(vendor).first(counts.vendorInfoCount))
            + describeCheckpoints();
    }

    void Device::setNameImpl(VkObjectType type, std::uint64_t handle, const char* name) const
    {
        if (mSetObjectName == nullptr)
            return;

        const VkDebugUtilsObjectNameInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .pNext = nullptr,
            .objectType = type,
            .objectHandle = handle,
            .pObjectName = name,
        };

        // Deliberately unchecked: a label on a debugging aid, called from every resource created; the only
        // documented failure is host memory exhaustion, which will announce itself elsewhere.
        mSetObjectName(mHandle.get(), &info);
    }

    void Device::beginLabelImpl(VkCommandBuffer commands, const char* name) const
    {
        const VkDebugUtilsLabelEXT label{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pNext = nullptr,
            .pLabelName = name,
            // Left black, which every tool reads as "no colour was chosen" and picks its own. A
            // palette here would be one this fork maintains against tools that already have one.
            .color = { 0.0f, 0.0f, 0.0f, 0.0f },
        };

        mBeginLabel(commands, &label);
    }
}
