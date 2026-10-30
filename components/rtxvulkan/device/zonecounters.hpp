#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/rtx/renderer/renderer.hpp>

namespace Rtx
{
    class Device;

    /// The device's own counters, read around each zone of a frame (`GpuTimer`): as many of what the
    /// queue's family offers as one pass reads, and the profiling lock a query of them needs, held for
    /// as long as this stands. Made by the device where the run asks (`DeviceReport::mCounters`) and
    /// the driver offers `VK_KHR_performance_query`.
    ///
    /// **One pass, because a frame is not replayed.** A set the driver reads in several passes is
    /// read by submitting the same commands once a pass, which a frame recorded once and submitted
    /// once cannot do; so the set is the counters that share one, and those it leaves out are said.
    class ZoneCounters
    {
    public:
        explicit ZoneCounters(const Device& device);
        ~ZoneCounters();

        ZoneCounters(const ZoneCounters&) = delete;
        ZoneCounters& operator=(const ZoneCounters&) = delete;

        /// Of `offered` counters, the ones a query reads in one pass: each in the driver's order
        /// that, with those taken before it, `passesOf` still answers one pass for, up to
        /// `sMaxZoneCounters`, into `into`. **Greedy, so the driver's order decides** which of two
        /// that cannot share a pass is read: RADV lists its instruction counts and its units' busy
        /// time first, then the memory's.
        ///
        /// @param passesOf the passes a query of the counters it is handed takes.
        template <class PassesOf>
        static void pickOnePass(std::uint32_t offered, PassesOf&& passesOf, std::vector<std::uint32_t>& into)
        {
            into.clear();
            for (std::uint32_t counter = 0; counter < offered && into.size() < sMaxZoneCounters; ++counter)
            {
                into.push_back(counter);
                if (passesOf(std::span<const std::uint32_t>(into)) != 1)
                    into.pop_back();
            }
        }

        /// What the queries are created with: the driver's indices of the counters read.
        std::span<const std::uint32_t> getIndices() const { return mIndices; }

        /// What the driver calls each, in the order of `getIndices`.
        std::span<const std::string_view> getNames() const { return mNames; }

        /// The value counter `counter` of `getIndices` read, from its result, whatever type the
        /// driver stores it as.
        double valueOf(std::size_t counter, const VkPerformanceCounterResultKHR& result) const;

    private:
        VkDevice mDevice;
        std::vector<std::uint32_t> mIndices;
        std::vector<VkPerformanceCounterStorageKHR> mStorages;
        std::vector<std::string> mNameText;
        std::vector<std::string_view> mNames;
    };
}
