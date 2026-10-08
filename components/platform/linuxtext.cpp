#include "linuxtext.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <system_error>

namespace Platform::LinuxText
{
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
}
