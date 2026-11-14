#pragma once

#include <string>
#include <variant>
#include <vector>

#include <QVariant>

namespace CSVFilter
{
    struct FilterData
    {
        std::variant<std::string, QVariant> searchData;
        std::vector<std::string> columns;
    };
}
