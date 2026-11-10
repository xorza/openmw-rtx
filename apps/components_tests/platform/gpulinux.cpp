#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/platform/gpu.hpp>

namespace
{
    /// **Every card sysfs lists is named, each vendor once**: the vendors match what a plain read of
    /// `/sys/class/drm/card*/device/vendor` finds, connectors left out. A sysfs file states a page as
    /// its size whatever it holds, which a bound on the stated size refused, and the launcher then
    /// offered no DLSS on a machine with an NVIDIA card.
    TEST(RtxPlatformGpuTest, theVendorsAreTheOnesSysfsLists)
    {
        std::vector<std::uint32_t> expected;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator("/sys/class/drm", error))
        {
            const std::string name = Files::pathToUnicodeString(entry.path().filename());
            if (!name.starts_with("card") || name.find('-') != std::string::npos)
                continue;
            std::ifstream file(entry.path() / "device" / "vendor");
            std::string text;
            if (file >> text)
                expected.push_back(static_cast<std::uint32_t>(std::stoul(text, nullptr, 16)));
        }
        std::ranges::sort(expected);
        expected.erase(std::ranges::unique(expected).begin(), expected.end());

        EXPECT_EQ(Platform::Gpu::vendors(), expected);
    }
}
