#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/cellselection.hpp>

#include "cell.hpp"
#include "instancedragmodes.hpp"
#include "worldspacewidget.hpp"

class QModelIndex;
class QObject;
class QWidget;

namespace osg
{
    class Vec3f;
    template <class T>
    class ref_ptr;
}

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class UniversalId;
}

namespace CSVWidget
{
    class SceneToolToggle2;
    class SceneToolMode;
    class SceneToolBar;
}

namespace CSVRender
{
    class Cell;
    class TagBase;

    class PagedWorldspaceWidget : public WorldspaceWidget
    {
        Q_OBJECT

        CSMDoc::Document& mDocument;
        CSMWorld::CellSelection mSelection;
        std::map<CSMWorld::CellCoordinates, Cell*> mCells;
        std::string mWorldspace;
        CSVWidget::SceneToolToggle2* mControlElements;
        bool mDisplayCellCoord;

    private:
        std::pair<std::int32_t, std::int32_t> getCoordinatesFromId(const std::string& record) const;

        /// Bring mCells into sync with mSelection again.
        ///
        /// \return Any cells added or removed?
        bool adjustCells();

        void referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) override;

        void referenceableAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        void referenceableAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        void referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) override;

        void referenceAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        void referenceAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        void pathgridDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) override;

        void pathgridAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        void pathgridAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;

        std::string getStartupInstruction() override;

        /// \note Does not update the view or any cell marker
        void addCellToScene(const CSMWorld::CellCoordinates& coordinates);

        /// \note Does not update the view or any cell marker
        ///
        /// \note Calling this function for a cell that is not in the selection is a no-op.
        void removeCellFromScene(const CSMWorld::CellCoordinates& coordinates);

        /// \note Does not update the view or any cell marker
        void addCellSelection(std::int32_t x, std::int32_t y);

        /// \note Does not update the view or any cell marker
        void moveCellSelection(std::int32_t x, std::int32_t y);

        void addCellToSceneFromCamera(std::int32_t offsetX, std::int32_t offsetY);

    public:
        PagedWorldspaceWidget(QWidget* parent, CSMDoc::Document& document);
        ///< \note Sets the cell area selection to an invalid value to indicate that currently
        /// no cells are displayed. The cells to be displayed will be specified later through
        /// hint system.

        ~PagedWorldspaceWidget() override;

        /// Decodes the the hint string to set of cell that are rendered.
        void useViewHint(const std::string& hint) override;

        void setCellSelection(const CSMWorld::CellSelection& selection);

        const CSMWorld::CellSelection& getCellSelection() const;

        /// \return Drop handled?
        bool handleDrop(const std::vector<CSMWorld::UniversalId>& data, DropType type) override;

        DropRequirements getDropRequirements(DropType type) const override;

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        virtual CSVWidget::SceneToolToggle2* makeControlVisibilitySelector(CSVWidget::SceneToolbar* parent);

        std::uint32_t getVisibilityMask() const override;

        /// \param elementMask Elements to be affected by the clear operation
        void clearSelection(std::int32_t elementMask) override;

        /// \param elementMask Elements to be affected by the select operation
        void invertSelection(std::int32_t elementMask) override;

        /// \param elementMask Elements to be affected by the select operation
        void selectAll(std::int32_t elementMask) override;

        // Select everything that references the same ID as at least one of the elements
        // already selected
        //
        /// \param elementMask Elements to be affected by the select operation
        void selectAllWithSameParentId(std::int32_t elementMask) override;

        void selectInsideCube(const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode) override;

        void selectWithinDistance(const osg::Vec3d& point, float distance, DragMode dragMode) override;

        std::string getCellId(const osg::Vec3f& point) const override;

        Cell* getCell(const osg::Vec3d& point) const override;

        Cell* getCell(const CSMWorld::CellCoordinates& coords) const override;

        void setCellAlteredHeight(
            const CSMWorld::CellCoordinates& coords, std::int32_t inCellX, std::int32_t inCellY, float height);

        float* getCellAlteredHeight(
            const CSMWorld::CellCoordinates& coords, std::int32_t inCellX, std::int32_t inCellY);

        void resetAllAlteredHeights();

        osg::ref_ptr<TagBase> getSnapTarget(std::uint32_t elementMask) const override;

        std::vector<osg::ref_ptr<TagBase>> getSelection(std::uint32_t elementMask) const override;

        void selectGroup(const std::vector<std::string>& group) const override;

        void unhideAll() const override;

        std::vector<osg::ref_ptr<TagBase>> getEdited(std::uint32_t elementMask) const override;

        void setSubMode(std::int32_t subMode, std::uint32_t elementMask) override;

        /// Erase all overrides and restore the visual representation to its true state.
        void reset(std::uint32_t elementMask) override;

        CSVRender::Object* getObjectByReferenceId(const std::string& referenceId) override;

    protected:
        void addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool) override;

        void addEditModeSelectorButtons(CSVWidget::SceneToolMode* tool) override;

        void handleInteractionPress(const WorldspaceHitResult& hit, InteractionType type) override;

    signals:

        void cellSelectionChanged(const CSMWorld::CellSelection& selection);

    private slots:

        virtual void cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        virtual void cellRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        virtual void cellAdded(const QModelIndex& index, std::int32_t start, std::int32_t end);

        virtual void landDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);
        virtual void landAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);
        virtual void landAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        virtual void landTextureDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);
        virtual void landTextureAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);
        virtual void landTextureAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        void assetTablesChanged();

        void loadCameraCell();

        void loadEastCell();

        void loadNorthCell();

        void loadWestCell();

        void loadSouthCell();
    };
}
