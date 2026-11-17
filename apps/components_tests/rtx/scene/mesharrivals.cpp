#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <components/rtx/scene/mesharrivals.hpp>

namespace Rtx
{
    namespace
    {
        /// Fills an arrival of `vertices` vertices, every position at `x`, its indices counting up.
        void arrive(MeshArrivals& arrivals, const Index mesh, const std::uint32_t vertices, const float x,
            const std::uint32_t indices = 3)
        {
            const MeshArrivals::Filling into
                = arrivals.add(mesh, MeshArrivals::Lengths{ .mVertices = vertices, .mIndices = indices });
            for (osg::Vec3f& position : into.mPositions)
                position = osg::Vec3f(x, 0.0f, 0.0f);
            for (std::uint32_t at = 0; at < indices; ++at)
                into.mIndices[at] = at;
        }

        /// **Each slot answers its last arrival's runs**: two meshes laid end to end read back their own, a slot that
        /// arrives again stands for its first, and one let go of answers nothing.
        TEST(RtxMeshArrivalsTest, eachSlotAnswersItsLastArrival)
        {
            MeshArrivals arrivals;
            arrive(arrivals, 0, 4, 1.0f);
            arrive(arrivals, 3, 3, 2.0f, 0);
            ASSERT_TRUE(arrivals.holds(0));
            ASSERT_TRUE(arrivals.holds(3));
            EXPECT_FALSE(arrivals.holds(1)) << "a slot between two arrivals";
            EXPECT_FALSE(arrivals.holds(4)) << "a slot past every arrival";

            const MeshGeometry first = arrivals.of(0);
            EXPECT_EQ(first.mPositions.size(), 4u);
            EXPECT_EQ(first.mNormals.size(), 4u);
            EXPECT_EQ(first.mColours.size(), 4u);
            EXPECT_TRUE(first.mSecondTexCoords.empty());
            EXPECT_EQ(first.mPositions[3].x(), 1.0f);
            EXPECT_EQ(first.mIndices.size(), 3u);
            EXPECT_EQ(arrivals.of(3).mPositions[0].x(), 2.0f) << "the second arrival read the first's run";
            EXPECT_TRUE(arrivals.of(3).mIndices.empty()) << "an arrival that brought no indices";

            arrive(arrivals, 0, 5, 7.0f);
            EXPECT_EQ(arrivals.of(0).mPositions.size(), 5u);
            EXPECT_EQ(arrivals.of(0).mPositions[4].x(), 7.0f) << "a slot answered an arrival it stood again for";

            arrivals.forget(3);
            EXPECT_FALSE(arrivals.holds(3));
            arrivals.forget(3);
            arrivals.forget(9);
            EXPECT_TRUE(arrivals.holds(0)) << "forgetting one slot forgot another";

            // 4 + 5 + 3 vertices of 12 + 4 + 8 + 8 + 4 = 36 bytes, and 3 + 3 indices of 4: at least 456 bytes held.
            EXPECT_GE(arrivals.getBytes(), std::size_t{ 456 });
        }

        /// **A hand-over empties the arrivals and keeps their room**, so the next crossing of the same size allocates
        /// nothing; past `sKeptBytes` the room goes back, as a world's first hand-over gives back hundreds of
        /// mebibytes. 1,900,000 vertices are 36 bytes each, 68.4 MB, past the 67.1 MB kept.
        TEST(RtxMeshArrivalsTest, aClearKeepsTheRoomUpToItsBound)
        {
            MeshArrivals arrivals;
            arrive(arrivals, 0, 1000, 1.0f);
            arrive(arrivals, 1, 1000, 2.0f);
            const std::size_t held = arrivals.getBytes();

            arrivals.clear();
            EXPECT_FALSE(arrivals.holds(0));
            EXPECT_FALSE(arrivals.holds(1));
            EXPECT_EQ(arrivals.getBytes(), held) << "a clear under the bound gave the room back";

            EXPECT_EQ(Testing::allocationsIn([&] {
                arrive(arrivals, 1, 1000, 3.0f);
                arrive(arrivals, 0, 1000, 4.0f);
            }),
                0u)
                << "a second hand-over of the same size allocated";
            EXPECT_EQ(arrivals.of(0).mPositions[0].x(), 4.0f);

            arrivals.clear();
            arrive(arrivals, 2, 1'900'000, 5.0f, 0);
            ASSERT_GT(arrivals.getBytes(), MeshArrivals::sKeptBytes);
            arrivals.clear();
            EXPECT_EQ(arrivals.getBytes(), 0u) << "a clear past the bound kept the room";
        }
    }
}
