#include "jsontext.hpp"

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
                    if (static_cast<unsigned char>(c) < 0x20)
                        quoted += std::format("\\u{:04x}", static_cast<unsigned>(c));
                    else
                        quoted += c;
            }
        }
        return quoted + '"';
    }
}
