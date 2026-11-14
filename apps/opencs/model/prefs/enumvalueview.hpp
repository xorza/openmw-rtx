#pragma once

#include <string_view>

namespace CSMPrefs
{
    struct EnumValueView
    {
        std::string_view mValue;
        std::string_view mTooltip;
    };
}
