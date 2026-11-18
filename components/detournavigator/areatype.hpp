#pragma once

#include <cstdint>

#include <Recast.h>

namespace DetourNavigator
{
    enum AreaType : std::uint8_t
    {
        AreaType_null = RC_NULL_AREA,
        AreaType_water = 1,
        AreaType_door = 2,
        AreaType_pathgrid = 3,
        AreaType_ground = RC_WALKABLE_AREA,
    };

    struct AreaCosts
    {
        float mWater = 1.0f;
        float mDoor = 2.0f;
        float mPathgrid = 1.0f;
        float mGround = 1.0f;
    };
}
