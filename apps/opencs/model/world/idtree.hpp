#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <QModelIndex>
#include <QString>
#include <QVariant>

#include "columns.hpp"
#include "idtable.hpp"

/*! \brief
 * Class for holding the model. Uses typical qt table abstraction/interface for granting access
 * to the individiual fields of the records, Some records are holding nested data (for instance
 * inventory list of the npc). In cases like this, table model offers interface to access
 * nested data in the qt way - that is specify parent. Since some of those nested data require
 * multiple columns to represent information, single int (default way to index model in the
 * qmodelindex) is not sufficiant. Therefore tablemodelindex class can hold two ints for the
 * sake of indexing two dimensions of the table. This model does not support multiple levels of
 * the nested data. Vast majority of methods makes sense only for the top level data.
 */

namespace CSMWorld
{
    class CollectionBase;
    class NestedCollection;
    struct NestedTableWrapperBase;

    class IdTree : public IdTable
    {
        Q_OBJECT

    private:
        NestedCollection* mNestedCollection;

        std::uint32_t foldIndexAddress(const QModelIndex& index) const;
        std::pair<std::int32_t, std::int32_t> unfoldIndexAddress(std::uint32_t id) const;

    public:
        IdTree(NestedCollection* nestedCollection, CollectionBase* idCollection, std::uint32_t features = 0);
        ///< The ownerships of \a nestedCollecton and \a idCollection are not transferred.
        IdTree(const IdTree&) = delete;
        IdTree& operator=(const IdTree&) = delete;
        ~IdTree() override = default;

        std::int32_t rowCount(const QModelIndex& parent = QModelIndex()) const override;

        std::int32_t columnCount(const QModelIndex& parent = QModelIndex()) const override;

        QVariant data(const QModelIndex& index, std::int32_t role = Qt::DisplayRole) const override;

        bool setData(const QModelIndex& index, const QVariant& value, std::int32_t role = Qt::EditRole) override;

        Qt::ItemFlags flags(const QModelIndex& index) const override;

        bool removeRows(std::int32_t row, std::int32_t count, const QModelIndex& parent = QModelIndex()) override;

        QModelIndex index(
            std::int32_t row, std::int32_t column, const QModelIndex& parent = QModelIndex()) const override;

        QModelIndex parent(const QModelIndex& index) const override;

        QModelIndex getNestedModelIndex(const std::string& id, std::int32_t column) const;

        QVariant nestedHeaderData(std::int32_t section, std::int32_t subSection, Qt::Orientation orientation,
            std::int32_t role = Qt::DisplayRole) const;

        NestedTableWrapperBase* nestedTable(const QModelIndex& index) const;

        void setNestedTable(const QModelIndex& index, const NestedTableWrapperBase& nestedTable);

        void addNestedRow(const QModelIndex& parent, std::int32_t position);

        bool hasChildren(const QModelIndex& index) const override;

        virtual std::int32_t searchNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id);
        ///< \return the column index or -1 if the requested column wasn't found.

        virtual std::int32_t findNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id);
        ///< \return the column index or throws an exception if the requested column wasn't found.

    signals:

        void resetStart(const QString& id);

        void resetEnd(const QString& id);
    };
}
