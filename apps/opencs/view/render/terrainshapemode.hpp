#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <osg/Vec3d>

#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/columnimp.hpp>
#include <apps/opencs/view/widget/brushshapes.hpp>

#include "brushdraw.hpp"
#include "editmode.hpp"

class QDragMoveEvent;
class QMouseEvent;
class QObject;
class QPoint;
class QWidget;

namespace osg
{
    class Group;
}

namespace CSMDoc
{
    class Document;
}

namespace CSVWidget
{
    class SceneToolShapeBrush;
}

namespace CSMWorld
{
    class IdTable;
}

namespace CSVRender
{
    class PagedWorldspaceWidget;
    class TerrainSelection;
    class WorldspaceWidget;
    struct WorldspaceHitResult;

    /// \brief EditMode for handling the terrain shape editing
    class TerrainShapeMode : public EditMode
    {
        Q_OBJECT

    public:
        enum InteractionType
        {
            InteractionType_PrimaryEdit,
            InteractionType_PrimarySelect,
            InteractionType_SecondaryEdit,
            InteractionType_SecondarySelect,
            InteractionType_None
        };

        enum ShapeEditTool
        {
            ShapeEditTool_Drag = 0,
            ShapeEditTool_PaintToRaise = 1,
            ShapeEditTool_PaintToLower = 2,
            ShapeEditTool_Smooth = 3,
            ShapeEditTool_Flatten = 4,
            ShapeEditTool_Equalize = 5
        };

        /// Editmode for terrain shape grid
        TerrainShapeMode(WorldspaceWidget*, osg::Group* parentNode, QWidget* parent = nullptr);

        void primaryOpenPressed(const WorldspaceHitResult& hit) override;

        /// Create single command for one-click shape editing
        void primaryEditPressed(const WorldspaceHitResult& hit) override;

        /// Open brush settings window
        void primarySelectPressed(const WorldspaceHitResult&) override;

        void secondarySelectPressed(const WorldspaceHitResult&) override;

        void activate(CSVWidget::SceneToolbar*) override;
        void deactivate(CSVWidget::SceneToolbar*) override;

        /// Start shape editing command macro
        bool primaryEditStartDrag(const QPoint& pos) override;

        bool secondaryEditStartDrag(const QPoint& pos) override;
        bool primarySelectStartDrag(const QPoint& pos) override;
        bool secondarySelectStartDrag(const QPoint& pos) override;

        /// Handle shape edit behavior during dragging
        void drag(const QPoint& pos, std::int32_t diffX, std::int32_t diffY, double speedFactor) override;

        /// End shape editing command macro
        void dragCompleted(const QPoint& pos) override;

        /// Cancel shape editing, and reset all pending changes
        void dragAborted() override;

        void dragWheel(std::int32_t diff, double speedFactor) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;

        std::shared_ptr<TerrainSelection> getTerrainSelection();

    private:
        /// Remove duplicates and sort mAlteredCells, then limitAlteredHeights forward and reverse
        void sortAndLimitAlteredCells();

        /// Reset everything in the current edit
        void clearTransientEdits();

        /// Move pending alteredHeights changes to omwgame/omwaddon -data
        void applyTerrainEditChanges();

        /// Handle brush mechanics for shape editing
        void editTerrainShapeGrid(const std::pair<std::int32_t, std::int32_t>& vertexCoords, bool dragOperation);

        /// Calculate height, when aiming for bump-shaped terrain change
        float calculateBumpShape(float distance, std::int32_t radius, float height);

        /// set the target height for flatten tool
        void setFlattenToolTargetHeight(const WorldspaceHitResult& hit);

        /// Do a single height alteration for transient shape edit map
        void alterHeight(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX, std::int32_t inCellY,
            float alteredHeight, bool useTool = true);

        /// Do a single smoothing height alteration for transient shape edit map
        void smoothHeight(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX, std::int32_t inCellY,
            std::int32_t toolStrength);

        /// Do a single flattening height alteration for transient shape edit map
        void flattenHeight(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX, std::int32_t inCellY,
            std::int32_t toolStrength, std::int32_t targetHeight);

