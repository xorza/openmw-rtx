#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <osg/Vec3d>

#include <apps/opencs/model/world/universalid.hpp>

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

namespace CSVWidget
{
    class SceneToolToggle2;
}

namespace CSMWorld
{
    class IdTable;
    class CellCoordinates;
}

namespace CSVRender
{
    class TagBase;

    class UnpagedWorldspaceWidget : public WorldspaceWidget
    {
        Q_OBJECT

        CSMDoc::Document& mDocument;
        std::string mCellId;
        CSMWorld::IdTable* mCellsModel;
        CSMWorld::IdTable* mReferenceablesModel;
        std::unique_ptr<Cell> mCell;

        void update();

    public:
        UnpagedWorldspaceWidget(std::string cellId, CSMDoc::Document& document, QWidget* parent);

        DropRequirements getDropRequirements(DropType type) const override;

        /// \return Drop handled?
        bool handleDrop(const std::vector<CSMWorld::UniversalId>& data, DropType type) override;

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

        osg::ref_ptr<TagBase> getSnapTarget(std::uint32_t elementMask) const override;

        std::vector<osg::ref_ptr<TagBase>> getSelection(std::uint32_t elementMask) const override;

        void selectGroup(const std::vector<std::string>& group) const override;

        void unhideAll() const override;

        std::vector<osg::ref_ptr<TagBase>> getEdited(std::uint32_t elementMask) const override;

        void setSubMode(std::int32_t subMode, std::uint32_t elementMask) override;

        /// Erase all overrides and restore the visual representation to its true state.
        void reset(std::uint32_t elementMask) override;

        CSVRender::Object* getObjectByReferenceId(const std::string& id) override;

    private:
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

    protected:
        void addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool) override;

    private slots:

        void cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void cellRowsAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        void assetTablesChanged();

    signals:

        void cellChanged(const CSMWorld::UniversalId& id);
    };
}
