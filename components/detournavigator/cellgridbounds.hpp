#pragma once

#include <osg/Vec2i>

namespace DetourNavigator
{
    struct CellGridBounds
    {
        osg::Vec2i mCenter;
        int mHalfSize;
    };
}
