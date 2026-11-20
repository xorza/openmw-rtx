#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/mirror/cells/predictedbands.hpp>

namespace Rtx
{
    namespace
    {
        /// **A slot holds a centre from its preload to its view's reset, and comes back for the next view**: two views,
        /// one preloaded; the second preloaded, the first reset and then let go; a third view takes the first's slot,
        /// with no centre until it is preloaded.
        TEST(RtxPredictedBandsTest, aSlotHoldsACentreFromItsPreloadToItsResetAndIsTakenAgain)
        {
            PredictedBands bands;
            const PredictedBands::Slot first = bands.open();
            const PredictedBands::Slot second = bands.open();
            EXPECT_NE(first, second);
            std::vector<osg::Vec3f> centres;
            bands.collect(centres);
            EXPECT_TRUE(centres.empty()) << "a view made predicts nothing";

            bands.place(first, osg::Vec3f(1.0f, 2.0f, 3.0f));
            bands.place(second, osg::Vec3f(4.0f, 5.0f, 6.0f));
            bands.collect(centres);
            EXPECT_EQ(centres, (std::vector<osg::Vec3f>{ osg::Vec3f(1.0f, 2.0f, 3.0f), osg::Vec3f(4.0f, 5.0f, 6.0f) }));

            bands.clear(first);
            centres.clear();
            bands.collect(centres);
            EXPECT_EQ(centres, std::vector<osg::Vec3f>{ osg::Vec3f(4.0f, 5.0f, 6.0f) }) << "a view reset";

            bands.close(first);
            EXPECT_EQ(bands.open(), first) << "the slot let go of, taken again";
            centres.clear();
            bands.collect(centres);
            EXPECT_EQ(centres, std::vector<osg::Vec3f>{ osg::Vec3f(4.0f, 5.0f, 6.0f) }) << "and empty until preloaded";
        }
    }
}
