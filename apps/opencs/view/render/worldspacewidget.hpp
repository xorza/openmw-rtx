#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QPoint>
#include <QTimer>
#include <osg/Vec3d>
#include <osg/ref_ptr>

#include "instancedragmodes.hpp"
#include "objectmarker.hpp"
#include "scenewidget.hpp"
#include "tagbase.hpp"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QModelIndex;
class QMouseEvent;
class QObject;
class QWheelEvent;
class QWidget;

namespace CSMDoc
{
    class Document;
}

namespace osg
{
    class Vec3f;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSMWorld
{
    class CellCoordinates;
    class UniversalId;
}

namespace CSVWidget
{
    class SceneToolMode;
    class SceneToolToggle2;
    class SceneToolbar;
    class SceneToolRun;
}

namespace CSVRender
{
    class Cell;
    class EditMode;

    struct WorldspaceHitResult
    {
        bool hit;
        osg::ref_ptr<TagBase> tag;
        std::uint32_t index0, index1, index2; // indices of mesh vertices
        osg::Vec3d worldPos;
    };

    class WorldspaceWidget : public SceneWidget
    {
        Q_OBJECT

        CSVWidget::SceneToolToggle2* mSceneElements;
        CSVWidget::SceneToolRun* mRun;
        CSMDoc::Document& mDocument;
        std::uint32_t mInteractionMask;
        CSVWidget::SceneToolMode* mEditMode;
        CSVWidget::SceneToolMode* mCameraMode{};
        bool mLocked;
        std::int32_t mDragMode;
        bool mDragging;
        std::int32_t mDragX;
        std::int32_t mDragY;
        bool mSpeedMode;
        double mDragFactor;
        double mDragWheelFactor;
        double mDragShiftFactor;
        QTimer mToolTipDelayTimer;
        QPoint mToolTipPos;
        bool mShowToolTips;
        std::int32_t mToolTipDelay;
        std::int32_t mSelectedNavigationMode;

    public:
        enum DropType
        {
            Type_CellsInterior,
            Type_CellsExterior,
            Type_Other,
            Type_DebugProfile
        };

        enum DropRequirements
        {
            canHandle,
            needPaged,
            needUnpaged,
            ignored // either mixed cells, or not cells
        };

        enum InteractionType
        {
            InteractionType_PrimaryEdit,
            InteractionType_PrimarySelect,
            InteractionType_SecondaryEdit,
            InteractionType_SecondarySelect,
            InteractionType_TertiarySelect,
            InteractionType_PrimaryOpen,
            InteractionType_None
        };

        explicit WorldspaceWidget(CSMDoc::Document& document, QWidget* parent = nullptr);
        ~WorldspaceWidget() override = default;

        CSVWidget::SceneToolMode* makeNavigationSelector(CSVWidget::SceneToolbar* parent);
        ///< \attention The created tool is not added to the toolbar (via addTool). Doing that
        /// is the responsibility of the calling function.

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolToggle2* makeSceneVisibilitySelector(CSVWidget::SceneToolbar* parent);

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolRun* makeRunTool(CSVWidget::SceneToolbar* parent);

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolMode* makeEditModeSelector(CSVWidget::SceneToolbar* parent);

        void selectDefaultNavigationMode();

        void centerOrbitCameraOnSelection();

        static DropType getDropType(const std::vector<CSMWorld::UniversalId>& data);

        virtual DropRequirements getDropRequirements(DropType type) const;

        virtual void useViewHint(const std::string& hint);
        ///< Default-implementation: ignored.

        /// \return Drop handled?
        virtual bool handleDrop(const std::vector<CSMWorld::UniversalId>& data, DropType type);

        virtual std::uint32_t getVisibilityMask() const;

        /// \note This function will implicitly add elements that are independent of the
        /// selected edit mode.
        virtual void setInteractionMask(std::uint32_t mask);

        /// \note This function will only return those elements that are both visible and
        /// marked for interaction.
        std::uint32_t getInteractionMask() const;

        virtual void setEditLock(bool locked);

        CSMDoc::Document& getDocument();
        const CSMDoc::Document& getDocument() const;

        /// \param elementMask Elements to be affected by the clear operation
        virtual void clearSelection(std::int32_t elementMask) = 0;

        /// \param elementMask Elements to be affected by the select operation
        virtual void invertSelection(std::int32_t elementMask) = 0;

        /// \param elementMask Elements to be affected by the select operation
        virtual void selectAll(std::int32_t elementMask) = 0;

        // Select everything that references the same ID as at least one of the elements
        // already selected
        //
        /// \param elementMask Elements to be affected by the select operation
        virtual void selectAllWithSameParentId(std::int32_t elementMask) = 0;

        virtual void selectInsideCube(const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode) = 0;

