#pragma once

#include <cstdint>

#include <LinearMath/btVector3.h>

namespace BulletHelpers
{
    inline btVector3 getHeightfieldShift(
        std::int32_t x, std::int32_t y, std::int32_t size, float minHeight, float maxHeight)
    {
        return btVector3((static_cast<float>(x) + 0.5f) * static_cast<float>(size),
            (static_cast<float>(y) + 0.5f) * static_cast<float>(size), (maxHeight + minHeight) * 0.5f);
    }
}
