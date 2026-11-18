#pragma once

#include <cstddef>
#include <cstdint>

#include <osg/ref_ptr>

namespace osg
{
    class Geometry;
    class Group;
    class PositionAttitudeTransform;
}

namespace ESM
{
    struct Land;
}

namespace CSMWorld
{
    class CellCoordinates;
}

namespace CSVRender
{

    class CellBorder
    {
    public:
        CellBorder(const CellBorder&) = delete;
        CellBorder& operator=(const CellBorder&) = delete;
        CellBorder(osg::Group* cellNode, const CSMWorld::CellCoordinates& coords);
        ~CellBorder();

        void buildShape(const ESM::Land& esmLand);

    private:
        std::size_t landIndex(std::int32_t x, std::int32_t y);
        float scaleToWorld(std::int32_t val);

        // unimplemented

        osg::Group* mParentNode;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        osg::ref_ptr<osg::Geometry> mBorderGeometry;
    };
}
