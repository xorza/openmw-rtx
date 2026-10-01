#include <components/misc/presentation.hpp>

#include <ostream>

#include <gtest/gtest.h>

#include <osg/io_utils>

namespace Misc
{
    namespace
    {
        struct Expected
        {
            const char* mCase;
            osg::Vec2i mAsked;
            osg::Vec2i mDrawable;
            osg::Vec2i mFrame;
            osg::Vec2i mShownOrigin;
            osg::Vec2i mShownSize;
        };

        /// Each by hand. A side fills the drawable when its ratio of drawable to frame is the
        /// smaller, and the other is `frame × drawable / frame` to the nearest pixel, centred.
        const Expected sCases[] = {
            // A side of nought is Native, the drawable itself.
            { "native", { 0, 0 }, { 1920, 1080 }, { 1920, 1080 }, { 0, 0 }, { 1920, 1080 } },
            { "native with one side asked", { 1280, 0 }, { 1920, 1080 }, { 1920, 1080 }, { 0, 0 }, { 1920, 1080 } },
            // The drawable's aspect: scaled up whole, no bar.
            { "same aspect", { 1280, 720 }, { 1920, 1080 }, { 1280, 720 }, { 0, 0 }, { 1920, 1080 } },
            // 1920 × 1080 = 2073600 ≤ 1080 × 2560 = 2764800: the width fills, 1080 × 1920 / 2560 = 810
            // high, and (1080 − 810) / 2 = 135 above and below.
            { "wider", { 2560, 1080 }, { 1920, 1080 }, { 2560, 1080 }, { 0, 135 }, { 1920, 810 } },
            // 1920 × 768 = 1474560 > 1080 × 1024 = 1105920: the height fills, 1024 × 1080 / 768 = 1440
            // wide, and (1920 − 1440) / 2 = 240 beside.
            { "taller", { 1024, 768 }, { 1920, 1080 }, { 1024, 768 }, { 240, 0 }, { 1440, 1080 } },
            // A frame larger than the window is scaled down: 1080 × 1920 / 3840 = 540.
            { "larger", { 3840, 1080 }, { 1920, 1080 }, { 3840, 1080 }, { 0, 270 }, { 1920, 540 } },
            // 2 × 10 / 3 = 6.67, so 7, and (10 − 7) / 2 = 1 in whole pixels.
            { "rounded", { 3, 2 }, { 10, 10 }, { 3, 2 }, { 0, 1 }, { 10, 7 } },
            // 1000 × 500 / 1000 = 500 wide in 1001: (1001 − 500) / 2 = 250, the odd pixel after.
            { "odd drawable", { 1000, 1000 }, { 1001, 500 }, { 1000, 1000 }, { 250, 0 }, { 500, 500 } },
            { "one pixel frame", { 1, 1 }, { 7, 5 }, { 1, 1 }, { 1, 0 }, { 5, 5 } },
            // 1080 × 1 / 1920 = 0.56, which rounds to 1 and never under it.
            { "one pixel drawable", { 1920, 1080 }, { 1, 1 }, { 1920, 1080 }, { 0, 0 }, { 1, 1 } },
            // A minimised window reports nought, which counts as one.
            { "minimised", { 0, 0 }, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 } },
        };

        TEST(MiscPresentationTest, aFrameIsShownAsLargeAsTheDrawableAllowsAtItsAspect)
        {
            for (const Expected& expected : sCases)
            {
                const Presentation presentation = present(expected.mAsked, expected.mDrawable);
                EXPECT_EQ(presentation.mFrame, expected.mFrame) << expected.mCase;
                EXPECT_EQ(presentation.mShownOrigin, expected.mShownOrigin) << expected.mCase;
                EXPECT_EQ(presentation.mShownSize, expected.mShownSize) << expected.mCase;
            }
        }

        /// The frame's corners are the shown rectangle's, a point on a bar is the frame's nearest
        /// edge, and a point of the frame comes back where it went. 2560 × 1080 in 1920 × 1080 is
        /// shown at (0, 135) by 1920 × 810, a scale of 0.75: the frame's (1280, 540) is at
        /// (960, 135 + 405 = 540).
        TEST(MiscPresentationTest, aPointMapsBetweenTheDrawableAndTheFrame)
        {
            const Presentation presentation = present({ 2560, 1080 }, { 1920, 1080 });

            EXPECT_EQ(presentation.toFrame({ 0, 135 }), osg::Vec2f(0, 0));
            EXPECT_EQ(presentation.toFrame({ 1920, 945 }), osg::Vec2f(2560, 1080));
            EXPECT_EQ(presentation.toFrame({ 960, 540 }), osg::Vec2f(1280, 540));
            EXPECT_EQ(presentation.toDrawable({ 1280, 540 }), osg::Vec2f(960, 540));

            EXPECT_EQ(presentation.toFrame({ 960, 10 }), osg::Vec2f(1280, 0)) << "a point on the top bar";
            EXPECT_EQ(presentation.toFrame({ 960, 1070 }), osg::Vec2f(1280, 1080)) << "a point on the bottom bar";

            for (const osg::Vec2f point : { osg::Vec2f(0, 0), osg::Vec2f(100, 250), osg::Vec2f(2559, 1079) })
                EXPECT_EQ(presentation.toFrame(presentation.toDrawable(point)), point);
        }

        /// A display of 7680 × 2160 pixels at a density of 1.5 is 5120 × 1440 points. Its own
        /// resolution puts max(7680 / 5120, 2160 / 1440) = 1.5 frame pixels on a point, and so does
        /// 3840 × 2160 by its height; 2560 × 1440 puts one, and 800 × 600 puts max(0.16, 0.42), which
        /// the floor of one raises. The setting multiplies whichever, and a display of no size
        /// leaves the setting alone.
        TEST(MiscPresentationTest, theInterfaceScaleIsTheFramePixelsOnAPointAndNeverUnderOne)
        {
            const osg::Vec2i display(5120, 1440);
            EXPECT_FLOAT_EQ(interfaceScale(1.f, { 7680, 2160 }, display), 1.5f);
            EXPECT_FLOAT_EQ(interfaceScale(1.f, { 3840, 2160 }, display), 1.5f);
            EXPECT_FLOAT_EQ(interfaceScale(1.f, { 2560, 1440 }, display), 1.f);
            EXPECT_FLOAT_EQ(interfaceScale(1.f, { 800, 600 }, display), 1.f);
            EXPECT_FLOAT_EQ(interfaceScale(1.f, { 1200, 900 }, display), 1.f) << "a small window at Native";
            EXPECT_FLOAT_EQ(interfaceScale(2.f, { 800, 600 }, display), 2.f);
            EXPECT_FLOAT_EQ(interfaceScale(2.f, { 7680, 2160 }, display), 3.f);
            EXPECT_FLOAT_EQ(interfaceScale(1.25f, { 7680, 2160 }, { 0, 0 }), 1.25f);
        }
    }
}
