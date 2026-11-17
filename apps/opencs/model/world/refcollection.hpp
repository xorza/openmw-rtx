#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "collection.hpp"
#include "record.hpp"
#include "ref.hpp"
#include "universalid.hpp"

namespace ESM
{
    class ESMReader;
}

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    struct Cell;

    template <>
    void Collection<CellRef>::removeRows(std::int32_t index, std::int32_t count);

    template <>
    void Collection<CellRef>::insertRecord(
        std::unique_ptr<RecordBase> record, std::int32_t index, UniversalId::Type type);

    /// \brief References in cells
    class RefCollection final : public Collection<CellRef>
    {
        Collection<Cell>& mCells;
        std::map<std::uint32_t, std::int32_t> mRefIndex; // CellRef index keyed by CSMWorld::CellRef::mIdNum

        std::int32_t mNextId;
        std::uint32_t mHighestUsedRefNum = 0;

        std::uint32_t extractIdNum(std::string_view id) const;

        std::uint32_t getNextRefNum();

        std::int32_t getIntIndex(std::uint32_t id) const;

        std::int32_t searchId(std::uint32_t id) const;

    public:
        // MSVC needs the constructor for a class inheriting a template to be defined in header
        explicit RefCollection(Collection<Cell>& cells)
            : mCells(cells)
            , mNextId(0)
        {
        }

        void load(ESM::ESMReader& reader, std::int32_t cellIndex, bool base,
            std::map<ESM::RefNum, std::uint32_t>& cache, CSMDoc::Messages& messages);
        ///< Load a sequence of references.

        std::string getNewId();

        void removeRows(std::int32_t index, std::int32_t count) override;

        void appendBlankRecord(const ESM::RefId& id, UniversalId::Type type = UniversalId::Type_None) override;

        void cloneRecord(
            const ESM::RefId& origin, const ESM::RefId& destination, const UniversalId::Type type) override;

        std::int32_t searchId(const ESM::RefId& id) const override;

        void appendRecord(std::unique_ptr<RecordBase> record, UniversalId::Type type = UniversalId::Type_None) override;

        void insertRecord(std::unique_ptr<RecordBase> record, std::int32_t index,
            UniversalId::Type type = UniversalId::Type_None) override;
    };
}
