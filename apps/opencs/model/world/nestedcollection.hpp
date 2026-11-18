#pragma once

#include <cstdint>

#include "columns.hpp"

class QVariant;

namespace CSMWorld
{
    class NestableColumn;
    struct NestedTableWrapperBase;

    class NestedCollection
    {

    public:
        NestedCollection() = default;
        virtual ~NestedCollection() = default;

        virtual void addNestedRow(std::int32_t row, std::int32_t col, std::int32_t position) = 0;

        virtual void removeNestedRows(std::int32_t row, std::int32_t column, std::int32_t subRow) = 0;

        virtual QVariant getNestedData(
            std::int32_t row, std::int32_t column, std::int32_t subRow, std::int32_t subColumn) const = 0;

        virtual void setNestedData(std::int32_t row, std::int32_t column, const QVariant& data, std::int32_t subRow,
            std::int32_t subColumn) = 0;

        virtual NestedTableWrapperBase* nestedTable(std::int32_t row, std::int32_t column) const = 0;

        virtual void setNestedTable(std::int32_t row, std::int32_t column, const NestedTableWrapperBase& nestedTable)
            = 0;

        virtual std::int32_t getNestedRowsCount(std::int32_t row, std::int32_t column) const;

        virtual std::int32_t getNestedColumnsCount(std::int32_t row, std::int32_t column) const;

        virtual NestableColumn* getNestableColumn(std::int32_t column) = 0;

        virtual std::int32_t searchNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id);
        ///< \return the column index or -1 if the requested column wasn't found.

        virtual std::int32_t findNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id);
        ///< \return the column index or throws an exception if the requested column wasn't found.
    };
}
