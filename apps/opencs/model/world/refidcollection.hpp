#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QVariant>

#include "collectionbase.hpp"
#include "columnbase.hpp"
#include "nestedcollection.hpp"
#include "refiddata.hpp"
#include "universalid.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;
    struct MagicEffect;
}

namespace CSMWorld
{
    class RefIdAdapter;
    struct RecordBase;
    struct NestedTableWrapperBase;
    class NestedRefIdAdapterBase;

    template <typename ESXRecordT>
    class IdCollection;

    class RefIdColumn : public NestableColumn
    {
        bool mEditable;
        bool mUserEditable;

    public:
        RefIdColumn(std::int32_t columnId, Display displayType, std::int32_t flag = Flag_Table | Flag_Dialogue,
            bool editable = true, bool userEditable = true);

        bool isEditable() const override;

        bool isUserEditable() const override;
    };

    class RefIdCollection : public CollectionBase, public NestedCollection
    {
    private:
        RefIdData mData;
        std::deque<RefIdColumn> mColumns;
        std::map<UniversalId::Type, RefIdAdapter*> mAdapters;

        std::vector<std::pair<const ColumnBase*, std::map<UniversalId::Type, NestedRefIdAdapterBase*>>> mNestedAdapters;

    private:
        const RefIdAdapter& findAdapter(UniversalId::Type) const;
        ///< Throws an exception if no adaptor for \a Type can be found.

        const NestedRefIdAdapterBase& getNestedAdapter(const ColumnBase& column, UniversalId::Type type) const;

        template <typename... Args>
        RefIdColumn* addColumn(Args&&... args)
        {
            mColumns.emplace_back(std::forward<Args>(args)...);
            return &mColumns.back();
        }

    public:
        explicit RefIdCollection(const IdCollection<ESM::MagicEffect>& magicEffects);

        ~RefIdCollection() override;

        std::int32_t getSize() const override;

        ESM::RefId getId(std::int32_t index) const override;

        std::int32_t getIndex(const ESM::RefId& id) const override;

        std::int32_t getColumns() const override;

        const ColumnBase& getColumn(std::int32_t column) const override;

        QVariant getData(std::int32_t index, std::int32_t column) const override;

        void setData(std::int32_t index, std::int32_t column, const QVariant& data) override;

        void removeRows(std::int32_t index, std::int32_t count) override;

        void cloneRecord(
            const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type) override;

        bool touchRecord(const ESM::RefId& id) override;

        void appendBlankRecord(const ESM::RefId& id, UniversalId::Type type) override;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        std::int32_t searchId(const ESM::RefId& id) const override;
        ////< Search record with \a id.
        /// \return index of record (if found) or -1 (not found)

        void replace(std::int32_t index, std::unique_ptr<RecordBase> record) override;
        ///< If the record type does not match, an exception is thrown.
        ///
        /// \attention \a record must not change the ID.

        void appendRecord(std::unique_ptr<RecordBase> record, UniversalId::Type type) override;
        ///< If the record type does not match, an exception is thrown.
        ///
        ///< \param type Will be ignored, unless the collection supports multiple record types

        const RecordBase& getRecord(const ESM::RefId& id) const override;

        const RecordBase& getRecord(std::int32_t index) const override;

        void load(ESM::ESMReader& reader, bool base, UniversalId::Type type);

        std::int32_t getAppendIndex(const ESM::RefId& id, UniversalId::Type type) const override;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        std::vector<ESM::RefId> getIds(bool listDeleted) const override;
        ///< Return a sorted collection of all IDs
        ///
        /// \param listDeleted include deleted record in the list

        bool reorderRows(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder) override;
        ///< Reorder the rows [baseIndex, baseIndex+newOrder.size()) according to the indices
        /// given in \a newOrder (baseIndex+newOrder[0] specifies the new index of row baseIndex).
        ///
        /// \return Success?

        QVariant getNestedData(
            std::int32_t row, std::int32_t column, std::int32_t subRow, std::int32_t subColumn) const override;

        NestedTableWrapperBase* nestedTable(std::int32_t row, std::int32_t column) const override;

        void setNestedTable(std::int32_t row, std::int32_t column, const NestedTableWrapperBase& nestedTable) override;

        std::int32_t getNestedRowsCount(std::int32_t row, std::int32_t column) const override;

        std::int32_t getNestedColumnsCount(std::int32_t row, std::int32_t column) const override;

        NestableColumn* getNestableColumn(std::int32_t column) override;

        void setNestedData(std::int32_t row, std::int32_t column, const QVariant& data, std::int32_t subRow,
            std::int32_t subColumn) override;

        void removeNestedRows(std::int32_t row, std::int32_t column, std::int32_t subRow) override;

        void addNestedRow(std::int32_t row, std::int32_t col, std::int32_t position) override;

        void save(std::int32_t index, ESM::ESMWriter& writer) const;

        const RefIdData& getDataSet() const; // I can't figure out a better name for this one :(
        void copyTo(std::int32_t index, RefIdCollection& target) const;
    };
}
