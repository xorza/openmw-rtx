#pragma once

#include <QVariant>
#include <string>
#include <variant>
#include <vector>

namespace CSVFilter
{
    struct FilterData
    {
        std::variant<std::string, QVariant> searchData;
        std::vector<std::string> columns;
    };
}
