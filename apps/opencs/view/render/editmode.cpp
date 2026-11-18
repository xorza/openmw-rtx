#include "editmode.hpp"

#include <cstdint>

#include <apps/opencs/view/widget/modebutton.hpp>

#include "worldspacewidget.hpp"

class QMouseEvent;
class QWidget;

namespace CSVWidget
{
    class SceneToolbar;
}

CSVRender::WorldspaceWidget& CSVRender::EditMode::getWorldspaceWidget()
{
    return *mWorldspaceWidget;
}

const CSVRender::WorldspaceWidget& CSVRender::EditMode::getWorldspaceWidget() const
{
    return *mWorldspaceWidget;
}

CSVRender::EditMode::EditMode(
    WorldspaceWidget* worldspaceWidget, const QIcon& icon, std::uint32_t mask, const QString& tooltip, QWidget* parent)
    : ModeButton(icon, tooltip, parent)
    , mWorldspaceWidget(worldspaceWidget)
    , mMask(mask)
{
}

std::uint32_t CSVRender::EditMode::getInteractionMask() const
{
    return mMask;
}

void CSVRender::EditMode::activate(CSVWidget::SceneToolbar* toolbar)
{
    mWorldspaceWidget->setInteractionMask(mMask);
    mWorldspaceWidget->clearSelection(static_cast<std::int32_t>(~mMask));
}

void CSVRender::EditMode::setEditLock(bool locked) {}

void CSVRender::EditMode::primaryOpenPressed(const WorldspaceHitResult& hit) {}

void CSVRender::EditMode::primaryEditPressed(const WorldspaceHitResult& hit) {}

void CSVRender::EditMode::secondaryEditPressed(const WorldspaceHitResult& hit) {}

void CSVRender::EditMode::primarySelectPressed(const WorldspaceHitResult& hit) {}

void CSVRender::EditMode::secondarySelectPressed(const WorldspaceHitResult& hit) {}

void CSVRender::EditMode::tertiarySelectPressed(const WorldspaceHitResult& hit) {}

bool CSVRender::EditMode::primaryEditStartDrag(const QPoint& pos)
{
    return false;
}

bool CSVRender::EditMode::secondaryEditStartDrag(const QPoint& pos)
{
    return false;
}

bool CSVRender::EditMode::primarySelectStartDrag(const QPoint& pos)
{
    return false;
}

bool CSVRender::EditMode::secondarySelectStartDrag(const QPoint& pos)
{
    return false;
}

void CSVRender::EditMode::drag(const QPoint& pos, std::int32_t diffX, std::int32_t diffY, double speedFactor) {}

void CSVRender::EditMode::dragCompleted(const QPoint& pos) {}

void CSVRender::EditMode::dragAborted() {}

void CSVRender::EditMode::dragWheel(std::int32_t diff, double speedFactor) {}

void CSVRender::EditMode::dragEnterEvent(QDragEnterEvent* event) {}

void CSVRender::EditMode::dropEvent(QDropEvent* event) {}

void CSVRender::EditMode::dragMoveEvent(QDragMoveEvent* event) {}

void CSVRender::EditMode::mouseMoveEvent(QMouseEvent* event) {}

std::int32_t CSVRender::EditMode::getSubMode() const
{
    return -1;
}
