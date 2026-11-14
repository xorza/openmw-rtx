#pragma once

#include <cstddef>
#include <vector>

namespace Misc
{
    std::vector<std::byte> compress(const std::vector<std::byte>& data);

    std::vector<std::byte> decompress(const std::vector<std::byte>& data);
}
