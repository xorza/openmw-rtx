#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

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
    class SceneToolVertexPaintBrush;
}

namespace CSMWorld
{
    class IdTable;
}

namespace CSVRender
{
    class WorldspaceWidget;
    struct WorldspaceHitResult;

    /// \brief EditMode for handling terrain vertex colour painting
    class TerrainVertexPaintMode : public EditMode
    {
        Q_OBJECT

    public:
        enum InteractionType
        {
            InteractionType_PrimaryEdit,
            InteractionType_PrimarySelect,
            InteractionType_None
        };

        enum VertexPaintEditTool
        {
            VertexPaintEditTool_Replace = 0
        };

        /// Editmode for terrain vertex colour grid
        TerrainVertexPaintMode(WorldspaceWidget*, osg::Group* parentNode, QWidget* parent = nullptr);

        void primaryOpenPressed(const WorldspaceHitResult& hit) override;

        /// Create single command for one-click vertex paint editing
        void primaryEditPressed(const WorldspaceHitResult& hit) override;

        /// Paint with selected colour on left click
        void primarySelectPressed(const WorldspaceHitResult&) override;

        void activate(CSVWidget::SceneToolbar*) override;
        void deactivate(CSVWidget::SceneToolbar*) override;

        /// Start vertex paint editing command macro
        bool primaryEditStartDrag(const QPoint& pos) override;

        bool secondaryEditStartDrag(const QPoint& pos) override;
        bool primarySelectStartDrag(const QPoint& pos) override;

        /// Handle vertex paint edit behavior during dragging
        void drag(const QPoint& pos, std::int32_t diffX, std::int32_t diffY, double speedFactor) override;

        /// End vertex paint editing command macro
        void dragCompleted(const QPoint& pos) override;

        /// Cancel vertex paint editing, and reset all pending changes
        void dragAborted() override;

        void dragWheel(std::int32_t diff, double speedFactor) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;

    private:
        /// Reset everything in the current edit
        void endVertexPaintEditing();

        /// Handle brush mechanics for colour editing
        void editVertexColourGrid(const std::pair<std::int32_t, std::int32_t>& vertexCoords, bool dragOperation);

        /// Alter one pixel's colour
        void alterColour(
            CSMWorld::LandColoursColumn::DataType& landColorsNew, std::int32_t inCellX, std::int32_t inCellY);

        /// Push terrain vertex colour edits to command macro
        void pushEditToCommand(const CSMWorld::LandColoursColumn::DataType& newLandColours, CSMDoc::Document& document,
            CSMWorld::IdTable& landTable, const std::string& cellId);

        /// Create new cell and land if needed, only user tools may ask for opening new cells (useTool == false is for
        /// automated land changes)
        bool allowLandColourEditing(const std::string& cellId, bool useTool = true);

        std::int32_t mBrushSize = 1;
        CSVWidget::BrushShape mBrushShape = CSVWidget::BrushShape_Point;
        std::unique_ptr<BrushDraw> mBrushDraw;
        CSVWidget::SceneToolVertexPaintBrush* mVertexPaintBrushScenetool = nullptr;
        std::int32_t mDragMode = InteractionType_None;
        osg::Group* mParentNode;
        bool mIsEditing = false;
        std::int32_t mVertexPaintEditTool = VertexPaintEditTool_Replace;
        QColor mVertexPaintEditToolColor;
        bool mRestoreMode = false;

    public slots:
        void setBrushSize(std::int32_t brushSize);
        void setBrushShape(CSVWidget::BrushShape brushShape);
        void setVertexPaintEditTool(std::int32_t shapeEditTool);
        void setVertexPaintColor(const QColor& color);
    };
}
