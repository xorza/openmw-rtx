#include "driverprofile.hpp"

#include <components/platform/gpu.hpp>

namespace Rtx
{
    namespace
    {
        StatisticNames statisticNamesOf(const VkDriverId driver)
        {
            switch (driver)
            {
                case VK_DRIVER_ID_AMD_PROPRIETARY:
                    return StatisticNames{ .mScratch = "scratchMemUsageInBytes",
                        .mRegisters = "numUsedVgprs",
                        .mShared = "ldsUsageSizeInBytes" };
                case VK_DRIVER_ID_MESA_RADV:
                    return StatisticNames{ .mScratch = "Scratch size", .mRegisters = "VGPRs", .mShared = "LDS size" };
                case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
                    return StatisticNames{ .mScratch = "Local Memory Size",
                        .mRegisters = "Register Count",
                        .mShared = "Shared Memory Size",
                        .mScratchBits = 0xFFFFFFFF,
                        .mScratchFreeBytes = 112 };
                default:
                    return StatisticNames{};
            }
        }
    }

    DriverProfile DriverProfile::of(const std::uint32_t vendorId, const VkDriverId driver)
    {
        return DriverProfile{ .mNgx = Platform::Gpu::vendorOf(vendorId) == Platform::Gpu::Vendor::Nvidia,
            .mStatistics = statisticNamesOf(driver),
            .mReconvergesRayQueries = driver != VK_DRIVER_ID_AMD_PROPRIETARY };
    }
}
