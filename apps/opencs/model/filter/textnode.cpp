#include "textnode.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <QRegularExpression>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/idtablebase.hpp>

CSMFilter::TextNode::TextNode(std::int32_t columnId, std::string text)
    : mColumnId(columnId)
    , mText(std::move(text))
    , mRegExp(QRegularExpression::anchoredPattern(QString::fromUtf8(mText.c_str())),
          QRegularExpression::CaseInsensitiveOption)
{
}

bool CSMFilter::TextNode::test(
    const CSMWorld::IdTableBase& table, std::int32_t row, const std::map<std::int32_t, std::int32_t>& columns) const
{
    const auto iter = columns.find(mColumnId);

    if (iter == columns.end())
        throw std::logic_error("invalid column in text node test");

    if (iter->second == -1)
        return true;

    QModelIndex index = table.index(row, iter->second);

    QVariant data = table.data(index);

    QString string;

    if (data.typeId() == QMetaType::QString)
    {
        string = data.toString();
    }
    else if ((data.typeId() == QMetaType::Int || data.typeId() == QMetaType::UInt)
        && CSMWorld::Columns::hasEnums(static_cast<CSMWorld::Columns::ColumnId>(mColumnId)))
    {
        std::int32_t value = data.toInt();

        std::vector<std::pair<std::int32_t, std::string>> enums
            = CSMWorld::Columns::getEnums(static_cast<CSMWorld::Columns::ColumnId>(mColumnId));

        if (value >= 0 && std::cmp_less(value, enums.size()))
            string = QString::fromUtf8(enums[static_cast<std::size_t>(value)].second.c_str());
    }
    else if (data.typeId() == QMetaType::Bool)
    {
        string = data.toBool() ? "true" : "false";
    }
    else if (mText.empty() && !data.isValid())
        return true;
    else
        return false;

    /// \todo make pattern syntax configurable
    QRegularExpressionMatch match = mRegExp.match(string);

    return match.hasMatch();
}

std::vector<std::int32_t> CSMFilter::TextNode::getReferencedColumns() const
{
    return std::vector<std::int32_t>(1, mColumnId);
}

std::string CSMFilter::TextNode::toString(bool numericColumns) const
{
    std::ostringstream stream;

    stream << "text (";

    if (numericColumns)
        stream << mColumnId;
    else
        stream << "\"" << CSMWorld::Columns::getName(static_cast<CSMWorld::Columns::ColumnId>(mColumnId)) << "\"";

    stream << ", \"" << mText << "\")";

    return stream.str();
}
