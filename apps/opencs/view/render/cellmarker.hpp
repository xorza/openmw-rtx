#pragma once

#include "tagbase.hpp"

#include <osg/ref_ptr>

#include "../../model/world/cellcoordinates.hpp"

namespace osg
{
    class AutoTransform;
    class Group;
}

namespace CSVRender
{
    class CellMarker;

    class CellMarkerTag : public TagBase
    {
    private:
        CellMarker* mMarker;

    public:
        explicit CellMarkerTag(CellMarker* marker);

        CellMarker* getCellMarker() const;
    };

    /// \brief Marker to display cell coordinates.
    class CellMarker
    {
    private:
        osg::Group* mCellNode;
        osg::ref_ptr<osg::AutoTransform> mMarkerNode;
        CSMWorld::CellCoordinates mCoordinates;
        bool mExists;

        // Not implemented.

        /// \brief Build marker containing cell's coordinates.
        void buildMarker();

        /// \brief Position marker at center of cell.
        void positionMarker();

    public:
        CellMarker(const CellMarker&) = delete;
        CellMarker& operator=(const CellMarker&) = delete;
        /// \brief Constructor.
        /// \param cellNode Cell to create marker for.
        /// \param coordinates Coordinates of cell.
        /// \param cellExists Whether or not cell exists.
        CellMarker(osg::Group* cellNode, const CSMWorld::CellCoordinates& coordinates, const bool cellExists);

        ~CellMarker();
    };
}
