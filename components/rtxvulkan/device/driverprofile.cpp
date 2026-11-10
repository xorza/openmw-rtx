#include "driverprofile.hpp"

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
                    return StatisticNames{
                        .mScratch = {}, .mRegisters = "Register Count", .mShared = "Shared Memory Size"
                    };
                default:
                    return StatisticNames{};
            }
        }
    }

    DriverProfile DriverProfile::of(const std::uint32_t vendorId, const VkDriverId driver)
    {
        return DriverProfile{ .mVendor = Platform::Gpu::vendorOf(vendorId), .mStatistics = statisticNamesOf(driver) };
    }
}
