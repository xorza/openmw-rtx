#include "tablesource.hpp"

#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>

#include <components/platform/wholefile.hpp>

namespace Rtx
{
    namespace
    {
        /// How many values a line of the source holds.
        constexpr std::size_t sPerLine = 12;

        void spell(std::string& out, float value)
        {
            assert(std::isfinite(value));
            if (std::signbit(value))
                out += '-';

            std::array<char, 32> digits{};
            const auto [end, error]
                = std::to_chars(digits.data(), digits.data() + digits.size(), std::fabs(value), std::chars_format::hex);
            assert(error == std::errc());
            out += "0x";
            out.append(digits.data(), end);
            out += 'f';
        }

        void spell(std::string& out, std::uint8_t value)
        {
            std::array<char, 4> digits{};
            const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
            assert(error == std::errc());
            out.append(digits.data(), end);
        }

        template <class T>
        void write(const std::filesystem::path& to, const TableName& name, std::span<const T> values,
            const std::string_view type)
        {
            std::string text;
            text += "// Made by the build, by the maker `";
            text += name.mName;
            text += "`'s header names. Never edited.\n\n#include <array>\n#include <cstdint>\n\n#include <";
            text += name.mHeader;
            text += ">\n\nnamespace Rtx\n{\n    constinit const std::array<";
            text += type;
            text += ", ";
            text += std::to_string(values.size());
            text += "> ";
            text += name.mName;
            text += "{ {\n";
            for (std::size_t at = 0; at < values.size(); ++at)
            {
                text += at % sPerLine == 0 ? "        " : " ";
                spell(text, values[at]);
                text += ',';
                if (at % sPerLine == sPerLine - 1 || at + 1 == values.size())
                    text += '\n';
            }
            text += "    } };\n}\n";

            // The filesystem's own error, which carries the path as it stands.
            const Result<void, Platform::IoError> written = Platform::writeWhole(to, text, Platform::Durability::Whole);
            if (!written.isOk())
                throw std::filesystem::filesystem_error("could not write the table", to, written.error().mCode);
        }
    }

    void writeTable(const std::filesystem::path& to, const TableName& name, std::span<const float> values)
    {
        write(to, name, values, "float");
    }

    void writeTable(const std::filesystem::path& to, const TableName& name, std::span<const std::uint8_t> values)
    {
        write(to, name, values, "std::uint8_t");
    }
}
