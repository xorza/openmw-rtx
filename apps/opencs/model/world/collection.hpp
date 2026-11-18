#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <QVariant>

#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/misc/strings/lower.hpp>

#include "collectionbase.hpp"
#include "columnbase.hpp"
#include "columnimp.hpp"
#include "info.hpp"
#include "land.hpp"
#include "record.hpp"
#include "ref.hpp"

namespace CSMWorld
{
    inline std::pair<std::string_view, std::string_view> parseInfoRefId(const ESM::RefId& infoId)
    {
        const auto separator = infoId.getRefIdString().find('#');
        if (separator == std::string::npos)
            throw std::runtime_error("Invalid info id: " + infoId.getRefIdString());
        const std::string_view view(infoId.getRefIdString());
        return { view.substr(0, separator), view.substr(separator + 1) };
    }

    template <typename T>
    void setRecordId(const decltype(T::mId)& id, T& record)
    {
        record.mId = id;
    }

    inline void setRecordId(const ESM::RefId& id, Info& record)
    {
        record.mId = id;
        const auto [topicId, originalId] = parseInfoRefId(id);
        record.mTopicId = ESM::RefId::stringRefId(topicId);
        record.mOriginalId = ESM::RefId::stringRefId(originalId);
    }

    template <typename T>
    auto getRecordId(const T& record)
    {
        return record.mId;
    }

    inline void setRecordId(const ESM::RefId& id, Land& record)
    {
        std::int32_t x = 0;
        std::int32_t y = 0;

        Land::parseUniqueRecordId(id.getRefIdString(), x, y);
        record.mX = x;
        record.mY = y;
    }

    inline ESM::RefId getRecordId(const Land& record)
    {
        return ESM::RefId::stringRefId(Land::createUniqueRecordId(record.mX, record.mY));
    }

    /// \brief Single-type record collection
    template <typename ESXRecordT>
    class Collection : public CollectionBase
    {
    public:
        typedef ESXRecordT ESXRecord;

    private:
        std::vector<std::unique_ptr<Record<ESXRecordT>>> mRecords;
        std::map<ESM::RefId, std::int32_t> mIndex;
        std::vector<Column<ESXRecordT>*> mColumns;

    protected:
        const std::vector<std::unique_ptr<Record<ESXRecordT>>>& getRecords() const;

        void reorderRowsImp(const std::vector<std::int32_t>& indexOrder);

        bool reorderRowsImp(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder);
        ///< Reorder the rows [baseIndex, baseIndex+newOrder.size()) according to the indices
        /// given in \a newOrder (baseIndex+newOrder[0] specifies the new index of row baseIndex).
        ///
        /// \return Success?

        std::int32_t cloneRecordImp(const ESM::RefId& origin, const ESM::RefId& dest, UniversalId::Type type);
        ///< Returns the index of the clone.

        std::int32_t touchRecordImp(const ESM::RefId& id);
        ///< Returns the index of the record on success, -1 on failure.

    public:
        Collection() = default;
        Collection(const Collection&) = delete;
        Collection& operator=(const Collection&) = delete;

        ~Collection() override;

        void add(const ESXRecordT& record);
        ///< Add a new record (modified)

        std::int32_t getSize() const override;

        ESM::RefId getId(std::int32_t index) const override;

        std::int32_t getIndex(const ESM::RefId& id) const override;

        std::int32_t getColumns() const override;

        QVariant getData(std::int32_t index, std::int32_t column) const override;

        void setData(std::int32_t index, std::int32_t column, const QVariant& data) override;

        const ColumnBase& getColumn(std::int32_t column) const override;

        void merge();
        ///< Merge modified into base.

        void purge();
        ///< Remove records that are flagged as erased.

        void removeRows(std::int32_t index, std::int32_t count) override;

        void appendBlankRecord(const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None) override;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        void cloneRecord(
            const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type) override;

        bool touchRecord(const ESM::RefId& id) override;
        ///< Change the state of a record from base to modified, if it is not already.
        ///  \return True if the record was changed.

        std::int32_t searchId(const ESM::RefId& id) const override;
        ////< Search record with \a id.
        /// \return index of record (if found) or -1 (not found)

        void replace(std::int32_t index, std::unique_ptr<RecordBase> record) override;
        ///< If the record type does not match, an exception is thrown.
        ///
        /// \attention \a record must not change the ID.

