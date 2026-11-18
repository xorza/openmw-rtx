#pragma once

#include <cstdint>

#include <apps/opencs/view/widget/modebutton.hpp>

class QDragEnterEvent;
class QDropEvent;
class QDragMoveEvent;
class QPoint;
class QMouseEvent;
class QObject;
class QWidget;

namespace CSVWidget
{
    class SceneToolbar;
}

namespace CSVRender
{
    class WorldspaceWidget;
    struct WorldspaceHitResult;

    class EditMode : public CSVWidget::ModeButton
    {
        Q_OBJECT

        WorldspaceWidget* mWorldspaceWidget;
        std::uint32_t mMask;

    protected:
        WorldspaceWidget& getWorldspaceWidget();
        const WorldspaceWidget& getWorldspaceWidget() const;

    public:
        EditMode(WorldspaceWidget* worldspaceWidget, const QIcon& icon, std::uint32_t mask, const QString& tooltip = "",
            QWidget* parent = nullptr);

        std::uint32_t getInteractionMask() const;

        void activate(CSVWidget::SceneToolbar* toolbar) override;

        /// Default-implementation: Ignored.
        virtual void setEditLock(bool locked);

        /// Default-implementation: Ignored.
        virtual void primaryOpenPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: Ignored.
        virtual void primaryEditPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: Ignored.
        virtual void secondaryEditPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: Ignored.
        virtual void primarySelectPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: Ignored.
        virtual void secondarySelectPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: Ignored.
        virtual void tertiarySelectPressed(const WorldspaceHitResult& hit);

        /// Default-implementation: ignore and return false
        ///
        /// \return Drag accepted?
        virtual bool primaryEditStartDrag(const QPoint& pos);

        /// Default-implementation: ignore and return false
        ///
        /// \return Drag accepted?
        virtual bool secondaryEditStartDrag(const QPoint& pos);

        /// Default-implementation: ignore and return false
        ///
        /// \return Drag accepted?
        virtual bool primarySelectStartDrag(const QPoint& pos);

        /// Default-implementation: ignore and return false
        ///
        /// \return Drag accepted?
        virtual bool secondarySelectStartDrag(const QPoint& pos);

        /// Default-implementation: ignored
        virtual void drag(const QPoint& pos, std::int32_t diffX, std::int32_t diffY, double speedFactor);

        /// Default-implementation: ignored
        virtual void dragCompleted(const QPoint& pos);

        /// Default-implementation: ignored
        ///
        /// \note dragAborted will not be called, if the drag is aborted via changing
        /// editing mode
        virtual void dragAborted();

        /// Default-implementation: ignored
        virtual void dragWheel(std::int32_t diff, double speedFactor);

        /// Default-implementation: ignored
        void dragEnterEvent(QDragEnterEvent* event) override;

        /// Default-implementation: ignored
        void dropEvent(QDropEvent* event) override;

        /// Default-implementation: ignored
        void dragMoveEvent(QDragMoveEvent* event) override;

        void mouseMoveEvent(QMouseEvent* event) override;

        /// Default: return -1
        virtual std::int32_t getSubMode() const;
    };
}