        /// Do a single equalize alteration for transient shape edit map
        void equalizeHeight(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX, std::int32_t inCellY,
            std::int32_t targetHeight);

        /// Get altered height values around one vertex
        void updateKeyHeightValues(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX,
            std::int32_t inCellY, float* thisHeight, float* thisAlteredHeight, float* leftHeight,
            float* leftAlteredHeight, float* upHeight, float* upAlteredHeight, float* rightHeight,
            float* rightAlteredHeight, float* downHeight, float* downAlteredHeight);

        /// Limit steepness based on either X or Y and return false if steepness is limited
        void compareAndLimit(const CSMWorld::CellCoordinates& cellCoords, std::int32_t inCellX, std::int32_t inCellY,
            const float* limitedAlteredHeightXAxis, const float* limitedAlteredHeightYAxis,
            bool* steepnessIsWithinLimits);

        /// Check that the edit doesn't break save format limits, fix if necessary, return true if slope steepness is
        /// within limits
        bool limitAlteredHeights(const CSMWorld::CellCoordinates& cellCoords, bool reverseMode = false);

        /// Check if global selection coordinate belongs to cell in view
        bool isInCellSelection(std::int32_t globalSelectionX, std::int32_t globalSelectionY);

        /// Select vertex at global selection coordinate
        void handleSelection(std::int32_t globalSelectionX, std::int32_t globalSelectionY,
            std::vector<std::pair<std::int32_t, std::int32_t>>* selections);

        /// Handle brush mechanics for terrain shape selection
        void selectTerrainShapes(const std::pair<std::int32_t, std::int32_t>& vertexCoords, std::uint8_t selectMode);

        /// Push terrain shape edits to command macro
        void pushEditToCommand(const CSMWorld::LandHeightsColumn::DataType& newLandGrid, CSMDoc::Document& document,
            CSMWorld::IdTable& landTable, const std::string& cellId);

        /// Push land normals edits to command macro
        void pushNormalsEditToCommand(const CSMWorld::LandNormalsColumn::DataType& newLandGrid,
            CSMDoc::Document& document, CSMWorld::IdTable& landTable, const std::string& cellId);

        bool noCell(const std::string& cellId);

        bool noLand(const std::string& cellId);

        bool noLandLoaded(const std::string& cellId);

        bool isLandLoaded(const std::string& cellId);

        /// Create new blank height record and new normals, if there are valid adjancent cell, take sample points and
        /// set the average height based on that
        void createNewLandData(const CSMWorld::CellCoordinates& cellCoords);

        /// Create new cell and land if needed, only user tools may ask for opening new cells (useTool == false is for
        /// automated land changes)
        bool allowLandShapeEditing(const std::string& cellId, bool useTool = true);

        /// Bind the edging vertice to the values of the adjancent cells
        void fixEdges(CSMWorld::CellCoordinates cellCoords);

        std::string mBrushTexture;
        std::int32_t mBrushSize = 1;
        CSVWidget::BrushShape mBrushShape = CSVWidget::BrushShape_Point;
        std::unique_ptr<BrushDraw> mBrushDraw;
        std::vector<std::pair<std::int32_t, std::int32_t>> mCustomBrushShape;
        CSVWidget::SceneToolShapeBrush* mShapeBrushScenetool = nullptr;
        std::int32_t mDragMode = InteractionType_None;
        osg::Group* mParentNode;
        bool mIsEditing = false;
        std::shared_ptr<TerrainSelection> mTerrainShapeSelection;
        std::int32_t mTotalDiffY = 0;
        std::vector<CSMWorld::CellCoordinates> mAlteredCells;
        osg::Vec3d mEditingPos;
        std::int32_t mShapeEditTool = ShapeEditTool_Drag;
        std::int32_t mShapeEditToolStrength = 8;
        std::int32_t mTargetHeight = 0;

        PagedWorldspaceWidget& getPagedWorldspaceWidget();

    public slots:
        void setBrushSize(std::int32_t brushSize);
        void setBrushShape(CSVWidget::BrushShape brushShape);
        void setShapeEditTool(std::int32_t shapeEditTool);
        void setShapeEditToolStrength(std::int32_t shapeEditToolStrength);
    };
}
