#include "nestedcollection.hpp"

#include <cstdint>
#include <stdexcept>

#include "columnbase.hpp"
#include "columns.hpp"

std::int32_t CSMWorld::NestedCollection::getNestedRowsCount(std::int32_t row, std::int32_t column) const
{
    return 0;
}

std::int32_t CSMWorld::NestedCollection::getNestedColumnsCount(std::int32_t row, std::int32_t column) const
{
    return 0;
}

std::int32_t CSMWorld::NestedCollection::searchNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id)
{
    // Assumed that the parentColumn is always a valid index
    const NestableColumn* parent = getNestableColumn(parentColumn);
    std::int32_t nestedColumnCount = getNestedColumnsCount(0, parentColumn);
    for (std::int32_t i = 0; i < nestedColumnCount; ++i)
    {
        if (parent->nestedColumn(i).mColumnId == id)
        {
            return i;
        }
    }
    return -1;
}

std::int32_t CSMWorld::NestedCollection::findNestedColumnIndex(std::int32_t parentColumn, Columns::ColumnId id)
{
    std::int32_t index = searchNestedColumnIndex(parentColumn, id);
    if (index == -1)
    {
        throw std::logic_error("CSMWorld::NestedCollection: No such nested column");
    }
    return index;
}
