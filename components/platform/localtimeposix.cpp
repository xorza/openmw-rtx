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
}