        void appendRecord(std::unique_ptr<RecordBase> record, UniversalId::Type type = UniversalId::Type_None) override;
        ///< If the record type does not match, an exception is thrown.
        ///< \param type Will be ignored, unless the collection supports multiple record types

        const Record<ESXRecordT>& getRecord(const ESM::RefId& id) const override;

        const Record<ESXRecordT>& getRecord(std::int32_t index) const override;

        std::int32_t getAppendIndex(
            const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None) const override;
        ///< \param type Will be ignored, unless the collection supports multiple record types

        std::vector<ESM::RefId> getIds(bool listDeleted = true) const override;
        ///< Return a sorted collection of all IDs
        ///
        /// \param listDeleted include deleted record in the list

        virtual void insertRecord(
            std::unique_ptr<RecordBase> record, std::int32_t index, UniversalId::Type type = UniversalId::Type_None);
        ///< Insert record before index.
        ///
        /// If the record type does not match, an exception is thrown.
        ///
        /// If the index is invalid either generally (by being out of range) or for the particular
        /// record, an exception is thrown.

        bool reorderRows(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder) override;
        ///< Reorder the rows [baseIndex, baseIndex+newOrder.size()) according to the indices
        /// given in \a newOrder (baseIndex+newOrder[0] specifies the new index of row baseIndex).
        ///
        /// \return Success?

        void addColumn(Column<ESXRecordT>* column);

        void setRecord(std::int32_t index, std::unique_ptr<Record<ESXRecordT>> record);
        ///< \attention This function must not change the ID.

        NestableColumn* getNestableColumn(std::int32_t column) const;
    };

