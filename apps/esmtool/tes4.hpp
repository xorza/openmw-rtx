#pragma once

#include <cstdint>
#include <fstream>
#include <iosfwd>
#include <memory>

namespace EsmTool
{
    struct Arguments;

    std::int32_t loadTes4(const Arguments& info, std::unique_ptr<std::ifstream>&& stream);
}
