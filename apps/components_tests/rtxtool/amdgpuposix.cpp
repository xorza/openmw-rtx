#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/amdgpu.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <components/rtx/renderer/pciaddress.hpp>
#include <components/testing/util.hpp>

// The AMD reader over a tree laid out as sysfs lays it out. POSIX's alone, because sysfs is, and
// because a PCI device's directory is named with colons, which no Windows path may hold.
namespace RtxTool
{
    namespace
    {
        /// **The AMD card the renderer draws on, by its place on the bus**, and not the first
        /// `card*` the kernel lists, which on an APU beside a discrete Radeon is the integrated one.
        /// Read from a tree laid out as sysfs lays it out: an AMD device with its hwmon, another
        /// vendor's device, and an address where nothing stands.
        TEST(RtxAmdGpuTest, theCardIsTheOneAtTheRenderersAddress)
        {
            EXPECT_EQ(
                AmdGpu::sysfsNameOf(Rtx::PciAddress{ .mDomain = 0x10, .mBus = 0x3, .mDevice = 0x1f, .mFunction = 1 }),
                "0010:03:1f.1");

            const std::filesystem::path devices = TestingOpenMW::outputFilePath("amdgpu-devices");
            std::filesystem::remove_all(devices);
            const auto write = [](const std::filesystem::path& file, const std::string& text) {
                std::filesystem::create_directories(file.parent_path());
                std::ofstream(file) << text;
            };
            write(devices / "0000:01:00.0" / "vendor", "0x10de\n");
            write(devices / "0000:03:00.0" / "vendor", "0x1002\n");
            write(devices / "0000:03:00.0" / "pp_dpm_sclk", "0: 500Mhz \n1: 2482Mhz *\n");
            write(devices / "0000:03:00.0" / "hwmon" / "hwmon4" / "temp1_input", "61500\n");

            const Rtx::PciAddress radeon{ .mBus = 3 };
            const std::optional<AmdGpu> found = AmdGpu::find(radeon, devices);
            ASSERT_TRUE(found.has_value());
            const GpuClock clock = found->readClock();
            ASSERT_TRUE(clock.mRead);
            EXPECT_EQ(clock.mCore.mLowestMhz, 2482u);
            EXPECT_EQ(clock.mMemory.mReadings, 0u) << "no pp_dpm_mclk, so no memory clock";
            EXPECT_EQ(clock.mTemperatureC, std::optional<std::uint32_t>(62u)) << "61.5 rounds to 62";
            EXPECT_EQ(clock.mThrottleMask, std::nullopt);

            EXPECT_FALSE(AmdGpu::find(Rtx::PciAddress{ .mBus = 1 }, devices).has_value()) << "another vendor's";
            EXPECT_FALSE(AmdGpu::find(Rtx::PciAddress{ .mBus = 9 }, devices).has_value()) << "nothing there";

            std::filesystem::remove_all(devices);
        }

    }
}
