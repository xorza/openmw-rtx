#pragma once

#include <array>

#include <osg/Matrix>
#include <osg/Quat>
#include <osg/Vec3f>

namespace LuaUtil
{
    class Box
    {
    public:
        Box(const osg::Vec3f& center, const osg::Vec3f& halfSize, const osg::Quat& rotation = osg::Quat());
        explicit Box(const osg::Matrix& transform);

        std::array<osg::Vec3f, 8> vertices() const;

        osg::Matrix asTransform() const;

        // TODO: Add `contains` and `intersects` methods

        bool operator==(const Box& other) const;

        osg::Vec3f mCenter;
        osg::Vec3f mHalfSize;
        osg::Quat mRotation;
    };
}
