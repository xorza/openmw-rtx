#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace MWWorld
{
    inline const double maxFloatHour = static_cast<double>(std::nextafter(24.0f, 0.0f));

    class Duration
    {
    public:
        static Duration fromHours(double hours)
        {
            if (hours < 0)
                throw std::runtime_error("Negative hours is not supported Duration");

            return Duration(static_cast<std::int32_t>(hours / 24),
                static_cast<float>(std::min(std::fmod(hours, 24), maxFloatHour)));
        }

        std::int32_t getDays() const { return mDays; }

        float getHours() const { return mHours; }

    private:
        std::int32_t mDays;
        float mHours;

        explicit Duration(std::int32_t days, float hours)
            : mDays(days)
            , mHours(hours)
        {
        }
    };
}
