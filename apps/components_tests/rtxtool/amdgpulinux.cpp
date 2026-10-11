#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/amdgpu.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <components/rtx/common/cardaddress.hpp>

namespace RtxTool
{
    namespace
    {
        /// **The AMD card at the address and no other**: two AMD cards and an NVIDIA one under a
        /// folder laid out as `/sys/bus/pci/devices` is, each AMD card at its own clock.
        TEST(RtxAmdGpuTest, theCardAtTheAddressIsReadAndNoOther)
        {
            const std::filesystem::path devices = std::filesystem::temp_directory_path()
                / ("openmw-pci-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
            std::filesystem::remove_all(devices);
            const auto card = [&](std::string_view folder, std::string_view vendor, std::string_view core) {
                const std::filesystem::path device = devices / folder;
                std::filesystem::create_directories(device);
                std::ofstream(device / "vendor") << vendor;
                std::ofstream(device / "pp_dpm_sclk") << core;
                std::ofstream(device / "pp_dpm_mclk") << "0: 96Mhz \n1: 1000Mhz *\n";
                return device;
            };
            const std::filesystem::path first = card("0000:03:00.0", "0x1002\n", "0: 500Mhz \n1: 1800Mhz *\n");
            std::filesystem::create_directories(first / "hwmon" / "hwmon3");
            std::ofstream(first / "hwmon" / "hwmon3" / "temp1_input") << "61400\n";
            card("0000:0a:00.0", "0x1002\n", "0: 500Mhz *\n1: 2100Mhz \n");
            card("0000:01:00.0", "0x10de\n", "1: 1500Mhz *\n");

            const Rtx::PciAddress atFirst{ .mDomain = 0, .mBus = 3, .mDevice = 0, .mFunction = 0 };
            const std::optional<AmdGpu> one = AmdGpu::at(devices, atFirst);
            ASSERT_TRUE(one.has_value());
            const GpuClock read = one->readClock();
            ASSERT_TRUE(read.mRead);
            EXPECT_EQ(read.mCore.mLowestMhz, 1800u);
            EXPECT_EQ(read.mMemory.mLowestMhz, 1000u);
            EXPECT_EQ(read.mTemperatureC, std::optional<std::uint32_t>(61u)) << "61.4 degrees, rounded";

            const Rtx::PciAddress atSecond{ .mDomain = 0, .mBus = 0xa, .mDevice = 0, .mFunction = 0 };
            const std::optional<AmdGpu> other = AmdGpu::at(devices, atSecond);
            ASSERT_TRUE(other.has_value());
            EXPECT_EQ(other->readClock().mCore.mLowestMhz, 500u) << "the second card's own clock";
            EXPECT_EQ(other->readClock().mTemperatureC, std::nullopt) << "a card with no hwmon has no temperature";

            const Rtx::PciAddress atNvidia{ .mDomain = 0, .mBus = 1, .mDevice = 0, .mFunction = 0 };
            EXPECT_FALSE(AmdGpu::at(devices, atNvidia).has_value()) << "another vendor's card";
            const Rtx::PciAddress atNothing{ .mDomain = 0, .mBus = 9, .mDevice = 0, .mFunction = 0 };
            EXPECT_FALSE(AmdGpu::at(devices, atNothing).has_value());

            std::filesystem::remove_all(devices);
        }
    }
}
