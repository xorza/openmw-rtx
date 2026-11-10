#pragma once

#include <string>
#include <string_view>

namespace RtxTool
{
    /// `text` as a JSON string, quotes included: a name comes from a file somebody wrote or from the
    /// process table, and one with a quote or a backslash in it would end the record there.
    std::string jsonString(std::string_view text);

    /// `value` as a JSON number at four decimals, or `null` where it is not finite, which JSON has no
    /// number for: a figure of no samples, which a reader such as Python's `json` refuses as `nan`.
    std::string jsonNumber(double value);
}
