#pragma once

#include <QAction>
#include <QString>
#include <utility>

#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/universalid.hpp>

class QTableView;

namespace CSMWorld
{
    class Data;
}

namespace CSVWorld
{
    class TableEditIdAction : public QAction
    {
        const QTableView& mTable;
        CSMWorld::Data& mData;
        CSMWorld::UniversalId mCurrentId;

    public:
        TableEditIdAction(const QTableView& table, CSMWorld::Data& data, QWidget* parent = nullptr);

        bool setCell(int row, int column);

        CSMWorld::UniversalId getCurrentId() const;
    };
}
