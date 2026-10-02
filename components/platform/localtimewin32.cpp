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
}
