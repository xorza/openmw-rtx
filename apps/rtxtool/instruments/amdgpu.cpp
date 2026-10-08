#include "amdgpu.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/platform/folder.hpp>

namespace RtxTool
{
    namespace
    {
        /// PCI's number for AMD, as `device/vendor` spells it.
        constexpr std::string_view sAmdVendor = "0x1002";

        /// The whole of a small sysfs file into `into`, or nothing where it cannot be read. A
        /// buffer on the stack, since every file read here is a line or a handful of them.
        std::optional<std::string_view> readSmall(const std::filesystem::path& path, std::array<char, 512>& into)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return std::nullopt;

            file.read(into.data(), static_cast<std::streamsize>(into.size()));
            return std::string_view(into.data(), static_cast<std::size_t>(file.gcount()));
        }

        std::optional<std::uint32_t> leadingNumber(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);

            std::uint32_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end == text.data())
                return std::nullopt;

            return value;
        }
    }

    AmdGpu::AmdGpu(std::filesystem::path device, std::filesystem::path temperature)
        : mDevice(std::move(device))
        , mTemperature(std::move(temperature))
    {
    }

    std::optional<AmdGpu> AmdGpu::find()
    {
        const std::optional<std::vector<std::filesystem::directory_entry>> cards
            = Platform::listFolder("/sys/class/drm");
        if (!cards.has_value())
            return std::nullopt;

        std::array<char, 512> text;
        for (const std::filesystem::directory_entry& card : *cards)
        {
            // `card0` and not a connector of it, `card0-HDMI-A-1`, which holds no `device/vendor`
            // of its own but a link back to the card's.
            const std::string name = Files::pathToUnicodeString(card.path().filename());
            if (!name.starts_with("card") || name.find('-') != std::string::npos)
                continue;

            const std::filesystem::path device = card.path() / "device";
            const std::optional<std::string_view> vendor = readSmall(device / "vendor", text);
            if (!vendor.has_value() || !vendor->starts_with(sAmdVendor))
                continue;

            std::filesystem::path temperature;
            const std::optional<std::vector<std::filesystem::directory_entry>> monitors
                = Platform::listFolder(device / "hwmon");
            for (const std::filesystem::directory_entry& hwmon :
                monitors.value_or(std::vector<std::filesystem::directory_entry>{}))
            {
                std::error_code error;
                if (std::filesystem::exists(hwmon.path() / "temp1_input", error))
                {
                    temperature = hwmon.path() / "temp1_input";
                    break;
                }
            }
            return AmdGpu(device, std::move(temperature));
        }

        return std::nullopt;
    }

    GpuClock AmdGpu::readClock() const
    {
        std::array<char, 512> text;
        const std::optional<std::string_view> core = readSmall(mDevice / "pp_dpm_sclk", text);
        const std::optional<std::uint32_t> coreMhz = core.has_value() ? currentLevelMhz(*core) : std::nullopt;
        if (!coreMhz.has_value())
            return {};

        const std::optional<std::string_view> memory = readSmall(mDevice / "pp_dpm_mclk", text);
        const std::optional<std::uint32_t> memoryMhz = memory.has_value() ? currentLevelMhz(*memory) : std::nullopt;

        std::optional<std::uint32_t> degrees;
        if (!mTemperature.empty())
            if (const std::optional<std::string_view> read = readSmall(mTemperature, text))
                degrees = wholeDegrees(*read);

        // The reasons a card is held back are NVML's bits, which sysfs has no counterpart of.
        return GpuClock::reading(*coreMhz, memoryMhz, degrees, std::nullopt);
    }

    std::optional<std::uint32_t> AmdGpu::currentLevelMhz(std::string_view levels)
    {
        while (!levels.empty())
        {
            const std::size_t end = levels.find('\n');
            const std::string_view line = levels.substr(0, end);
            levels.remove_prefix(end == std::string_view::npos ? levels.size() : end + 1);

            const std::size_t colon = line.find(':');
            if (line.find('*') == std::string_view::npos || colon == std::string_view::npos)
                continue;

            return leadingNumber(line.substr(colon + 1));
        }

        return std::nullopt;
    }

    std::optional<std::uint32_t> AmdGpu::wholeDegrees(std::string_view millidegrees)
    {
        const std::optional<std::uint32_t> value = leadingNumber(millidegrees);
        if (!value.has_value())
            return std::nullopt;

        return (*value + 500) / 1000;
    }
}
