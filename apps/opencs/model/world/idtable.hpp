#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QModelIndex>
#include <QVariant>

#include "columns.hpp"
#include "idtablebase.hpp"
#include "universalid.hpp"

class QObject;

namespace ESM
{
    struct LandTexture;
}

namespace CSMWorld
{
    class CollectionBase;
    struct RecordBase;
    template <typename ESXRecordT>
    struct Record;

    class IdTable : public IdTableBase
    {
        Q_OBJECT

    private:
        CollectionBase* mIdCollection;

        // not implemented
        IdTable(const IdTable&);
        IdTable& operator=(const IdTable&);

    public:
        explicit IdTable(CollectionBase* idCollection, std::uint32_t features = 0);
        ///< The ownership of \a idCollection is not transferred.

        ~IdTable() override = default;

        std::int32_t rowCount(const QModelIndex& parent = QModelIndex()) const override;

        std::int32_t columnCount(const QModelIndex& parent = QModelIndex()) const override;

        QVariant data(const QModelIndex& index, std::int32_t role = Qt::DisplayRole) const override;

        QVariant headerData(
            std::int32_t section, Qt::Orientation orientation, std::int32_t role = Qt::DisplayRole) const override;

        bool setData(const QModelIndex& index, const QVariant& value, std::int32_t role = Qt::EditRole) override;

        Qt::ItemFlags flags(const QModelIndex& index) const override;

        bool removeRows(std::int32_t row, std::int32_t count, const QModelIndex& parent = QModelIndex()) override;

        QModelIndex index(
            std::int32_t row, std::int32_t column, const QModelIndex& parent = QModelIndex()) const override;

        QModelIndex parent(const QModelIndex& index) const override;

        void addRecord(const std::string& id, UniversalId::Type type = UniversalId::Type_None);
        ///< \param type Will be ignored, unless the collection supports multiple record types

        void addRecordWithData(const std::string& id, const std::map<std::int32_t, QVariant>& data,
            UniversalId::Type type = UniversalId::Type_None);
        ///< \param type Will be ignored, unless the collection supports multiple record types

        void cloneRecord(
            const ESM::RefId& origin, const ESM::RefId& destination, UniversalId::Type type = UniversalId::Type_None);

        bool touchRecord(const std::string& id);
        ///< Will change the record state to modified, if it is not already.

        std::string getId(std::int32_t row) const;

        QModelIndex getModelIndex(const std::string& id, std::int32_t column) const override;

        void setRecord(
            const std::string& id, std::unique_ptr<RecordBase> record, UniversalId::Type type = UniversalId::Type_None);
        ///< Add record or overwrite existing record.

        const RecordBase& getRecord(const std::string& id) const;

        std::int32_t searchColumnIndex(Columns::ColumnId id) const override;
        ///< Return index of column with the given \a id. If no such column exists, -1 is returned.

        std::int32_t findColumnIndex(Columns::ColumnId id) const override;
        ///< Return index of column with the given \a id. If no such column exists, an exception is
        /// thrown.

        void reorderRows(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder);
        ///< Reorder the rows [baseIndex, baseIndex+newOrder.size()) according to the indices
        /// given in \a newOrder (baseIndex+newOrder[0] specifies the new index of row baseIndex).

        std::pair<UniversalId, std::string> view(std::int32_t row) const override;
        ///< Return the UniversalId and the hint for viewing \a row. If viewing is not
        /// supported by this table, return (UniversalId::Type_None, "").

        /// Is \a id flagged as deleted?
        bool isDeleted(const std::string& id) const override;

        std::int32_t getColumnId(std::int32_t column) const override;

    protected:
        virtual CollectionBase* idCollection() const;
    };

    class LandTextureIdTable : public IdTable
    {
    public:
        explicit LandTextureIdTable(CollectionBase* idCollection, std::uint32_t features = 0);

        const CSMWorld::Record<ESM::LandTexture>* searchRecord(std::uint16_t index, std::int32_t plugin) const;
    };
}
