#pragma once

#include <fstream>
#include <iosfwd>
#include <memory>

namespace EsmTool
{
    struct Arguments;

    int loadTes4(const Arguments& info, std::unique_ptr<std::ifstream>&& stream);
}
