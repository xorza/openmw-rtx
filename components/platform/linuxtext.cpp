#include "linuxtext.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <utility>

namespace Platform::LinuxText
{
    namespace
    {
        /// The bytes a field of the kernel's `<name>:  <number> kB` lines states, or nothing where no line names
        /// it, or its value is not a number of kilobytes.
        std::optional<std::uint64_t> kilobytesField(std::string_view text, std::string_view field)
        {
            for (std::string_view rest = text; !rest.empty();)
            {
                const std::size_t end = rest.find('\n');
                std::string_view line = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                if (!line.starts_with(field))
                    continue;

                line.remove_prefix(field.size());
                line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
                std::uint64_t kilobytes = 0;
                const std::from_chars_result read = std::from_chars(line.data(), line.data() + line.size(), kilobytes);
                if (read.ec != std::errc{} || std::string_view(read.ptr, line.data() + line.size()) != " kB")
                    return std::nullopt;
                return kilobytes * 1024;
            }
            return std::nullopt;
        }
    }

    std::optional<float> hugePageShare(std::string_view rollup)
    {
        const auto kilobytes = [&](std::string_view field) -> std::optional<std::uint64_t> {
            for (std::string_view rest = rollup; !rest.empty();)
            {
                const std::size_t end = rest.find('\n');
                std::string_view line = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                if (!line.starts_with(field))
                    continue;

                line.remove_prefix(field.size());
                line.remove_prefix(std::min(line.find_first_not_of(' '), line.size()));
                std::uint64_t value = 0;
                const std::from_chars_result read = std::from_chars(line.data(), line.data() + line.size(), value);
                if (read.ec != std::errc{} || std::string_view(read.ptr, line.data() + line.size()) != " kB")
                    return std::nullopt;
                return value;
            }
            return std::uint64_t{ 0 };
        };

        const std::optional<std::uint64_t> anonymous = kilobytes("Anonymous:");
        const std::optional<std::uint64_t> transparent = kilobytes("AnonHugePages:");
        const std::optional<std::uint64_t> reserved = kilobytes("Private_Hugetlb:");
        if (!anonymous.has_value() || !transparent.has_value() || !reserved.has_value() || *anonymous + *reserved == 0)
            return std::nullopt;
        return static_cast<float>(
            static_cast<double>(*transparent + *reserved) / static_cast<double>(*anonymous + *reserved));
    }

    std::optional<std::vector<std::uint32_t>> parseCpuList(std::string_view text)
    {
        constexpr std::uint32_t limit = 8192;
        const auto number = [](std::string_view digits, std::uint32_t& into) {
            const char* const end = digits.data() + digits.size();
            // The view is handed on with its size beside it.
            // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
            const std::from_chars_result read = std::from_chars(digits.data(), end, into);
            return !digits.empty() && read.ec == std::errc{} && read.ptr == end && into < limit;
        };

        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.remove_suffix(1);

        std::vector<std::uint32_t> cpus;
        while (true)
        {
            const std::size_t comma = text.find(',');
            const std::string_view range = text.substr(0, comma);
            const std::size_t dash = range.find('-');
            std::uint32_t first = 0;
            std::uint32_t last = 0;
            if (!number(range.substr(0, dash), first))
                return std::nullopt;
            if (dash == std::string_view::npos)
                last = first;
            else if (!number(range.substr(dash + 1), last) || last < first)
                return std::nullopt;

            for (std::uint32_t cpu = first; cpu <= last; ++cpu)
                cpus.push_back(cpu);
            if (comma == std::string_view::npos)
                return cpus;
            text.remove_prefix(comma + 1);
        }
    }

    std::optional<std::uint64_t> compactionStalls(std::string_view vmstat)
    {
        constexpr std::string_view field = "compact_stall ";
        for (std::string_view rest = vmstat; !rest.empty();)
        {
            const std::size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            if (!line.starts_with(field))
                continue;

            line.remove_prefix(field.size());
            std::uint64_t value = 0;
            const std::from_chars_result read = std::from_chars(line.data(), line.data() + line.size(), value);
            if (read.ec != std::errc{} || read.ptr != line.data() + line.size())
                return std::nullopt;
            return value;
        }
        return std::nullopt;
    }

