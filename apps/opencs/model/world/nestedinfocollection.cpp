#include "nestedinfocollection.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>

#include "collection.hpp"
#include "columnbase.hpp"
#include "info.hpp"
#include "nestedcolumnadapter.hpp"
#include "nestedtablewrapper.hpp"
#include "record.hpp"

namespace CSMWorld
{
    NestedInfoCollection::~NestedInfoCollection()
    {
        for (auto& adapter : mAdapters)
        {
            delete adapter.second;
        }
    }

    void NestedInfoCollection::addAdapter(std::pair<const ColumnBase*, NestedColumnAdapter<Info>*> adapter)
    {
        mAdapters.insert(adapter);
    }

    const NestedColumnAdapter<Info>& NestedInfoCollection::getAdapter(const ColumnBase& column) const
    {
        auto iter = mAdapters.find(&column);

        if (iter == mAdapters.end())
            throw std::logic_error("No such column in the nestedidadapter");

        return *iter->second;
    }

    void NestedInfoCollection::addNestedRow(std::int32_t row, std::int32_t column, std::int32_t position)
    {
        auto record = std::make_unique<Record<Info>>();
        record->assign(Collection<Info>::getRecord(row));

        getAdapter(Collection<Info>::getColumn(column)).addRow(*record, position);

        Collection<Info>::setRecord(row, std::move(record));
    }

    void NestedInfoCollection::removeNestedRows(std::int32_t row, std::int32_t column, std::int32_t subRow)
    {
        auto record = std::make_unique<Record<Info>>();
        record->assign(Collection<Info>::getRecord(row));

        getAdapter(Collection<Info>::getColumn(column)).removeRow(*record, subRow);

        Collection<Info>::setRecord(row, std::move(record));
    }

    QVariant NestedInfoCollection::getNestedData(
        std::int32_t row, std::int32_t column, std::int32_t subRow, std::int32_t subColumn) const
    {
        return getAdapter(Collection<Info>::getColumn(column))
            .getData(Collection<Info>::getRecord(row), subRow, subColumn);
    }

    void NestedInfoCollection::setNestedData(
        std::int32_t row, std::int32_t column, const QVariant& data, std::int32_t subRow, std::int32_t subColumn)
    {
        auto record = std::make_unique<Record<Info>>();
        record->assign(Collection<Info>::getRecord(row));

        getAdapter(Collection<Info>::getColumn(column)).setData(*record, data, subRow, subColumn);

        Collection<Info>::setRecord(row, std::move(record));
    }

    CSMWorld::NestedTableWrapperBase* NestedInfoCollection::nestedTable(std::int32_t row, std::int32_t column) const
    {
        return getAdapter(Collection<Info>::getColumn(column)).table(Collection<Info>::getRecord(row));
    }

    void NestedInfoCollection::setNestedTable(
        std::int32_t row, std::int32_t column, const CSMWorld::NestedTableWrapperBase& nestedTable)
    {
        auto record = std::make_unique<Record<Info>>();
        record->assign(Collection<Info>::getRecord(row));

        getAdapter(Collection<Info>::getColumn(column)).setTable(*record, nestedTable);

        Collection<Info>::setRecord(row, std::move(record));
    }

    std::int32_t NestedInfoCollection::getNestedRowsCount(std::int32_t row, std::int32_t column) const
    {
        return getAdapter(Collection<Info>::getColumn(column)).getRowsCount(Collection<Info>::getRecord(row));
    }

    std::int32_t NestedInfoCollection::getNestedColumnsCount(std::int32_t row, std::int32_t column) const
    {
        return getAdapter(Collection<Info>::getColumn(column)).getColumnsCount(Collection<Info>::getRecord(row));
    }

    CSMWorld::NestableColumn* NestedInfoCollection::getNestableColumn(std::int32_t column)
    {
        return Collection<Info>::getNestableColumn(column);
    }
}
