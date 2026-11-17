#pragma once

#include <cstdint>

#include <osg/Vec2i>

namespace DetourNavigator
{
    struct CellGridBounds
    {
        osg::Vec2i mCenter;
        std::int32_t mHalfSize{};
    };
}
