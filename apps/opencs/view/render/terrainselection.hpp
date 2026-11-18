#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <osg/Array>
#include <osg/ref_ptr>

namespace osg
{
    class Group;
    class Geometry;
    class PositionAttitudeTransform;
}

namespace CSVRender
{
    class WorldspaceWidget;

    enum class TerrainSelectionType
    {
        Texture,
        Shape
    };

    enum class SelectionMethod
    {
        OnlySelect,
        AddSelect,
        RemoveSelect,
        ToggleSelect
    };

    /// \brief Class handling the terrain selection data and rendering
    class TerrainSelection
    {
    public:
        TerrainSelection(osg::Group* parentNode, WorldspaceWidget* worldspaceWidget, TerrainSelectionType type);
        ~TerrainSelection();
        TerrainSelection(const TerrainSelection&) = delete;
        TerrainSelection& operator=(const TerrainSelection&) = delete;

        void onlySelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions);
        void addSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions);
        void removeSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions);
        void toggleSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions);
        void clearTemporarySelection();

        void activate();
        void deactivate();

        std::vector<std::pair<std::int32_t, std::int32_t>> getTerrainSelection() const;

        void update();

    protected:
        void drawShapeSelection(const osg::ref_ptr<osg::Vec3Array>& vertices);
        void drawTextureSelection(const osg::ref_ptr<osg::Vec3Array>& vertices);

        std::int32_t calculateLandHeight(std::int32_t x, std::int32_t y);

    private:
        void handleSelection(
            const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions, SelectionMethod selectionMethod);

        bool noCell(const std::string& cellId);

        bool noLand(const std::string& cellId);

        bool noLandLoaded(const std::string& cellId);

        bool isLandLoaded(const std::string& cellId);

        osg::Group* mParentNode;
        WorldspaceWidget* mWorldspaceWidget;
        osg::ref_ptr<osg::PositionAttitudeTransform> mBaseNode;
        osg::ref_ptr<osg::Geometry> mGeometry;
        osg::ref_ptr<osg::Group> mSelectionNode;
        std::vector<std::pair<std::int32_t, std::int32_t>>
            mSelection; // Global terrain selection coordinate in either vertex or texture units
        std::vector<std::pair<std::int32_t, std::int32_t>>
            mTemporarySelection; // Used during toggle to compare the most recent drag operation
        TerrainSelectionType mSelectionType;
    };
}
