#include "jsontext.hpp"

#include <cmath>
#include <cstdint>
#include <format>

namespace RtxTool
{
    std::string jsonString(std::string_view text)
    {
        std::string quoted = "\"";
        for (const char c : text)
        {
            switch (c)
            {
                case '"':
                    quoted += "\\\"";
                    break;
                case '\\':
                    quoted += "\\\\";
                    break;
                case '\n':
                    quoted += "\\n";
                    break;
                case '\t':
                    quoted += "\\t";
                    break;
                case '\r':
                    quoted += "\\r";
                    break;
                default:
                    if (static_cast<std::uint8_t>(c) < 0x20)
                        quoted += std::format("\\u{:04x}", static_cast<std::uint32_t>(c));
                    else
                        quoted += c;
            }
        }
        return quoted + '"';
    }

    std::string jsonNumber(const double value, const std::int32_t decimals)
    {
        return std::isfinite(value) ? std::format("{:.{}f}", value, decimals) : std::string("null");
    }

    std::string jsonSignificant(const double value, const std::int32_t digits)
    {
        return std::isfinite(value) ? std::format("{:.{}g}", value, digits) : std::string("null");
    }
}
