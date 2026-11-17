#pragma once

#include <cstdint>

class QVariant;

namespace CSMWorld
{
    struct NestedTableWrapperBase;

    template <typename ESXRecordT>
    struct Record;

    template <typename ESXRecordT>
    class NestedColumnAdapter
    {
    public:
        NestedColumnAdapter() = default;

        virtual ~NestedColumnAdapter() = default;

        virtual void addRow(Record<ESXRecordT>& record, std::int32_t position) const = 0;

        virtual void removeRow(Record<ESXRecordT>& record, std::int32_t rowToRemove) const = 0;

        virtual void setTable(Record<ESXRecordT>& record, const NestedTableWrapperBase& nestedTable) const = 0;

        virtual NestedTableWrapperBase* table(const Record<ESXRecordT>& record) const = 0;

        virtual QVariant getData(
            const Record<ESXRecordT>& record, std::int32_t subRowIndex, std::int32_t subColIndex) const = 0;

        virtual void setData(Record<ESXRecordT>& record, const QVariant& value, std::int32_t subRowIndex,
            std::int32_t subColIndex) const = 0;

        virtual std::int32_t getColumnsCount(const Record<ESXRecordT>& record) const = 0;

        virtual std::int32_t getRowsCount(const Record<ESXRecordT>& record) const = 0;
    };
}
