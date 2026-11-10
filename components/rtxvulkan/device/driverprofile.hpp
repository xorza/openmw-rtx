#pragma once

#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/platform/gpu.hpp>

namespace Rtx
{
    /// The names a driver reports a pipeline's use under (`CompiledUse`), empty where it reports none.
    struct StatisticNames
    {
        std::string_view mScratch;
        std::string_view mRegisters;
        std::string_view mShared;
    };

    /// What differs by the vendor and the driver of a device, read once off its identity: the one place
    /// a vendor or a driver is told apart (`AGENTS.md`, "GPU vendors stand behind one entity"). Every
    /// other file reads a fact here and names no vendor.
    struct DriverProfile
    {
        Platform::Gpu::Vendor mVendor = Platform::Gpu::Vendor::Other;
        StatisticNames mStatistics{};

        /// What a device of `vendorId` running `driver` comes to.
        static DriverProfile of(std::uint32_t vendorId, VkDriverId driver);
    };
}
