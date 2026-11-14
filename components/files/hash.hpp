#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace Files
{
    std::array<std::uint64_t, 2> getHash(std::string_view fileName, std::istream& stream);
}
