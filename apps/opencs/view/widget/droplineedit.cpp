#include "droplineedit.hpp"

#include <QDropEvent>
#include <string>

#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/tablemimedata.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/world/dragdroputils.hpp>

CSVWidget::DropLineEdit::DropLineEdit(CSMWorld::ColumnBase::Display type, QWidget* parent)
    : QLineEdit(parent)
    , mDropType(type)
{
    setAcceptDrops(true);
}

void CSVWidget::DropLineEdit::dragEnterEvent(QDragEnterEvent* event)
{
    if (CSVWorld::DragDropUtils::canAcceptData(*event, mDropType))
    {
        event->acceptProposedAction();
    }
}

void CSVWidget::DropLineEdit::dragMoveEvent(QDragMoveEvent* event)
{
    if (CSVWorld::DragDropUtils::canAcceptData(*event, mDropType))
    {
        event->accept();
    }
}

void CSVWidget::DropLineEdit::dropEvent(QDropEvent* event)
{
    if (CSVWorld::DragDropUtils::canAcceptData(*event, mDropType))
    {
        CSMWorld::UniversalId id = CSVWorld::DragDropUtils::getAcceptedData(*event, mDropType);
        setText(QString::fromUtf8(id.getId().c_str()));
        emit tableMimeDataDropped(id, CSVWorld::DragDropUtils::getTableMimeData(*event)->getDocumentPtr());
    }
}
