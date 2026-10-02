#include <ctime>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include <components/platform/localtime.hpp>

namespace
{
    /// **The local calendar's time, and nothing past it**: a day after the epoch comes back to the
    /// same second through `mktime`, which reads a calendar time as local, and the last second a
    /// `time_t` holds is billions of years past any calendar's year.
    TEST(PlatformLocalTimeTest, aTimeIsTheLocalCalendarsOrNothing)
    {
        const std::time_t day = 86400;
        std::optional<std::tm> local = Platform::localTime(day);
        ASSERT_TRUE(local.has_value());
        EXPECT_EQ(std::mktime(&*local), day);

        EXPECT_EQ(Platform::localTime(std::numeric_limits<std::time_t>::max()), std::nullopt);
    }
}
