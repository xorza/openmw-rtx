#include "gpu.hpp"

#include <algorithm>

namespace Platform::Gpu
{
    bool has(const Vendor vendor)
    {
        const std::vector<Vendor> all = vendors();
        return std::find(all.begin(), all.end(), vendor) != all.end();
    }
}
