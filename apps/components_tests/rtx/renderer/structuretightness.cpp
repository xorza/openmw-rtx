#include <string>

#include <gtest/gtest.h>

#include <components/rtx/renderer/structuretightness.hpp>

namespace Rtx
{
    namespace
    {
        /// **A structure falls in the octave of its triangles, the last holding every larger one**: 1 in the first,
        /// 2 and 3 in the second, 1000 in the tenth (512 to 1023), 2^19 and 2^30 both in the last; none in the first.
        TEST(RtxStructureTightnessTest, aStructureFallsInTheOctaveOfItsTriangles)
        {
            EXPECT_EQ(StructureTightness::octaveOf(0), 0u);
            EXPECT_EQ(StructureTightness::octaveOf(1), 0u);
            EXPECT_EQ(StructureTightness::octaveOf(2), 1u);
            EXPECT_EQ(StructureTightness::octaveOf(3), 1u);
            EXPECT_EQ(StructureTightness::octaveOf(1000), 9u);
            EXPECT_EQ(StructureTightness::octaveOf(1u << 19), 19u);
            EXPECT_EQ(StructureTightness::octaveOf(1u << 30), 19u);
        }

        /// **An octave sums its structures**, and the report says each that holds one: two meshes of 600 and 1000
        /// triangles, 100 000 and 60 000 bytes built, 50 000 and 30 000 tight, are 1 600 triangles at 100.0 bytes a
        /// triangle built and 50.0 tight, 50.0% kept; one of 2^20 at 100 MiB and 25 MiB is the last octave's.
        TEST(RtxStructureTightnessTest, anOctaveSumsItsStructuresAndTheReportSaysEach)
        {
            StructureTightness tightness;
            EXPECT_EQ(describe(tightness), "");
            tightness.add(600, 100000, 50000);
            tightness.add(1000, 60000, 30000);
            tightness.add(1u << 20, 1u << 26, 1u << 24);
            EXPECT_EQ(tightness.getOctaves()[9],
                (StructureTightness::Octave{
                    .mCount = 2, .mTriangles = 1600, .mBuiltBytes = 160000, .mTightBytes = 80000 }));
            EXPECT_EQ(describe(tightness),
                "       512-1023 triangles       2 structures    100.0 bytes a triangle built     50.0 tight   50.0% "
                "kept\n"
                "        524288+ triangles       1 structures     64.0 bytes a triangle built     16.0 tight   25.0% "
                "kept\n");
        }
    }
}
