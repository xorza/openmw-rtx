#pragma once

#include <cstdint>
#include <map>
#include <utility>

#include <QVariant>

#include "infocollection.hpp"
#include "nestedcollection.hpp"

namespace CSMWorld
{
    struct NestedTableWrapperBase;
    class NestableColumn;
    struct ColumnBase;
    struct Info;

    template <typename ESXRecordT>
    class NestedColumnAdapter;

    class NestedInfoCollection : public InfoCollection, public NestedCollection
    {
        std::map<const ColumnBase*, NestedColumnAdapter<Info>*> mAdapters;

        const NestedColumnAdapter<Info>& getAdapter(const ColumnBase& column) const;

    public:
        NestedInfoCollection() = default;
        ~NestedInfoCollection() override;

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

        // this method is inherited from NestedCollection, not from Collection<Info>
        NestableColumn* getNestableColumn(std::int32_t column) override;

        void addAdapter(std::pair<const ColumnBase*, NestedColumnAdapter<Info>*> adapter);
    };
}
