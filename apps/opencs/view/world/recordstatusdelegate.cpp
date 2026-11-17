#include "recordstatusdelegate.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <apps/opencs/model/world/columns.hpp>

#include "datadisplaydelegate.hpp"
#include "util.hpp"

class QObject;

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class CommandDispatcher;
}

CSVWorld::RecordStatusDelegate::RecordStatusDelegate(const ValueList& values, const IconList& icons,
    CSMWorld::CommandDispatcher* dispatcher, CSMDoc::Document& document, QObject* parent)
    : DataDisplayDelegate(values, icons, dispatcher, document, "Records", "status-format", parent)
{
}

CSVWorld::CommandDelegate* CSVWorld::RecordStatusDelegateFactory::makeDelegate(
    CSMWorld::CommandDispatcher* dispatcher, CSMDoc::Document& document, QObject* parent) const
{
    return new RecordStatusDelegate(mValues, mIcons, dispatcher, document, parent);
}

CSVWorld::RecordStatusDelegateFactory::RecordStatusDelegateFactory()
{
    std::vector<std::pair<std::int32_t, std::string>> enums
        = CSMWorld::Columns::getEnums(CSMWorld::Columns::ColumnId_Modification);

    static const char* sIcons[]
        = { ":list-base", ":list-modified", ":list-added", ":list-removed", ":list-removed", nullptr };

    for (std::int32_t i = 0; sIcons[i]; ++i)
    {
        auto& enumPair = enums.at(static_cast<std::size_t>(i));
        add(enumPair.first, enumPair.second.c_str(), sIcons[i]);
    }
}
