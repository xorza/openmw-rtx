#pragma once

#include <string>
#include <string_view>

namespace RtxTool
{
    /// `text` as a JSON string, quotes included: a name comes from a file somebody wrote or from the
    /// process table, and one with a quote or a backslash in it would end the record there.
    std::string jsonString(std::string_view text);

    /// `value` as a JSON number at `decimals` decimals, or `null` where it is not finite, which JSON has no
    /// number for: a figure of no samples, which a reader such as Python's `json` refuses as `nan`.
    std::string jsonNumber(double value, int decimals);

    /// `value` as a JSON number to `digits` significant digits, or `null` where it is not finite: a figure whose
    /// scale nobody knows ahead, as a card's counter.
    std::string jsonSignificant(double value, int digits);
}
