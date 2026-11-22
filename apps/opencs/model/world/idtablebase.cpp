#include "idtablebase.hpp"

#include <cstdint>

#include "universalid.hpp"
#include "variantconversion.hpp"

CSMWorld::IdTableBase::IdTableBase(std::uint32_t features)
    : mFeatures(features)
{
}

QVariant CSMWorld::IdTableBase::getData(std::int32_t row, std::int32_t column, std::int32_t role) const
{
    return data(index(row, column), role);
}

CSMWorld::UniversalId CSMWorld::IdTableBase::getUniversalId(std::int32_t row) const
{
    std::int32_t idColumn = findColumnIndex(Columns::ColumnId_Id);
    std::int32_t typeColumn = findColumnIndex(Columns::ColumnId_RecordType);

    const auto type = static_cast<UniversalId::Type>(getData(row, typeColumn).toInt());
    const auto id = getString(getData(row, idColumn));
    return UniversalId(type, id);
}

CSMWorld::ColumnBase::Display CSMWorld::IdTableBase::getColumnDisplay(std::int32_t column) const
{
    return static_cast<ColumnBase::Display>(headerData(column, Qt::Horizontal, ColumnBase::Role_Display).toInt());
}

std::uint32_t CSMWorld::IdTableBase::getFeatures() const
{
    return mFeatures;
}
