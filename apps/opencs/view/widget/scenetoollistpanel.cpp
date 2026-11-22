#include "scenetoollistpanel.hpp"

#include <cstddef>
#include <cstdint>

#include <QAbstractItemView>
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMargins>
#include <QModelIndex>
#include <QPoint>
#include <QStyle>
#include <QTableWidget>

CSVWidget::SceneToolListPanel::SceneToolListPanel(std::int32_t textColumnCount, QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , mTable(new QTableWidget(0, textColumnCount + 1, this))
{
    auto* layout = new QHBoxLayout(this);

    layout->setContentsMargins(QMargins(0, 0, 0, 0));

    mTable->setShowGrid(false);
    mTable->verticalHeader()->hide();
    mTable->horizontalHeader()->hide();
    mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mTable->horizontalHeader()->setSectionResizeMode(textColumnCount, QHeaderView::ResizeToContents);
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    layout->addWidget(mTable);

    connect(mTable, &QTableWidget::clicked, this, &SceneToolListPanel::clicked);
}

void CSVWidget::SceneToolListPanel::setRows(const std::vector<QStringList>& rows)
{
    const auto rowCount = static_cast<std::int32_t>(rows.size());
    const std::int32_t removeColumn = mTable->columnCount() - 1;

    mTable->setRowCount(rowCount);

    for (std::int32_t row = 0; row < rowCount; ++row)
    {
        for (std::int32_t column = 0; column < removeColumn; ++column)
            mTable->setItem(row, column, new QTableWidgetItem(rows[static_cast<std::size_t>(row)].value(column)));

        mTable->setItem(row, removeColumn,
            new QTableWidgetItem(QApplication::style()->standardIcon(QStyle::SP_TitleBarCloseButton), ""));
    }

    if (rows.empty())
        hide();
}

void CSVWidget::SceneToolListPanel::showAt(const QPoint& position)
{
    if (mTable->rowCount() == 0)
        return;

    move(position);
    show();
}

void CSVWidget::SceneToolListPanel::clicked(const QModelIndex& index)
{
    if (index.column() == mTable->columnCount() - 1)
        emit removeRequest(index.row());
    else
    {
        hide();
        emit selectRequest(index.row());
    }
}
