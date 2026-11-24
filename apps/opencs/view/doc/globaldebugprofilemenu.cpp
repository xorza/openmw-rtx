#include "globaldebugprofilemenu.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QModelIndex>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/model/world/record.hpp>

class QWidget;

void CSVDoc::GlobalDebugProfileMenu::rebuild()
{
    clear();

    delete mActions;
    mActions = nullptr;

    std::int32_t idColumn = mDebugProfiles->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    std::int32_t stateColumn = mDebugProfiles->findColumnIndex(CSMWorld::Columns::ColumnId_Modification);
    std::int32_t globalColumn = mDebugProfiles->findColumnIndex(CSMWorld::Columns::ColumnId_GlobalProfile);

    std::int32_t size = mDebugProfiles->rowCount();

    std::vector<QString> ids;

    for (std::int32_t i = 0; i < size; ++i)
    {
        std::int32_t state = mDebugProfiles->getData(i, stateColumn).toInt();

        bool global = mDebugProfiles->getData(i, globalColumn).toInt();

        if (state != CSMWorld::RecordBase::State_Deleted && global)
            ids.push_back(mDebugProfiles->getData(i, idColumn).toString());
    }

    mActions = new QActionGroup(this);
    connect(mActions, &QActionGroup::triggered, this, &GlobalDebugProfileMenu::actionTriggered);

    std::sort(ids.begin(), ids.end());

    for (const auto& id : ids)
    {
        mActions->addAction(addAction(id));
    }
}

CSVDoc::GlobalDebugProfileMenu::GlobalDebugProfileMenu(CSMWorld::IdTable* debugProfiles, QWidget* parent)
    : QMenu(parent)
    , mDebugProfiles(debugProfiles)
    , mActions(nullptr)
{
    rebuild();

    connect(mDebugProfiles, &CSMWorld::IdTable::rowsAboutToBeRemoved, this,
        &GlobalDebugProfileMenu::profileAboutToBeRemoved);

    connect(mDebugProfiles, &CSMWorld::IdTable::rowsInserted, this, &GlobalDebugProfileMenu::profileInserted);

    connect(mDebugProfiles, &CSMWorld::IdTable::dataChanged, this, &GlobalDebugProfileMenu::profileChanged);
}

void CSVDoc::GlobalDebugProfileMenu::updateActions(bool running)
{
    if (mActions)
        mActions->setEnabled(!running);
}

void CSVDoc::GlobalDebugProfileMenu::profileAboutToBeRemoved(
    const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    rebuild();
}

void CSVDoc::GlobalDebugProfileMenu::profileInserted(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    rebuild();
}

void CSVDoc::GlobalDebugProfileMenu::profileChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    rebuild();
}

void CSVDoc::GlobalDebugProfileMenu::actionTriggered(QAction* action)
{
    emit triggered(std::string(action->text().toUtf8().constData()));
}