    std::optional<DriverClient> driverClient(std::string_view fdinfo)
    {
        bool client = false;
        std::optional<std::uint64_t> id;
        Process::DriverMemory held;
        const std::pair<std::string_view, std::uint64_t Process::DriverMemory::*> fields[] = {
            { "drm-total-vram:", &Process::DriverMemory::mTotalVram },
            { "drm-resident-vram:", &Process::DriverMemory::mResidentVram },
            { "drm-total-gtt:", &Process::DriverMemory::mTotalGtt },
            { "drm-resident-gtt:", &Process::DriverMemory::mResidentGtt },
            { "drm-resident-cpu:", &Process::DriverMemory::mResidentCpu },
        };
        const auto valueOf = [](std::string_view line, const std::string_view name) {
            line.remove_prefix(name.size());
            line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
            return line;
        };
        for (std::string_view rest = fdinfo; !rest.empty();)
        {
            const std::size_t end = rest.find('\n');
            const std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            client = client || line.starts_with("drm-driver:");
            if (constexpr std::string_view name = "drm-client-id:"; line.starts_with(name))
            {
                const std::string_view value = valueOf(line, name);
                std::uint64_t number = 0;
                const std::from_chars_result read = std::from_chars(value.data(), value.data() + value.size(), number);
                if (read.ec != std::errc{} || read.ptr != value.data() + value.size())
                    return std::nullopt;
                id = number;
            }
            for (const auto& [name, field] : fields)
            {
                if (!line.starts_with(name))
                    continue;
                const std::string_view value = valueOf(line, name);
                std::uint64_t size = 0;
                const std::from_chars_result read = std::from_chars(value.data(), value.data() + value.size(), size);
                const std::string_view unit(read.ptr, value.data() + value.size());
                if (read.ec != std::errc{})
                    return std::nullopt;
                if (unit == " KiB")
                    size *= 1024;
                else if (unit == " MiB")
                    size *= 1024 * 1024;
                else if (!unit.empty())
                    return std::nullopt;
                held.*field = size;
            }
        }
        if (!client || !id.has_value())
            return std::nullopt;
        return DriverClient{ .mId = *id, .mMemory = held };
    }

    std::optional<std::uint64_t> availableMemory(std::string_view meminfo)
    {
        return kilobytesField(meminfo, "MemAvailable:");
    }

    std::optional<std::uint64_t> availableToProcess(std::string_view meminfo, std::string_view status)
    {
        const std::optional<std::uint64_t> available = availableMemory(meminfo);
        if (!available.has_value())
            return std::nullopt;
        const std::uint64_t swapped = kilobytesField(status, "VmSwap:").value_or(0);
        return *available - std::min(*available, swapped);
    }

    std::optional<Process::AnonymousMemory> anonymousMemory(std::string_view status)
    {
        const std::optional<std::uint64_t> resident = kilobytesField(status, "RssAnon:");
        const std::optional<std::uint64_t> swapped = kilobytesField(status, "VmSwap:");
        if (!resident.has_value() || !swapped.has_value())
            return std::nullopt;
        return Process::AnonymousMemory{ .mResident = *resident, .mNotResident = *swapped };
    }

    std::optional<std::string_view> unifiedCgroup(std::string_view cgroup)
    {
        constexpr std::string_view prefix = "0::";
        for (std::string_view rest = cgroup; !rest.empty();)
        {
            const std::size_t end = rest.find('\n');
            const std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            if (line.starts_with(prefix) && line.size() > prefix.size() && line[prefix.size()] == '/')
                return line.substr(prefix.size());
        }
        return std::nullopt;
    }

    std::optional<std::uint64_t> cgroupBytes(std::string_view text)
    {
        while (!text.empty() && text.back() == '\n')
            text.remove_suffix(1);
        std::uint64_t bytes = 0;
        const std::from_chars_result read = std::from_chars(text.data(), text.data() + text.size(), bytes);
        if (text.empty() || read.ec != std::errc{} || read.ptr != text.data() + text.size())
            return std::nullopt;
        return bytes;
    }

    std::optional<std::uint16_t> pciId(std::string_view text)
    {
        if (text.ends_with('\n'))
            text.remove_suffix(1);
        if (text.size() != 6 || !text.starts_with("0x"))
            return std::nullopt;
        text.remove_prefix(2);
        std::uint16_t id = 0;
        const std::from_chars_result read = std::from_chars(text.data(), text.data() + text.size(), id, 16);
        if (read.ec != std::errc{} || read.ptr != text.data() + text.size())
            return std::nullopt;
        return id;
    }
}
