#pragma once

#include <iosfwd>
#include <memory>

namespace Files
{
    using IStreamPtr = std::unique_ptr<std::istream>;
}
