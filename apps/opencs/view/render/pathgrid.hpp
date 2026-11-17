#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <QString>
#include <osg/Vec3d>
#include <osg/ref_ptr>

#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/subcellcollection.hpp>
#include <components/esm/refid.hpp>

#include "tagbase.hpp"

namespace osg
{
    class Vec3f;
    class Geometry;
    class Group;
    class PositionAttitudeTransform;
}

namespace CSMWorld
{
    class CommandMacro;
    class Data;
    struct Pathgrid;
}

namespace CSVRender
{
    class Pathgrid;

    struct WorldspaceHitResult;

    class PathgridTag : public TagBase
    {
    public:
        explicit PathgridTag(Pathgrid* pathgrid);

        Pathgrid* getPathgrid() const;

        QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const override;

    private:
        Pathgrid* mPathgrid;
    };

    class Pathgrid
    {
    public:
        typedef std::vector<std::uint16_t> NodeList;

        Pathgrid(CSMWorld::Data& data, osg::Group* parent, const std::string& pathgridId,
            const CSMWorld::CellCoordinates& coordinates);

        ~Pathgrid();
        Pathgrid(const Pathgrid&) = delete;
        Pathgrid& operator=(const Pathgrid&) = delete;

        const CSMWorld::CellCoordinates& getCoordinates() const;
        const std::string& getId() const;

        bool isSelected() const;
        const NodeList& getSelected() const;
        void selectAll();
        void toggleSelected(std::uint16_t node); // Adds to end of vector
        void invertSelected();
        void clearSelected();

        void moveSelected(const osg::Vec3d& offset);
        void setDragOrigin(std::uint16_t node);
        void setDragEndpoint(std::uint16_t node);
        void setDragEndpoint(const osg::Vec3d& pos);

        void resetIndicators();

        void applyPoint(CSMWorld::CommandMacro& commands, const osg::Vec3d& worldPos);
        void applyPosition(CSMWorld::CommandMacro& commands);
        void applyEdge(CSMWorld::CommandMacro& commands, std::uint16_t node1, std::uint16_t node2);
        void applyEdges(CSMWorld::CommandMacro& commands, std::uint16_t node);
        void applyRemoveNodes(CSMWorld::CommandMacro& commands);
        void applyRemoveEdges(CSMWorld::CommandMacro& commands);

        osg::ref_ptr<PathgridTag> getTag() const;

        void recreateGeometry();
        void removeGeometry();

        void update();

    private:
        CSMWorld::Data& mData;
        CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& mPathgridCollection;
        ESM::RefId mId;
        CSMWorld::CellCoordinates mCoords;
        bool mInterior;

        NodeList mSelected;
        osg::Vec3d mMoveOffset;
        std::uint16_t mDragOrigin;

        bool mChangeGeometry;
        bool mRemoveGeometry;
        bool mUseOffset;

        osg::Group* mParent;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        osg::ref_ptr<osg::Group> mPathgridGroup;
        osg::ref_ptr<osg::Geometry> mPathgridGeometry;
        osg::ref_ptr<osg::Geometry> mSelectedGeometry;
        osg::ref_ptr<osg::Geometry> mDragGeometry;

        osg::ref_ptr<PathgridTag> mTag;

        void createGeometry();
        void createSelectedGeometry();
        void createSelectedGeometry(const CSMWorld::Pathgrid& source);
        void removePathgridGeometry();
        void removeSelectedGeometry();

        void createDragGeometry(const osg::Vec3f& start, const osg::Vec3f& end, bool valid);

        const CSMWorld::Pathgrid* getPathgridSource();

        std::int32_t edgeExists(const CSMWorld::Pathgrid& source, std::uint16_t node1, std::uint16_t node2);
        void addEdge(CSMWorld::CommandMacro& commands, const CSMWorld::Pathgrid& source, std::uint16_t node1,
            std::uint16_t node2);
        void removeEdge(CSMWorld::CommandMacro& commands, const CSMWorld::Pathgrid& source, std::uint16_t node1,
            std::uint16_t node2);

        std::int32_t clampToCell(std::int32_t v);
    };
}
