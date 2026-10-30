#include "zonecounters.hpp"

#include <cassert>
#include <cstddef>

#include <components/debug/debuglog.hpp>

#include "device.hpp"
#include "physicaldevice.hpp"
#include "result.hpp"

namespace Rtx
{
    ZoneCounters::ZoneCounters(const Device& device)
        : mDevice(device.getHandle())
    {
        const VkPhysicalDevice physical = device.getPhysicalDevice().getHandle();
        const std::uint32_t family = device.getPhysicalDevice().getQueueFamily();

        std::uint32_t offered = 0;
        checkVk(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(
                    physical, family, &offered, nullptr, nullptr),
            "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
        std::vector<VkPerformanceCounterKHR> counters(offered,
            VkPerformanceCounterKHR{ .sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR,
                .pNext = nullptr,
                .unit = VK_PERFORMANCE_COUNTER_UNIT_GENERIC_KHR,
                .scope = VK_PERFORMANCE_COUNTER_SCOPE_COMMAND_KHR,
                .storage = VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR,
                .uuid = {} });
        std::vector<VkPerformanceCounterDescriptionKHR> descriptions(offered,
            VkPerformanceCounterDescriptionKHR{ .sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR,
                .pNext = nullptr,
                .flags = 0,
                .name = {},
                .category = {},
                .description = {} });
        checkVk(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(
                    physical, family, &offered, counters.data(), descriptions.data()),
            "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");

        pickOnePass(
            offered,
            [&](const std::span<const std::uint32_t> indices) {
                const VkQueryPoolPerformanceCreateInfoKHR asked{
                    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .queueFamilyIndex = family,
                    .counterIndexCount = static_cast<std::uint32_t>(indices.size()),
                    .pCounterIndices = indices.data(),
                };
                std::uint32_t passes = 0;
                vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(physical, &asked, &passes);
                return passes;
            },
            mIndices);

        mNameText.reserve(mIndices.size());
        for (const std::uint32_t index : mIndices)
        {
            mStorages.push_back(counters[index].storage);
            mNameText.emplace_back(descriptions[index].name);
        }
        mNames.assign(mNameText.begin(), mNameText.end());

        // Said once, where it is decided: which counters every zone reads, and which the one pass
        // left out, so a reader looking for one knows it was not missed.
        Log line(Debug::Info);
        line << "zone counters: " << mIndices.size() << " of " << offered << " in one pass";
        std::size_t taken = 0;
        bool leftOut = false;
        for (std::uint32_t index = 0; index < offered; ++index)
        {
            const bool read = taken < mIndices.size() && mIndices[taken] == index;
            taken += read ? 1 : 0;
            if (!read)
            {
                line << (leftOut ? ", " : "; left out: ") << descriptions[index].name;
                leftOut = true;
            }
        }

        if (mIndices.empty())
            return;

        // **Held from here to the destructor**: a command buffer that begins one of these queries
        // must have begun its recording under the lock, and every frame's does. Waited for no
        // longer than a device is, so a lock another process keeps fails the start and does not
        // hang it.
        const VkAcquireProfilingLockInfoKHR lock{
            .sType = VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .timeout = sPatience,
        };
        checkVk(vkAcquireProfilingLockKHR(mDevice, &lock), "vkAcquireProfilingLockKHR");
    }

    ZoneCounters::~ZoneCounters()
    {
        if (!mIndices.empty())
            vkReleaseProfilingLockKHR(mDevice);
    }

    double ZoneCounters::valueOf(const std::size_t counter, const VkPerformanceCounterResultKHR& result) const
    {
        switch (mStorages[counter])
        {
            case VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR:
                return static_cast<double>(result.int32);
            case VK_PERFORMANCE_COUNTER_STORAGE_INT64_KHR:
                return static_cast<double>(result.int64);
            case VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR:
                return static_cast<double>(result.uint32);
            case VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR:
                return static_cast<double>(result.uint64);
            case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR:
                return static_cast<double>(result.float32);
            case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR:
                return result.float64;
            default:
                assert(false && "a counter stored as no type the extension names");
                return 0.0;
        }
    }
}