    template <typename ESXRecordT>
    const std::vector<std::unique_ptr<Record<ESXRecordT>>>& Collection<ESXRecordT>::getRecords() const
    {
        return mRecords;
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::reorderRowsImp(const std::vector<std::int32_t>& indexOrder)
    {
        assert(indexOrder.size() == mRecords.size());
        assert(std::unordered_set(indexOrder.begin(), indexOrder.end()).size() == indexOrder.size());
        std::vector<std::unique_ptr<Record<ESXRecordT>>> orderedRecords;
        for (const std::int32_t index : indexOrder)
        {
            mIndex.at(mRecords[static_cast<std::size_t>(index)]->get().mId)
                = static_cast<std::int32_t>(orderedRecords.size());
            orderedRecords.push_back(std::move(mRecords[static_cast<std::size_t>(index)]));
        }
        mRecords = std::move(orderedRecords);
    }

    template <typename ESXRecordT>
    bool Collection<ESXRecordT>::reorderRowsImp(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder)
    {
        if (!newOrder.empty())
        {
            auto size = static_cast<std::int32_t>(newOrder.size());

            // check that all indices are present
            std::vector<std::int32_t> test(newOrder);
            std::sort(test.begin(), test.end());
            if (*test.begin() != 0 || *--test.end() != size - 1)
                return false;

            // reorder records
            std::vector<std::unique_ptr<Record<ESXRecordT>>> buffer(static_cast<std::size_t>(size));

            for (std::int32_t i = 0; i < size; ++i)
            {
                std::unique_ptr<Record<ESXRecordT>>& moved
                    = buffer[static_cast<std::size_t>(newOrder[static_cast<std::size_t>(i)])];
                moved = std::move(mRecords[static_cast<std::size_t>(baseIndex) + static_cast<std::size_t>(i)]);
                if (moved)
                    moved->setModified(moved->get());
            }

            std::move(buffer.begin(), buffer.end(), mRecords.begin() + baseIndex);

            // adjust index
            for (auto& [id, index] : mIndex)
                if (index >= baseIndex && index < baseIndex + size)
                    index = newOrder.at(static_cast<std::size_t>(index - baseIndex)) + baseIndex;
        }

        return true;
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::cloneRecordImp(
        const ESM::RefId& origin, const ESM::RefId& destination, UniversalId::Type type)
    {
        auto copy = std::make_unique<Record<ESXRecordT>>();
        copy->mModified = getRecord(origin).get();
        copy->mState = RecordBase::State_ModifiedOnly;
        setRecordId(destination, copy->get());

        if constexpr (std::is_same_v<ESXRecordT, ESM::Dialogue>)
        {
            copy->mModified.mStringId = copy->mModified.mId.getRefIdString();
        }

        const std::int32_t index = getAppendIndex(destination, type);
        insertRecord(std::move(copy), getAppendIndex(destination, type));

        return index;
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::touchRecordImp(const ESM::RefId& id)
    {
        const std::int32_t index = getIndex(id);
        Record<ESXRecordT>& record = *mRecords.at(static_cast<std::size_t>(index));
        if (record.isDeleted())
            throw std::runtime_error("attempt to touch deleted record from collection of "
                + std::string(ESXRecordT::getRecordType()) + ": " + id.toDebugString());

        if (!record.isModified())
        {
            record.setModified(record.get());
            return index;
        }

        return -1;
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::cloneRecord(
        const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type)
    {
        cloneRecordImp(origin, destination, type);
    }

    template <>
    inline void Collection<Land>::cloneRecord(
        const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type)
    {
        const std::int32_t index = cloneRecordImp(origin, destination, type);
        mRecords.at(static_cast<std::size_t>(index))->get().setPlugin(-1);
    }

    template <typename ESXRecordT>
    bool Collection<ESXRecordT>::touchRecord(const ESM::RefId& id)
    {
        return touchRecordImp(id) != -1;
    }

    template <>
    inline bool Collection<Land>::touchRecord(const ESM::RefId& id)
    {
        const std::int32_t index = touchRecordImp(id);
        if (index >= 0)
        {
            mRecords.at(static_cast<std::size_t>(index))->get().setPlugin(-1);
            return true;
        }

        return false;
    }

    template <typename ESXRecordT>
    Collection<ESXRecordT>::~Collection()
    {
        for (auto iter(mColumns.begin()); iter != mColumns.end(); ++iter)
            delete *iter;
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::add(const ESXRecordT& record)
    {
        const ESM::RefId id = getRecordId(record);

        auto iter = mIndex.find(id);

        if (iter == mIndex.end())
        {
            auto record2 = std::make_unique<Record<ESXRecordT>>();
            record2->mState = Record<ESXRecordT>::State_ModifiedOnly;
            record2->mModified = record;

            insertRecord(std::move(record2), getAppendIndex(id));
        }
        else
        {
            mRecords[static_cast<std::size_t>(iter->second)]->setModified(record);
        }
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::getSize() const
    {
        return static_cast<std::int32_t>(mRecords.size());
    }

    template <typename ESXRecordT>
    ESM::RefId Collection<ESXRecordT>::getId(std::int32_t index) const
    {
        return getRecordId(mRecords.at(static_cast<std::size_t>(index))->get());
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::getIndex(const ESM::RefId& id) const
    {
        std::int32_t index = searchId(id);

        if (index == -1)
            throw std::runtime_error("ID is not found in collection of " + std::string(ESXRecordT::getRecordType())
                + " records: " + id.getRefIdString());

        return index;
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::getColumns() const
    {
        return static_cast<std::int32_t>(mColumns.size());
    }

    template <typename ESXRecordT>
    QVariant Collection<ESXRecordT>::getData(std::int32_t index, std::int32_t column) const
    {
        return mColumns.at(static_cast<std::size_t>(column))->get(*mRecords.at(static_cast<std::size_t>(index)));
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::setData(std::int32_t index, std::int32_t column, const QVariant& data)
    {
        return mColumns.at(static_cast<std::size_t>(column))->set(*mRecords.at(static_cast<std::size_t>(index)), data);
    }

    template <typename ESXRecordT>
    const ColumnBase& Collection<ESXRecordT>::getColumn(std::int32_t column) const
    {
        return *mColumns.at(static_cast<std::size_t>(column));
    }

    template <typename ESXRecordT>
    NestableColumn* Collection<ESXRecordT>::getNestableColumn(std::int32_t column) const
    {
        if (column < 0 || column >= static_cast<std::int32_t>(mColumns.size()))
            throw std::runtime_error(
                "column index out of range [0, " + std::to_string(mColumns.size()) + "): " + std::to_string(column));

        return mColumns.at(static_cast<std::size_t>(column));
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::addColumn(Column<ESXRecordT>* column)
    {
        mColumns.push_back(column);
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::merge()
    {
        for (auto iter(mRecords.begin()); iter != mRecords.end(); ++iter)
            (*iter)->merge();

        purge();
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::purge()
    {
        std::int32_t i = 0;

        while (i < static_cast<std::int32_t>(mRecords.size()))
        {
            if (mRecords[static_cast<std::size_t>(i)]->isErased())
                removeRows(i, 1);
            else
                ++i;
        }
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::removeRows(std::int32_t index, std::int32_t count)
    {
        mRecords.erase(mRecords.begin() + index, mRecords.begin() + index + count);

        auto iter = mIndex.begin();

        while (iter != mIndex.end())
        {
            if (iter->second >= index)
            {
                if (iter->second >= index + count)
                {
                    iter->second -= count;
                    ++iter;
                }
                else
                {
                    iter = mIndex.erase(iter);
                }
            }
            else
                ++iter;
        }
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::appendBlankRecord(const ESM::RefId& id, UniversalId::Type type)
    {
        ESXRecordT record;
        setRecordId(id, record);
        record.blank();

        if constexpr (std::is_same_v<ESXRecordT, ESM::Dialogue>)
        {
            record.mStringId = record.mId.getRefIdString();
        }

        auto record2 = std::make_unique<Record<ESXRecordT>>();
        record2->mState = Record<ESXRecordT>::State_ModifiedOnly;
        record2->mModified = std::move(record);

        insertRecord(std::move(record2), getAppendIndex(id, type), type);
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::searchId(const ESM::RefId& id) const
    {
        const auto iter = mIndex.find(id);

        if (iter == mIndex.end())
            return -1;

        return iter->second;
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::replace(std::int32_t index, std::unique_ptr<RecordBase> record)
    {
        std::unique_ptr<Record<ESXRecordT>> tmp(static_cast<Record<ESXRecordT>*>(record.release()));
        mRecords.at(static_cast<std::size_t>(index)) = std::move(tmp);
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::appendRecord(std::unique_ptr<RecordBase> record, UniversalId::Type type)
    {
        std::int32_t index = getAppendIndex(getRecordId(static_cast<Record<ESXRecordT>*>(record.get())->get()), type);
        insertRecord(std::move(record), index, type);
    }

    template <typename ESXRecordT>
    std::int32_t Collection<ESXRecordT>::getAppendIndex(const ESM::RefId& id, UniversalId::Type type) const
    {
        return static_cast<std::int32_t>(mRecords.size());
    }

    template <typename ESXRecordT>
    std::vector<ESM::RefId> Collection<ESXRecordT>::getIds(bool listDeleted) const
    {
        std::vector<ESM::RefId> ids;

        for (const auto& entry : mIndex)
        {
            if (listDeleted || !mRecords[static_cast<std::size_t>(entry.second)]->isDeleted())
                ids.push_back(getRecordId(mRecords[static_cast<std::size_t>(entry.second)]->get()));
        }

        return ids;
    }

    template <typename ESXRecordT>
    const Record<ESXRecordT>& Collection<ESXRecordT>::getRecord(const ESM::RefId& id) const
    {
        std::int32_t index = getIndex(id);
        return *mRecords.at(static_cast<std::size_t>(index));
    }

    template <typename ESXRecordT>
    const Record<ESXRecordT>& Collection<ESXRecordT>::getRecord(std::int32_t index) const
    {
        return *mRecords.at(static_cast<std::size_t>(index));
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::insertRecord(
        std::unique_ptr<RecordBase> record, std::int32_t index, UniversalId::Type type)
    {
        auto size = static_cast<std::int32_t>(mRecords.size());
        if (index < 0 || index > size)
            throw std::runtime_error("index out of range");

        std::unique_ptr<Record<ESXRecordT>> record2(static_cast<Record<ESXRecordT>*>(record.release()));
        ESM::RefId id = getRecordId(record2->get());

        if (index == size)
            mRecords.push_back(std::move(record2));
        else
            mRecords.insert(mRecords.begin() + index, std::move(record2));

        if (index < size - 1)
        {
            for (auto& [key, value] : mIndex)
            {
                if (value >= index)
                    ++value;
            }
        }

        mIndex.insert(std::make_pair(id, index));
    }

    template <typename ESXRecordT>
    void Collection<ESXRecordT>::setRecord(std::int32_t index, std::unique_ptr<Record<ESXRecordT>> record)
    {
        if (getRecordId(mRecords.at(static_cast<std::size_t>(index))->get()) != getRecordId(record->get()))
            throw std::runtime_error("attempt to change the ID of a record");

        mRecords.at(static_cast<std::size_t>(index)) = std::move(record);
    }

    template <typename ESXRecordT>
    bool Collection<ESXRecordT>::reorderRows(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder)
    {
        return false;
    }
}
