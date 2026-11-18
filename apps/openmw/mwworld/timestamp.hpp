#pragma once

#include <cstdint>

namespace ESM
{
    struct TimeStamp;
}

namespace MWWorld
{
    /// \brief In-game time stamp
    ///
    /// This class is based on the global variables GameHour and DaysPassed.
    class TimeStamp
    {
        float mHour;
        std::int32_t mDay;

    public:
        explicit TimeStamp(float hour = 0, std::int32_t day = 0);
        ///< \param hour [0, 23)
        /// \param day >=0

        explicit TimeStamp(const ESM::TimeStamp& esm);
        ESM::TimeStamp toEsm() const;

        float getHour() const;

        std::int32_t getDay() const;

        TimeStamp& operator+=(double hours);
        ///< \param hours >=0
    };

    bool operator==(const TimeStamp& left, const TimeStamp& right);
    bool operator!=(const TimeStamp& left, const TimeStamp& right);

    bool operator<(const TimeStamp& left, const TimeStamp& right);
    bool operator<=(const TimeStamp& left, const TimeStamp& right);

    bool operator>(const TimeStamp& left, const TimeStamp& right);
    bool operator>=(const TimeStamp& left, const TimeStamp& right);

    TimeStamp operator+(const TimeStamp& stamp, double hours);
    TimeStamp operator+(double hours, const TimeStamp& stamp);

    double operator-(const TimeStamp& left, const TimeStamp& right);
    ///< Returns the difference between \a left and \a right in in-game hours.
}
