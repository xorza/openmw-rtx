#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>

#include "fourcc.hpp"

namespace ESM
{
    enum class Format : std::uint32_t
    {
        Tes3 = fourCC("TES3"),
        Tes4 = fourCC("TES4"),
    };

    Format readFormat(std::istream& stream);

    Format parseFormat(std::string_view value);
}
