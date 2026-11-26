#include "idtableproxymodel.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>

#include <apps/opencs/model/filter/node.hpp>

#include "columnbase.hpp"
#include "columns.hpp"
#include "idtablebase.hpp"

class QObject;

namespace
{
    std::string_view getEnumValue(const std::vector<std::pair<std::int32_t, std::string>>& values, std::int32_t index)
    {
        if (index < 0 || std::cmp_greater_equal(index, values.size()))
        {
            return {};
        }
        return values[static_cast<std::size_t>(index)].second;
    }
}

void CSMWorld::IdTableProxyModel::updateColumnMap()
{
    Q_ASSERT(mSourceModel != nullptr);

    mColumnMap.clear();
    if (mFilter)
    {
        std::vector<std::int32_t> columns = mFilter->getReferencedColumns();
<<<<<<< apps/opencs/model/world/idtableproxymodel.cpp (ours)
        for (std::int32_t column : columns)
            mColumnMap.insert(std::make_pair(
                column, mSourceModel->searchColumnIndex(static_cast<CSMWorld::Columns::ColumnId>(column))));
=======
        for (const auto& columnId : columns)
            mColumnMap.insert(std::make_pair(
                columnId, mSourceModel->searchColumnIndex(static_cast<CSMWorld::Columns::ColumnId>(columnId))));
>>>>>>> apps/opencs/model/world/idtableproxymodel.cpp (theirs)
    }
}

bool CSMWorld::IdTableProxyModel::filterAcceptsRow(std::int32_t sourceRow, const QModelIndex& sourceParent) const
{
    Q_ASSERT(mSourceModel != nullptr);

    // It is not possible to use filterAcceptsColumn() and check for
    // sourceModel()->headerData (sourceColumn, Qt::Horizontal, CSMWorld::ColumnBase::Role_Flags)
    // because the sourceColumn parameter excludes the hidden columns, i.e. wrong columns can
    // be rejected.  Workaround by disallowing tree branches (nested columns), which are not meant
    // to be visible, from the filter.
    if (sourceParent.isValid())
        return false;

    if (!mFilter)
        return true;

    return mFilter->test(*mSourceModel, sourceRow, mColumnMap);
}

CSMWorld::IdTableProxyModel::IdTableProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
    , mSourceModel(nullptr)
{
    setSortCaseSensitivity(Qt::CaseInsensitive);

    mFilterTimer.setSingleShot(true);
    std::int32_t intervalSetting = CSMPrefs::State::get()["ID Tables"]["filter-delay"].toInt();
    mFilterTimer.setInterval(intervalSetting);

    connect(&CSMPrefs::State::get(), &CSMPrefs::State::settingChanged, this,
        [this](const CSMPrefs::Setting* setting) { this->settingChanged(setting); });
    connect(&mFilterTimer, &QTimer::timeout, this, [this]() { this->timerTimeout(); });
}

QModelIndex CSMWorld::IdTableProxyModel::getModelIndex(const std::string& id, std::int32_t column) const
{
    Q_ASSERT(mSourceModel != nullptr);

    return mapFromSource(mSourceModel->getModelIndex(id, column));
}

void CSMWorld::IdTableProxyModel::setSourceModel(QAbstractItemModel* model)
{
    QSortFilterProxyModel::setSourceModel(model);

    mSourceModel = dynamic_cast<IdTableBase*>(sourceModel());
    connect(mSourceModel, &IdTableBase::rowsInserted, this, &IdTableProxyModel::sourceRowsInserted);
    connect(mSourceModel, &IdTableBase::rowsRemoved, this, &IdTableProxyModel::sourceRowsRemoved);
    connect(mSourceModel, &IdTableBase::dataChanged, this, &IdTableProxyModel::sourceDataChanged);
}

void CSMWorld::IdTableProxyModel::setFilter(const std::shared_ptr<CSMFilter::Node>& filter)
{
    mAwaitingFilter = filter;
    mFilterTimer.start();
}

bool CSMWorld::IdTableProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    Columns::ColumnId id = static_cast<Columns::ColumnId>(left.data(ColumnBase::Role_ColumnId).toInt());
    auto valuesIt = mEnumColumnCache.find(id);
    if (valuesIt == mEnumColumnCache.end())
    {
        if (Columns::hasEnums(id))
        {
            valuesIt = mEnumColumnCache.insert(std::make_pair(id, Columns::getEnums(id))).first;
        }
    }

    if (valuesIt != mEnumColumnCache.end())
    {
        std::string_view first = getEnumValue(valuesIt->second, left.data().toInt());
        std::string_view second = getEnumValue(valuesIt->second, right.data().toInt());
        return first < second;
    }
    return QSortFilterProxyModel::lessThan(left, right);
}

QString CSMWorld::IdTableProxyModel::getRecordId(std::int32_t sourceRow) const
{
    Q_ASSERT(mSourceModel != nullptr);

    std::int32_t idColumn = mSourceModel->findColumnIndex(Columns::ColumnId_Id);
    return mSourceModel->getData(sourceRow, idColumn).toString();
}

void CSMWorld::IdTableProxyModel::timerTimeout()
{
    if (mAwaitingFilter)
    {
        beginResetModel();
        mFilter = mAwaitingFilter;
        updateColumnMap();
        endResetModel();
        mAwaitingFilter.reset();
    }
}

void CSMWorld::IdTableProxyModel::settingChanged(const CSMPrefs::Setting* setting)
{
    if (*setting == "ID Tables/filter-delay")
    {
        mFilterTimer.setInterval(setting->toInt());
    }
}

void CSMWorld::IdTableProxyModel::sourceRowsInserted(
    const QModelIndex& parent, std::int32_t /*start*/, std::int32_t end)
{
    if (!parent.isValid())
    {
        emit rowAdded(getRecordId(end).toUtf8().constData());
    }
}

void CSMWorld::IdTableProxyModel::sourceRowsRemoved(
    const QModelIndex& /*parent*/, std::int32_t /*start*/, std::int32_t /*end*/)
{
}

void CSMWorld::IdTableProxyModel::sourceDataChanged(const QModelIndex& /*topLeft*/, const QModelIndex& /*bottomRight*/)
{
}
