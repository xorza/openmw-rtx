#include <components/misc/display.hpp>

#include <gtest/gtest.h>

namespace
{
    // A display of 7680x2160 pixels at a density of 1.5 is 5120x1440 points. Its own resolution puts
    // max(7680 / 5120, 2160 / 1440) = 1.5 pixels on a point, and so does 3840x2160 by its height. 2560x1440 puts 1,
    // and 800x600 puts max(0.16, 0.42), which the floor of 1 raises. The setting multiplies the result, and a
    // display with no size leaves the setting unchanged.
    TEST(MiscInterfaceScaleTest, shouldBeSettingTimesPixelsPerDisplayPointAndNeverLessThanSetting)
    {
        const osg::Vec2i display(5120, 1440);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(1.f, { 7680, 2160 }, display), 1.5f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(1.f, { 3840, 2160 }, display), 1.5f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(1.f, { 2560, 1440 }, display), 1.f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(1.f, { 800, 600 }, display), 1.f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(2.f, { 800, 600 }, display), 2.f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(2.f, { 7680, 2160 }, display), 3.f);
        EXPECT_FLOAT_EQ(Misc::interfaceScale(1.25f, { 7680, 2160 }, { 0, 0 }), 1.25f);
    }
}
