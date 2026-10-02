#include <gtest/gtest.h>

#include <osg/Vec2f>

#include <apps/openmw/mwrender/searules.hpp>

namespace MWRender
{
    namespace
    {
        /// The sea stands at an exterior cell's middle, half of 8192 in from its corner: (4096, 4096)
        /// for the cell at the origin, and -2 * 8192 + 4096 = -12288 by 3 * 8192 + 4096 = 28672 for
        /// the cell at (-2, 3).
        TEST(RtxSeaRulesTest, theSeaStandsAtTheCellsMiddle)
        {
            EXPECT_EQ(seaCentre(0, 0), osg::Vec2f(4096.0f, 4096.0f));
            EXPECT_EQ(seaCentre(-2, 3), osg::Vec2f(-12288.0f, 28672.0f));
        }
    }
}
