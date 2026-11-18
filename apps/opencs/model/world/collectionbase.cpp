#include "collectionbase.hpp"

#include <cstdint>
#include <stdexcept>

#include "columnbase.hpp"
#include "columns.hpp"
#include "universalid.hpp"

std::int32_t CSMWorld::CollectionBase::getInsertIndex(
    const ESM::RefId& id, UniversalId::Type type, RecordBase* record) const
{
    return getAppendIndex(id, type);
}

std::int32_t CSMWorld::CollectionBase::searchColumnIndex(Columns::ColumnId id) const
{
    std::int32_t columns = getColumns();

    for (std::int32_t i = 0; i < columns; ++i)
        if (getColumn(i).mColumnId == id)
            return i;

    return -1;
}

std::int32_t CSMWorld::CollectionBase::findColumnIndex(Columns::ColumnId id) const
{
    std::int32_t index = searchColumnIndex(id);

    if (index == -1)
        throw std::logic_error("invalid column index");

    return index;
}
