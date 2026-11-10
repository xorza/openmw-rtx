#include "gpu.hpp"

#include <algorithm>

namespace Platform::Gpu
{
    bool has(const std::uint32_t vendor)
    {
        const std::vector<std::uint32_t> all = vendors();
        return std::find(all.begin(), all.end(), vendor) != all.end();
    }
}
