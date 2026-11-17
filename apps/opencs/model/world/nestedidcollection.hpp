#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>

#include "collection.hpp"
#include "nestedcollection.hpp"
#include "nestedcolumnadapter.hpp"

namespace ESM
{
    class ESMReader;
}

namespace CSMWorld
{
    struct NestedTableWrapperBase;
    struct Cell;
    struct ColumnBase;

    template <typename ESXRecordT>
    class IdCollection;

    template <typename ESXRecordT>
    class NestedColumnAdapter;

    template <typename ESXRecordT>
    class NestedIdCollection : public IdCollection<ESXRecordT>, public NestedCollection
    {
        std::map<const ColumnBase*, NestedColumnAdapter<ESXRecordT>*> mAdapters;

        const NestedColumnAdapter<ESXRecordT>& getAdapter(const ColumnBase& column) const;

    public:
        NestedIdCollection();
        ~NestedIdCollection() override;

        void addNestedRow(std::int32_t row, std::int32_t column, std::int32_t position) override;

        void removeNestedRows(std::int32_t row, std::int32_t column, std::int32_t subRow) override;

        QVariant getNestedData(
            std::int32_t row, std::int32_t column, std::int32_t subRow, std::int32_t subColumn) const override;

        void setNestedData(std::int32_t row, std::int32_t column, const QVariant& data, std::int32_t subRow,
            std::int32_t subColumn) override;

        NestedTableWrapperBase* nestedTable(std::int32_t row, std::int32_t column) const override;

        void setNestedTable(std::int32_t row, std::int32_t column, const NestedTableWrapperBase& nestedTable) override;

        std::int32_t getNestedRowsCount(std::int32_t row, std::int32_t column) const override;

        std::int32_t getNestedColumnsCount(std::int32_t row, std::int32_t column) const override;

        // this method is inherited from NestedCollection, not from Collection<ESXRecordT>
        NestableColumn* getNestableColumn(std::int32_t column) override;

        void addAdapter(std::pair<const ColumnBase*, NestedColumnAdapter<ESXRecordT>*> adapter);
    };

    template <typename ESXRecordT>
    NestedIdCollection<ESXRecordT>::NestedIdCollection()
    {
    }

    template <typename ESXRecordT>
    NestedIdCollection<ESXRecordT>::~NestedIdCollection()
    {
        for (auto iter(mAdapters.begin()); iter != mAdapters.end(); ++iter)
        {
            delete (*iter).second;
        }
    }

    template <typename ESXRecordT>
    void NestedIdCollection<ESXRecordT>::addAdapter(
        std::pair<const ColumnBase*, NestedColumnAdapter<ESXRecordT>*> adapter)
    {
        mAdapters.insert(adapter);
    }

    template <typename ESXRecordT>
    const NestedColumnAdapter<ESXRecordT>& NestedIdCollection<ESXRecordT>::getAdapter(const ColumnBase& column) const
    {
        auto iter = mAdapters.find(&column);

        if (iter == mAdapters.end())
            throw std::logic_error("No such column in the nestedidadapter");

        return *iter->second;
    }

    template <typename ESXRecordT>
    void NestedIdCollection<ESXRecordT>::addNestedRow(std::int32_t row, std::int32_t column, std::int32_t position)
    {
        auto record = std::make_unique<Record<ESXRecordT>>();
        record->assign(Collection<ESXRecordT>::getRecord(row));

        getAdapter(Collection<ESXRecordT>::getColumn(column)).addRow(*record, position);

        Collection<ESXRecordT>::setRecord(row, std::move(record));
    }

    template <typename ESXRecordT>
    void NestedIdCollection<ESXRecordT>::removeNestedRows(std::int32_t row, std::int32_t column, std::int32_t subRow)
    {
        auto record = std::make_unique<Record<ESXRecordT>>();
        record->assign(Collection<ESXRecordT>::getRecord(row));

        getAdapter(Collection<ESXRecordT>::getColumn(column)).removeRow(*record, subRow);

        Collection<ESXRecordT>::setRecord(row, std::move(record));
    }

    template <typename ESXRecordT>
    QVariant NestedIdCollection<ESXRecordT>::getNestedData(
        std::int32_t row, std::int32_t column, std::int32_t subRow, std::int32_t subColumn) const
    {
        return getAdapter(Collection<ESXRecordT>::getColumn(column))
            .getData(Collection<ESXRecordT>::getRecord(row), subRow, subColumn);
    }

    template <typename ESXRecordT>
    void NestedIdCollection<ESXRecordT>::setNestedData(
        std::int32_t row, std::int32_t column, const QVariant& data, std::int32_t subRow, std::int32_t subColumn)
    {
        auto record = std::make_unique<Record<ESXRecordT>>();
        record->assign(Collection<ESXRecordT>::getRecord(row));

        getAdapter(Collection<ESXRecordT>::getColumn(column)).setData(*record, data, subRow, subColumn);

        Collection<ESXRecordT>::setRecord(row, std::move(record));
    }

    template <typename ESXRecordT>
    CSMWorld::NestedTableWrapperBase* NestedIdCollection<ESXRecordT>::nestedTable(
        std::int32_t row, std::int32_t column) const
    {
        return getAdapter(Collection<ESXRecordT>::getColumn(column)).table(Collection<ESXRecordT>::getRecord(row));
    }

    template <typename ESXRecordT>
    void NestedIdCollection<ESXRecordT>::setNestedTable(
        std::int32_t row, std::int32_t column, const CSMWorld::NestedTableWrapperBase& nestedTable)
    {
        auto record = std::make_unique<Record<ESXRecordT>>();
        record->assign(Collection<ESXRecordT>::getRecord(row));

        getAdapter(Collection<ESXRecordT>::getColumn(column)).setTable(*record, nestedTable);

        Collection<ESXRecordT>::setRecord(row, std::move(record));
    }

    template <typename ESXRecordT>
    std::int32_t NestedIdCollection<ESXRecordT>::getNestedRowsCount(std::int32_t row, std::int32_t column) const
    {
        return getAdapter(Collection<ESXRecordT>::getColumn(column))
            .getRowsCount(Collection<ESXRecordT>::getRecord(row));
    }

    template <typename ESXRecordT>
    std::int32_t NestedIdCollection<ESXRecordT>::getNestedColumnsCount(std::int32_t row, std::int32_t column) const
    {
        const ColumnBase& nestedColumn = Collection<ESXRecordT>::getColumn(column);
        std::int32_t numRecords = Collection<ESXRecordT>::getSize();
        if (row >= 0 && row < numRecords)
        {
            const Record<ESXRecordT>& record = Collection<ESXRecordT>::getRecord(row);
            return getAdapter(nestedColumn).getColumnsCount(record);
        }
        else
        {
            // If the row is invalid (or there no records), retrieve the column count using a blank record
            const Record<ESXRecordT> record;
            return getAdapter(nestedColumn).getColumnsCount(record);
        }
    }

    template <typename ESXRecordT>
    CSMWorld::NestableColumn* NestedIdCollection<ESXRecordT>::getNestableColumn(std::int32_t column)
    {
        return Collection<ESXRecordT>::getNestableColumn(column);
    }
}
