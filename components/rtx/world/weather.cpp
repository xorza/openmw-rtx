#include "weather.hpp"

#include <cstdint>
#include <utility>

#include <components/crashcatcher/crash.hpp>
#include <components/esm/refid.hpp>
#include <components/esm/stringrefid.hpp>
#include <components/esm3/loadregn.hpp>

namespace Rtx
{
    static_assert(sWeatherCount == ESM::Weather::Length, "a weather the content's table does not hold");

    std::optional<Weather> weatherNamed(std::string_view name)
    {
        return weatherOfScriptId(ESM::Weather::refIdToIndex(ESM::RefId::stringRefId(name)));
    }

    std::optional<Weather> weatherOfScriptId(const std::int32_t id)
    {
        if (id < 0 || std::cmp_greater_equal(id, sWeatherCount))
            return std::nullopt;

        return static_cast<Weather>(id);
    }

    std::string_view nameOf(const Weather weather)
    {
        Crash::contract(weather < Weather::Count, "a weather past the ten");

        // The table's own interned spelling, which outlives every caller.
        const ESM::RefId id = ESM::Weather::indexToRefId(static_cast<std::int32_t>(weather));
        const auto* const named = id.getIf<ESM::StringRefId>();
        Crash::contract(named != nullptr, "a weather ESM::Weather does not name by a string");
        return named->getValue();
    }
}
