#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <QModelIndex>
#include <QVariant>

#include "columns.hpp"
#include "idtablebase.hpp"

namespace CSMWorld
{
    class Resources;
    class UniversalId;

    class ResourceTable : public IdTableBase
    {
        const Resources* mResources;

    public:
        /// \note The feature Feature_Constant will be added implicitly.
        explicit ResourceTable(const Resources* resources, std::uint32_t features = 0);

        ~ResourceTable() override = default;

        std::int32_t rowCount(const QModelIndex& parent = QModelIndex()) const override;

        std::int32_t columnCount(const QModelIndex& parent = QModelIndex()) const override;

        QVariant data(const QModelIndex& index, std::int32_t role = Qt::DisplayRole) const override;

        QVariant headerData(
            std::int32_t section, Qt::Orientation orientation, std::int32_t role = Qt::DisplayRole) const override;

        bool setData(const QModelIndex& index, const QVariant& value, std::int32_t role = Qt::EditRole) override;

        Qt::ItemFlags flags(const QModelIndex& index) const override;

        QModelIndex index(
            std::int32_t row, std::int32_t column, const QModelIndex& parent = QModelIndex()) const override;

        QModelIndex parent(const QModelIndex& index) const override;

        QModelIndex getModelIndex(const std::string& id, std::int32_t column) const override;

        /// Return index of column with the given \a id. If no such column exists, -1 is
        /// returned.
        std::int32_t searchColumnIndex(Columns::ColumnId id) const override;

        /// Return index of column with the given \a id. If no such column exists, an
        /// exception is thrown.
        std::int32_t findColumnIndex(Columns::ColumnId id) const override;

        /// Return the UniversalId and the hint for viewing \a row. If viewing is not
        /// supported by this table, return (UniversalId::Type_None, "").
        std::pair<UniversalId, std::string> view(std::int32_t row) const override;

        /// Is \a id flagged as deleted?
        bool isDeleted(const std::string& id) const override;

        std::int32_t getColumnId(std::int32_t column) const override;

        /// Signal Qt that the data is about to change.
        void beginReset();
        /// Signal Qt that the data has been changed.
        void endReset();
    };
}
