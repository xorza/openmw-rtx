#include "valuenode.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <apps/opencs/model/world/idtablebase.hpp>

CSMFilter::ValueNode::ValueNode(std::int32_t columnId, Type lowerType, Type upperType, double lower, double upper)
    : mColumnId(columnId)
    , mLower(lower)
    , mUpper(upper)
    , mLowerType(lowerType)
    , mUpperType(upperType)
{
}

bool CSMFilter::ValueNode::test(
    const CSMWorld::IdTableBase& table, std::int32_t row, const std::map<std::int32_t, std::int32_t>& columns) const
{
    const auto iter = columns.find(mColumnId);

    if (iter == columns.end())
        throw std::logic_error("invalid column in value node test");

    if (iter->second == -1)
        return true;

    QModelIndex index = table.index(row, iter->second);

    QVariant data = table.data(index);

    if (data.typeId() != QMetaType::Double && data.typeId() != QMetaType::Bool && data.typeId() != QMetaType::Int
        && data.typeId() != QMetaType::UInt && data.typeId() != QMetaType::Float)
        return false;

    double value = data.toDouble();

    switch (mLowerType)
    {
        case Type_Closed:
            if (value < mLower)
                return false;
            break;
        case Type_Open:
            if (value <= mLower)
                return false;
            break;
        case Type_Infinite:
            break;
    }

    switch (mUpperType)
    {
        case Type_Closed:
            if (value > mUpper)
                return false;
            break;
        case Type_Open:
            if (value >= mUpper)
                return false;
            break;
        case Type_Infinite:
            break;
    }

    return true;
}

std::vector<std::int32_t> CSMFilter::ValueNode::getReferencedColumns() const
{
    return std::vector<std::int32_t>(1, mColumnId);
}

std::string CSMFilter::ValueNode::toString(bool numericColumns) const
{
    std::ostringstream stream;

    stream << "value (";

    if (numericColumns)
        stream << mColumnId;
    else
        stream << "\"" << CSMWorld::Columns::getName(static_cast<CSMWorld::Columns::ColumnId>(mColumnId)) << "\"";

    stream << ", ";

    if (mLower == mUpper && mLowerType != Type_Infinite && mUpperType != Type_Infinite)
        stream << mLower;
    else
    {
        switch (mLowerType)
        {
            case Type_Closed:
                stream << "[" << mLower;
                break;
            case Type_Open:
                stream << "(" << mLower;
                break;
            case Type_Infinite:
                stream << "(";
                break;
        }

        stream << ", ";

        switch (mUpperType)
        {
            case Type_Closed:
                stream << mUpper << "]";
                break;
            case Type_Open:
                stream << mUpper << ")";
                break;
            case Type_Infinite:
                stream << ")";
                break;
        }
    }

    stream << ")";

    return stream.str();
}
