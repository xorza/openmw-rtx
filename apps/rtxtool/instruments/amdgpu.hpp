#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "gpuclock.hpp"

namespace RtxTool
{
    /// AMD's kernel driver as it describes a card in sysfs: the current level of each clock and the
    /// temperature, which is what `CardWatch` asks NVML for on an NVIDIA card. Read from the files
    /// `amdgpu` keeps under `/sys/class/drm/card*/device/`, of the first card whose PCI vendor is
    /// AMD's; a box with none answers nothing, as `Nvml` does without its library.
    ///
    /// **No process samples.** amdgpu counts a client's use per open file of the device, under
    /// `/proc/<pid>/fdinfo`, and keeps no list per card a reader can take, so who held the card is
    /// NVML's answer alone.
    class AmdGpu
    {
    public:
        /// Finds the card. Nothing is read until `readClock`.
        AmdGpu();

        bool isOpen() const { return !mDevice.empty(); }

        /// Why the report names no holder of an AMD card.
        static std::string_view describeUnsampled() { return "amdgpu keeps no process samples for a card"; }

        /// One reading of the clocks and the temperature, or one that answered nothing where no
        /// card was found or a file could not be read. No throttle reasons: those are NVML's bits.
        GpuClock readClock() const;

        /// The level a `pp_dpm_sclk` or `pp_dpm_mclk` marks current, in MHz: the line with the `*`,
        /// "1: 2482Mhz *", or the sleep level's "S: 800Mhz *". Nothing where no line is marked.
        static std::optional<std::uint32_t> currentLevelMhz(std::string_view levels);

        /// A hwmon `temp*_input`, which is millidegrees Celsius, as whole degrees rounded.
        static std::optional<std::uint32_t> wholeDegrees(std::string_view millidegrees);

    private:
        /// The card's `device` directory, empty where no AMD card was found.
        std::filesystem::path mDevice;

        /// Its hwmon's edge temperature, empty where the driver lists no hwmon.
        std::filesystem::path mTemperature;
    };
}
