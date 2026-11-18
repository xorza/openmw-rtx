#include "textureasks.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Rtx
{
    std::size_t footprintStep(const float footprint)
    {
        const float step = std::floor(footprint) - static_cast<float>(sFootprintFinest);
        if (!(step > 0.0f))
            return 0;
        return std::min(
            static_cast<std::size_t>(std::min(step, static_cast<float>(sFootprintSteps - 1))), sFootprintSteps - 1);
    }

    std::size_t levelAsked(const std::size_t step, const float offset, const std::size_t ceiling)
    {
        assert(step < sFootprintSteps && "a step past the footprint's scale");
        assert(ceiling < sTextureLevels && "a ceiling past a texture's levels");

        const float level = std::floor(static_cast<float>(step) + static_cast<float>(sFootprintFinest) + offset);
        if (!(level > 0.0f))
            return 0;
        return std::min(static_cast<std::size_t>(std::min(level, static_cast<float>(ceiling))), ceiling);
    }
}
