#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/uniquehold.hpp>

namespace
{
    /// What the traits below closed, in order.
    std::vector<std::int32_t> sClosed;

    struct Recorded
    {
        using Handle = std::int32_t;
        static constexpr Handle sNone = -1;
        static void close(Handle handle) noexcept { sClosed.push_back(handle); }
    };

    using Hold = Platform::UniqueHold<Recorded>;

    /// **A move assignment closes what it held, then takes the other's**, where a swap left the
    /// source holding the handle the target gave up. A move hands over and closes nothing, a reset
    /// closes once, and what holds nothing closes nothing as it goes.
    TEST(PlatformUniqueHoldTest, aMoveAssignmentClosesWhatItHeldThenTakesTheOther)
    {
        sClosed.clear();
        {
            Hold one(1);
            Hold two(2);
            one = std::move(two);
            EXPECT_EQ(sClosed, std::vector<std::int32_t>{ 1 });
            EXPECT_EQ(one.get(), 2);
            // The test reads the moved-from object, whose state is what it checks.
            // NOLINTNEXTLINE(bugprone-use-after-move)
            EXPECT_FALSE(two.isOpen());

            Hold moved(std::move(one));
            EXPECT_EQ(sClosed, std::vector<std::int32_t>{ 1 }) << "a move closed something";
            EXPECT_EQ(moved.get(), 2);
            // The test reads the moved-from object, whose state is what it checks.
            // NOLINTNEXTLINE(bugprone-use-after-move)
            EXPECT_FALSE(one.isOpen());

            moved.reset();
            moved.reset();
            EXPECT_EQ(sClosed, (std::vector<std::int32_t>{ 1, 2 }));

            const Hold kept(3);
        }
        EXPECT_EQ(sClosed, (std::vector<std::int32_t>{ 1, 2, 3 })) << "only the one still held closed as it went";
    }
}
