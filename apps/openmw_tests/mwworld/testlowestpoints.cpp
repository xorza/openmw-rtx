#include <apps/openmw/mwworld/lowestpoints.hpp>

#include <array>
#include <limits>

#include <gtest/gtest.h>

namespace MWWorld
{
    namespace
    {
        constexpr float nothingHeld = std::numeric_limits<float>::max();

        // Elements of one array, since MSVC's identical data folding may give equal constants one address. Cast,
        // as the points tell objects apart by their address alone and never read one.
        const std::array<int, 3> objects{};
        const auto* const a = reinterpret_cast<const MWPhysics::Object*>(&objects[0]);
        const auto* const b = reinterpret_cast<const MWPhysics::Object*>(&objects[1]);
        const auto* const c = reinterpret_cast<const MWPhysics::Object*>(&objects[2]);

        TEST(LowestPointsTest, HoldsNothingAtFirstAndAfterClear)
        {
            LowestPoints points;
            EXPECT_EQ(points.lowest(), nothingHeld);

            points.set(a, -5.f);
            points.clear();
            EXPECT_EQ(points.lowest(), nothingHeld);

            points.set(a, 7.f);
            EXPECT_EQ(points.lowest(), 7.f);
        }

        TEST(LowestPointsTest, MovesAnObjectInBothDirections)
        {
            LowestPoints points;
            points.set(a, 10.f);
            points.set(b, 20.f);
            EXPECT_EQ(points.lowest(), 10.f);

            points.set(b, -30.f);
            EXPECT_EQ(points.lowest(), -30.f);

            // Raised past the other, it leaves the other's height: 10, and not its own 40.
            points.set(b, 40.f);
            EXPECT_EQ(points.lowest(), 10.f);
        }

        TEST(LowestPointsTest, RemovesOneObjectOfSeveralAtOneHeight)
        {
            LowestPoints points;
            points.set(a, 1.f);
            points.set(b, 1.f);
            points.set(c, 2.f);

            points.remove(a);
            EXPECT_EQ(points.lowest(), 1.f);
            points.remove(b);
            EXPECT_EQ(points.lowest(), 2.f);
            points.remove(c);
            EXPECT_EQ(points.lowest(), nothingHeld);
        }

        TEST(LowestPointsTest, RemovingAnObjectItDoesNotHoldChangesNothing)
        {
            LowestPoints points;
            points.remove(a);
            EXPECT_EQ(points.lowest(), nothingHeld);

            points.set(b, 3.f);
            points.remove(a);
            EXPECT_EQ(points.lowest(), 3.f);
        }

        TEST(LowestPointsTest, NotANumberLeavesAnObjectWithNoHeight)
        {
            LowestPoints points;
            points.set(a, 1.f);
            points.set(b, std::numeric_limits<float>::quiet_NaN());
            EXPECT_EQ(points.lowest(), 1.f);

            points.set(a, std::numeric_limits<float>::quiet_NaN());
            EXPECT_EQ(points.lowest(), nothingHeld);

            points.set(a, 4.f);
            EXPECT_EQ(points.lowest(), 4.f);
        }
    }
}
