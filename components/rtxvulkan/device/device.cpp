#include "device.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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

#include "commands.hpp"
#include "extensionfilter.hpp"
#include "faultreport.hpp"
#include "graveyard.hpp"
#include "instance.hpp"
#include "notfinitecensus.hpp"
#include "pipelinecache.hpp"
#include "pipelinereport.hpp"
#include "requirements.hpp"
#include "result.hpp"
#include "timeline.hpp"
#include "zonecounters.hpp"

namespace Rtx
{
    namespace
    {
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
        mPipelineReport = std::make_unique<PipelineReport>(*this, mReport.mCodeFolder);

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

        FaultReport::EntryPoints faultEntries;
        if (describesFault)
            load(mHandle.get(), faultEntries.mFaultInfo, "vkGetDeviceFaultInfoEXT");

        if (has(DeviceOption::Checkpoints))
        {
            load(mHandle.get(), faultEntries.mSetCheckpoint, "vkCmdSetCheckpointNV");
            load(mHandle.get(), faultEntries.mCheckpointData, "vkGetQueueCheckpointDataNV");
        }

        if (has(DeviceOption::BufferMarkers))
            load(mHandle.get(), faultEntries.mWriteMarker, "vkCmdWriteBufferMarkerAMD");

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

        mFaults = std::make_unique<FaultReport>(*this, faultEntries, has(DeviceOption::UncachedMarkers));

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

    std::string Device::describeInFlight() const
    {
        return Rtx::describeInFlight(mTimeline->getNext() - 1, mTimeline->getKnownFinished(), mMemory->report());
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
