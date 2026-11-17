#include "localtime.hpp"

namespace Platform
{
    std::optional<std::tm> localTime(const std::time_t seconds)
    {
        std::tm local{};
        if (localtime_s(&local, &seconds) != 0)
            return std::nullopt;
        return local;
    }

    std::optional<std::tm> universalTime(const std::time_t seconds)
    {
        std::tm universal{};
        if (gmtime_s(&universal, &seconds) != 0)
            return std::nullopt;
        return universal;
    }
}
