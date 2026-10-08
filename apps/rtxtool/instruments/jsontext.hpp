#pragma once

#include <string>
#include <string_view>

namespace RtxTool
{
    /// `text` as a JSON string, quotes included: a name comes from a file somebody wrote or from the
    /// process table, and one with a quote or a backslash in it would end the record there.
    std::string jsonString(std::string_view text);
}
