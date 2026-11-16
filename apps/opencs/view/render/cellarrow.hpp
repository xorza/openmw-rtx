#pragma once

#include <QString>

#include <osg/ref_ptr>

#include <apps/opencs/model/world/cellcoordinates.hpp>

#include "tagbase.hpp"

namespace osg
{
    class PositionAttitudeTransform;
    class Group;
}

namespace CSVRender
{
    class CellArrow;
    struct WorldspaceHitResult;

    class CellArrowTag : public TagBase
    {
        CellArrow* mArrow;

    public:
        explicit CellArrowTag(CellArrow* arrow);

        CellArrow* getCellArrow() const;

        QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const override;
    };

    class CellArrow
    {
    public:
        CellArrow(const CellArrow&) = delete;
        CellArrow& operator=(const CellArrow&) = delete;
        enum Direction
        {
            Direction_North = 1,
            Direction_West = 2,
            Direction_South = 4,
            Direction_East = 8
        };

    private:
        // not implemented

        Direction mDirection;
        osg::Group* mParentNode;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        CSMWorld::CellCoordinates mCoordinates;

        void adjustTransform();

        void buildShape();

    public:
        CellArrow(osg::Group* cellNode, Direction direction, const CSMWorld::CellCoordinates& coordinates);

        ~CellArrow();

        CSMWorld::CellCoordinates getCoordinates() const;

        Direction getDirection() const;
    };
}
