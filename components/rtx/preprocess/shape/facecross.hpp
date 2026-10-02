#pragma once

#include <osg/Vec3f>

namespace Rtx
{
    /// A triangle's cross product as the shape passes read a face: its unit normal, nought for a
    /// triangle with no area, and its area twice over. One rounding rule for a face, which every
    /// pass reads alike.
    struct FaceCross
    {
        osg::Vec3f mUnit;
        float mTwiceArea = 0.0f;

        static FaceCross of(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
        {
            const osg::Vec3f cross = (b - a) ^ (c - a);
            const float length = cross.length();
            return FaceCross{ .mUnit = length > 0.0f ? cross / length : osg::Vec3f(), .mTwiceArea = length };
        }
    };
}
