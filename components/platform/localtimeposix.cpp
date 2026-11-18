#include "localtime.hpp"

namespace Platform
{
    std::optional<std::tm> localTime(const std::time_t seconds)
    {
        std::tm local{};
        if (localtime_r(&seconds, &local) == nullptr)
            return std::nullopt;
        return local;
    }

    std::optional<std::tm> universalTime(const std::time_t seconds)
    {
        std::tm universal{};
        if (gmtime_r(&seconds, &universal) == nullptr)
            return std::nullopt;
        return universal;
    }
}