        virtual void selectWithinDistance(const osg::Vec3d& point, float distance, DragMode dragMode) = 0;

        template <typename Tag>
        std::optional<WorldspaceHitResult> checkTag(
            const osgUtil::LineSegmentIntersector::Intersection& intersection) const;

        std::tuple<osg::Vec3d, osg::Vec3d, osg::Vec3d> getStartEndDirection(
            std::int32_t pointX, std::int32_t pointY) const;

        /// Return the next intersection with scene elements matched by
        /// \a interactionMask based on \a localPos and the camera vector.
        /// If there is no such intersection, instead a point "in front" of \a localPos will be
        /// returned.
        WorldspaceHitResult mousePick(const QPoint& localPos, std::uint32_t interactionMask) const;

        virtual std::string getCellId(const osg::Vec3f& point) const = 0;

        /// \note Returns the cell if it exists, otherwise a null pointer
        virtual Cell* getCell(const osg::Vec3d& point) const = 0;

        virtual Cell* getCell(const CSMWorld::CellCoordinates& coords) const = 0;

        virtual osg::ref_ptr<TagBase> getSnapTarget(std::uint32_t elementMask) const = 0;

        virtual std::vector<osg::ref_ptr<TagBase>> getSelection(std::uint32_t elementMask) const = 0;

        virtual void selectGroup(const std::vector<std::string>&) const = 0;

        virtual void unhideAll() const = 0;

        virtual std::vector<osg::ref_ptr<TagBase>> getEdited(std::uint32_t elementMask) const = 0;

        virtual void setSubMode(std::int32_t subMode, std::uint32_t elementMask) = 0;

        /// Erase all overrides and restore the visual representation to its true state.
        virtual void reset(std::uint32_t elementMask) = 0;

        EditMode* getEditMode();

        virtual CSVRender::Object* getObjectByReferenceId(const std::string& id) = 0;

        ObjectMarker* getSelectionMarker() { return mSelectionMarker.get(); }
        const ObjectMarker* getSelectionMarker() const { return mSelectionMarker.get(); }

    protected:
        const std::unique_ptr<CSVRender::ObjectMarker> mSelectionMarker;

        /// Visual elements in a scene
        /// @note do not change the enumeration values, they are used in pre-existing button file names!
        enum ButtonId
        {
            Button_Reference = 0x1,
            Button_Pathgrid = 0x2,
            Button_Water = 0x4,
            Button_Terrain = 0x8
        };

        enum CameraMode
        {
            FirstPerson,
            Orbit,
            Free
        };

        virtual void addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool);

        virtual void addEditModeSelectorButtons(CSVWidget::SceneToolMode* tool);

        virtual void updateOverlay();

        void mouseMoveEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

        virtual void handleInteractionPress(const WorldspaceHitResult& hit, InteractionType type);

        void settingChanged(const CSMPrefs::Setting* setting) override;

        void cycleNavigationMode();

    private:
        bool hitBehindMarker(const osg::Vec3d& hitPos) const;

        void handleMarkerHighlight(const std::int32_t x, const std::int32_t y);

        void dragEnterEvent(QDragEnterEvent* event) override;

        void dropEvent(QDropEvent* event) override;

        void dragMoveEvent(QDragMoveEvent* event) override;

        virtual std::string getStartupInstruction() = 0;

        void handleInteraction(InteractionType type, bool activate);

    public slots:

        /// \note Drags will be automatically aborted when the aborting is triggered
        /// (either explicitly or implicitly) from within this class. This function only
        /// needs to be called, when the drag abort is triggered externally (e.g. from
        /// an edit mode).
        void abortDrag();

    private slots:

        virtual void referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void referenceableAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) = 0;

        virtual void referenceableAdded(const QModelIndex& index, std::int32_t start, std::int32_t end) = 0;

        virtual void referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void referenceAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) = 0;

        virtual void referenceAdded(const QModelIndex& index, std::int32_t start, std::int32_t end) = 0;

        virtual void pathgridDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void pathgridAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) = 0;

        virtual void pathgridAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end) = 0;

        virtual void runRequest(const std::string& profile);

        void debugProfileDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void debugProfileAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        void editModeChanged(const std::string& id);

        void showToolTip();

        void primaryOpen(bool activate);

        void primaryEdit(bool activate);

        void secondaryEdit(bool activate);

        void primarySelect(bool activate);

        void secondarySelect(bool activate);

        void tertiarySelect(bool activate);

        void speedMode(bool activate);

        void toggleHiddenInstances();

    protected slots:

        void elementSelectionChanged();

    signals:

        void closeRequest();

        void dataDropped(const std::vector<CSMWorld::UniversalId>& data);

        void requestFocus(const std::string& id);

        friend class MouseState;
    };
}
