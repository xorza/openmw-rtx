#include "amdgpu.hpp"

#include <charconv>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <components/platform/folder.hpp>
#include <components/platform/gpu.hpp>
#include <components/platform/linuxtext.hpp>
#include <components/platform/wholefile.hpp>

namespace RtxTool
{
    namespace
    {
        /// The whole of a sysfs file into `into`, or nothing where it cannot be read. `into` is the
        /// reader's own, so a reading allocates nothing once it has been read once.
        std::optional<std::string_view> readSmall(const std::filesystem::path& path, std::string& into)
        {
            if (!Platform::readWhole(path, into, Platform::sUnbounded).isOk())
                return std::nullopt;
            return std::string_view(into);
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
        mText.reserve(sReadBytes);
    }

    std::optional<AmdGpu> AmdGpu::at(const std::filesystem::path& devices, const Rtx::PciAddress& card)
    {
        const std::filesystem::path device = devices / folderName(card);

        std::string text;
        const std::optional<std::string_view> vendor = readSmall(device / "vendor", text);
        const std::optional<std::uint16_t> id = vendor.has_value() ? Platform::LinuxText::pciId(*vendor) : std::nullopt;
        if (!id.has_value() || Platform::Gpu::vendorOf(*id) != Platform::Gpu::Vendor::Amd)
            return std::nullopt;

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

    std::string AmdGpu::folderName(const Rtx::PciAddress& card)
    {
        return std::format("{:04x}:{:02x}:{:02x}.{:x}", card.mDomain, card.mBus, card.mDevice, card.mFunction);
    }

    GpuClock AmdGpu::readClock()
    {
        std::string& text = mText;
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
