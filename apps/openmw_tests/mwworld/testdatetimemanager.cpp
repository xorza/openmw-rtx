#include <gtest/gtest.h>

#include "apps/openmw/mwworld/datetimemanager.hpp"

namespace MWWorld
{
    namespace
    {
        /// **A jump is a move past the frame's own step, either way round the day.** A frame of a
        /// sixtieth of a second at the game's thirty to one is 1/7200 of an hour. A script holding
        /// the hour writes it back one step, the float's rounding included — at 23.9, a float's
        /// spacing is 2^-19 of an hour — and that is no jump; `set gamehour to 21` from noon is
        /// nine hours, and so is the way from 23 to 2 round midnight, three hours. A frame that did
        /// not move the clock jumps at anything past the float's rounding.
        TEST(MWWorldDateTimeManagerTest, aWriteJumpsTheClockWhereItMovesItPastTheFramesStep)
        {
            constexpr double step = 1.0 / 7200.0;

            const float held = 23.9f;
            const float stepped = static_cast<float>(static_cast<double>(held) + step);
            EXPECT_FALSE(DateTimeManager::jumps(stepped, held, step)) << "a held hour cut every frame";
            EXPECT_FALSE(DateTimeManager::jumps(held, held, 0.0)) << "the same hour written again";

            EXPECT_TRUE(DateTimeManager::jumps(12.0f, 21.0f, step));
            EXPECT_TRUE(DateTimeManager::jumps(23.0f, 2.0f, step)) << "round midnight is three hours";
            EXPECT_TRUE(DateTimeManager::jumps(2.0f, 23.0f, step)) << "and three hours the other way";
            EXPECT_TRUE(DateTimeManager::jumps(12.0f, 12.0f + 2.0f * static_cast<float>(step), step))
                << "two frames' worth in one";
            EXPECT_FALSE(DateTimeManager::jumps(0.0f, 23.99999f, step)) << "a step back across midnight";
        }
    }
}
