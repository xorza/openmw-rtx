#include <chrono>
#include <ctime>
#include <string_view>

#include <gtest/gtest.h>

#include <components/debug/debugging.hpp>

namespace
{
    /// **The stamp a log line starts with**, the game's and the crash monitor's alike: the local
    /// time to the millisecond and the level's letter. Five past one and five seconds in the
    /// afternoon of a January day, wherever the test runs, and 123 milliseconds into the second.
    TEST(DebugStampTest, aStampIsTheLocalTimeToTheMillisecondAndTheLevel)
    {
        std::tm local{};
        local.tm_year = 2020 - 1900;
        local.tm_mon = 0;
        local.tm_mday = 15;
        local.tm_hour = 13;
        local.tm_min = 4;
        local.tm_sec = 5;
        local.tm_isdst = -1;
        const auto now = std::chrono::system_clock::from_time_t(std::mktime(&local)) + std::chrono::milliseconds(123);

        char text[Debug::sStampCapacity];
        EXPECT_EQ(std::string_view(text, Debug::writeStamp(text, Debug::Error, now)), "[13:04:05.123 E] ");
        EXPECT_EQ(std::string_view(text, Debug::writeStamp(text, Debug::Warning, now)), "[13:04:05.123 W] ");
    }
}
