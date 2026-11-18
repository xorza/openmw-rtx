#include <cstddef>

#include <gtest/gtest.h>

#include <components/rtx/renderer/arrivalwindow.hpp>

namespace Rtx
{
    namespace
    {
        /// **The largest of the last 64 extensions, each figure its own most, and it comes down**: an arrival of 900
        /// bytes, 600 of them structures, then one of 100 with 700 structures, leads both figures from its own; 62
        /// extensions of 10 later, 64 in all, both still stand; the 65th forgets the first, leaving the second's 700
        /// the structures' most and its 100 the most of all; the 66th forgets that too.
        TEST(RtxArrivalWindowTest, theLargestOfTheLastExtensionsComesDownOnceTheyPass)
        {
            ArrivalWindow window;
            EXPECT_EQ(window.largest().mAll, 0u) << "nothing arrived";
            window.note(ArrivalBytes{ .mAll = 900, .mStructures = 600 });
            window.note(ArrivalBytes{ .mAll = 100, .mStructures = 700 });
            EXPECT_EQ(window.largest().mAll, 900u);
            EXPECT_EQ(window.largest().mStructures, 700u) << "each figure its own most";

            for (std::size_t at = 2; at < ArrivalWindow::sCount; ++at)
                window.note(ArrivalBytes{ .mAll = 10, .mStructures = 10 });
            EXPECT_EQ(window.largest().mAll, 900u) << "64 noted, the first among them";

            window.note(ArrivalBytes{ .mAll = 10, .mStructures = 10 });
            EXPECT_EQ(window.largest().mAll, 100u) << "the first forgotten";
            EXPECT_EQ(window.largest().mStructures, 700u);

            window.note(ArrivalBytes{ .mAll = 10, .mStructures = 10 });
            EXPECT_EQ(window.largest().mAll, 10u);
            EXPECT_EQ(window.largest().mStructures, 10u);
        }
    }
}
