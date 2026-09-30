#include "cellgrid.hpp"

#include <algorithm>
#include <cmath>

namespace Rtx
{
    osg::Vec2f CellGrid::gapsTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const
    {
        const float low = static_cast<float>(cell.x()) * mCellSize;
        const float lowY = static_cast<float>(cell.y()) * mCellSize;
        return osg::Vec2f(std::max({ low - eye.x(), eye.x() - (low + mCellSize), 0.0f }),
            std::max({ lowY - eye.y(), eye.y() - (lowY + mCellSize), 0.0f }));
    }

    osg::Vec2i CellGrid::cellOf(const osg::Vec3f& position) const
    {
        return osg::Vec2i(static_cast<int>(std::floor(position.x() / mCellSize)),
            static_cast<int>(std::floor(position.y() / mCellSize)));
    }

    bool CellGrid::withinReach(const osg::Vec2i& cell, const osg::Vec3f& eye, const float radius) const
    {
        return distanceSquaredTo(cell, eye) < radius * radius;
    }

    float CellGrid::distanceSquaredTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const
    {
        return gapsTo(cell, eye).length2();
    }

    float CellGrid::chebyshevDistanceTo(const osg::Vec2i& cell, const osg::Vec3f& eye) const
    {
        const osg::Vec2f gap = gapsTo(cell, eye);
        return std::max(gap.x(), gap.y());
    }

    float CellGrid::reachOf(const LandReach& reach) const
    {
        if (!(reach.mCells > 0.0f))
            return reach.mViewingDistance;

        return reach.mCells * mCellSize;
    }

    bool inActiveGrid(const osg::Vec2i& cell, const osg::Vec4i& activeGrid)
    {
        return cell.x() >= activeGrid.x() && cell.y() >= activeGrid.y() && cell.x() < activeGrid.z()
            && cell.y() < activeGrid.w();
    }
}
