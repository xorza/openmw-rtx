#pragma once

#include <cstdint>

namespace DetourNavigator
{
    enum class CollisionShapeType : std::uint8_t
    {
        Aabb = 0,
        RotatingBox = 1,
        Cylinder = 2,
    };

    CollisionShapeType toCollisionShapeType(std::int32_t value);
}
