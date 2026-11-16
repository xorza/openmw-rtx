#pragma once

#include <cstddef>

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
        size_t landIndex(int x, int y);
        float scaleToWorld(int val);

        // unimplemented

        osg::Group* mParentNode;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        osg::ref_ptr<osg::Geometry> mBorderGeometry;
    };
}
