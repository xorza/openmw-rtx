#include <components/misc/presentation.hpp>

#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/io_utils>

#include <components/misc/display.hpp>

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
            float mShownScale;
        };

        /// Each by hand. A side fills the drawable when its ratio of drawable to frame is the
        /// smaller, and the other is `frame × drawable / frame` to the nearest pixel, centred. The
        /// shown scale is that smaller ratio: `taller` is min(1920 / 1024, 1080 / 768) = 1.40625, and
        /// `odd drawable` min(1001 / 1000, 500 / 1000) = 0.5, not the rounded side's.
        const Expected sCases[] = {
            // A side of nought is Native, the drawable itself.
            { "native", { 0, 0 }, { 1920, 1080 }, { 1920, 1080 }, { 0, 0 }, { 1920, 1080 }, 1.f },
            { "native with one side asked", { 1280, 0 }, { 1920, 1080 }, { 1920, 1080 }, { 0, 0 }, { 1920, 1080 },
                1.f },
            // The drawable's aspect: scaled up whole, no bar.
            { "same aspect", { 1280, 720 }, { 1920, 1080 }, { 1280, 720 }, { 0, 0 }, { 1920, 1080 }, 1.5f },
            // 1920 × 1080 = 2073600 ≤ 1080 × 2560 = 2764800: the width fills, 1080 × 1920 / 2560 = 810
            // high, and (1080 − 810) / 2 = 135 above and below.
            { "wider", { 2560, 1080 }, { 1920, 1080 }, { 2560, 1080 }, { 0, 135 }, { 1920, 810 }, 0.75f },
            // 1920 × 768 = 1474560 > 1080 × 1024 = 1105920: the height fills, 1024 × 1080 / 768 = 1440
            // wide, and (1920 − 1440) / 2 = 240 beside.
            { "taller", { 1024, 768 }, { 1920, 1080 }, { 1024, 768 }, { 240, 0 }, { 1440, 1080 }, 1.40625f },
            // A frame larger than the window is scaled down: 1080 × 1920 / 3840 = 540.
            { "larger", { 3840, 1080 }, { 1920, 1080 }, { 3840, 1080 }, { 0, 270 }, { 1920, 540 }, 0.5f },
            // 2 × 10 / 3 = 6.67, so 7, and (10 − 7) / 2 = 1 in whole pixels.
            { "rounded", { 3, 2 }, { 10, 10 }, { 3, 2 }, { 0, 1 }, { 10, 7 }, 10.f / 3.f },
            // 1000 × 500 / 1000 = 500 wide in 1001: (1001 − 500) / 2 = 250, the odd pixel after.
            { "odd drawable", { 1000, 1000 }, { 1001, 500 }, { 1000, 1000 }, { 250, 0 }, { 500, 500 }, 0.5f },
            { "one pixel frame", { 1, 1 }, { 7, 5 }, { 1, 1 }, { 1, 0 }, { 5, 5 }, 5.f },
            // 1080 × 1 / 1920 = 0.56, which rounds to 1 and never under it.
            { "one pixel drawable", { 1920, 1080 }, { 1, 1 }, { 1920, 1080 }, { 0, 0 }, { 1, 1 }, 1.f / 1920.f },
            // A minimised window reports nought, which counts as one.
            { "minimised", { 0, 0 }, { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, 1.f },
        };

        TEST(MiscPresentationTest, aFrameIsShownAsLargeAsTheDrawableAllowsAtItsAspect)
        {
            for (const Expected& expected : sCases)
            {
                const Presentation presentation = present(expected.mAsked, expected.mDrawable);
                EXPECT_EQ(presentation.mFrame, expected.mFrame) << expected.mCase;
                EXPECT_EQ(presentation.mShownOrigin, expected.mShownOrigin) << expected.mCase;
                EXPECT_EQ(presentation.mShownSize, expected.mShownSize) << expected.mCase;
                EXPECT_FLOAT_EQ(presentation.shownScale(), expected.mShownScale) << expected.mCase;
            }
        }

        /// **A frame outside a renderer's bounds is drawn at the nearest it can be, each side on its
        /// own**, Native too, and `askedFrame` still names what was asked. Bounds of 3 to 16384:
        /// 2 × 2 is drawn at 3 × 3, 38400 × 1080 at 16384 × 1080, and a minimised window's Native
        /// frame of one pixel at 3 × 3.
        TEST(MiscPresentationTest, aFrameIsHeldInsideTheRenderersBounds)
        {
            const FrameBounds bounds{ .mSmallest = { 3, 3 }, .mLargest = { 16384, 16384 } };

            EXPECT_EQ(present({ 2, 2 }, { 1920, 1080 }, bounds).mFrame, osg::Vec2i(3, 3));
            EXPECT_EQ(present({ 38400, 1080 }, { 1920, 1080 }, bounds).mFrame, osg::Vec2i(16384, 1080));
            EXPECT_EQ(present({ 0, 0 }, { 0, 0 }, bounds).mFrame, osg::Vec2i(3, 3));
            EXPECT_EQ(present({ 0, 0 }, { 20000, 1080 }, bounds).mFrame, osg::Vec2i(16384, 1080));
            EXPECT_EQ(present({ 1280, 720 }, { 1920, 1080 }, bounds).mFrame, osg::Vec2i(1280, 720))
                << "a frame inside the bounds moved";

            EXPECT_EQ(askedFrame({ 38400, 1080 }, { 1920, 1080 }), osg::Vec2i(38400, 1080));
            EXPECT_EQ(askedFrame({ 0, 0 }, { 0, 0 }), osg::Vec2i(1, 1));
            EXPECT_EQ(present({ 2, 2 }, { 1920, 1080 }).mFrame, osg::Vec2i(2, 2)) << "the default bounds held a frame";
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

        /// **The interface keeps one size on the display**: a unit covers `setting × displayScale`
        /// window pixels at every frame and window, so in the frame it takes that over the window
        /// pixels a frame pixel covers. A 7680 × 2160 window at a display scale of 1.5: its own
        /// frame shows a frame pixel a window pixel, so 1.5; 5120 × 1440 shows one on 7680 / 5120 =
        /// 1.5, so 1; 3840 × 1080 on 2, so 0.75; 2560 × 1440, narrower, fills the height,
        /// 2160 / 1440 = 1.5, so 1. A 1200 × 900 window at Native is its frame, so 1.5. A setting of
        /// two doubles each, and the same 3840 × 1080 frame in a window of its own size takes 1.5,
        /// where the larger window gave 0.75.
        TEST(MiscPresentationTest, theInterfaceKeepsOneSizeOnTheDisplay)
        {
            const osg::Vec2i window(7680, 2160);
            constexpr float display = 1.5f;
            const struct
            {
                osg::Vec2i mAsked;
                osg::Vec2i mWindow;
                float mSetting;
                float mScale;
            } cases[] = {
                { { 7680, 2160 }, window, 1.f, 1.5f },
                { { 5120, 1440 }, window, 1.f, 1.f },
                { { 3840, 1080 }, window, 1.f, 0.75f },
                { { 2560, 1440 }, window, 1.f, 1.f },
                { { 0, 0 }, { 1200, 900 }, 1.f, 1.5f },
                { { 7680, 2160 }, window, 2.f, 3.f },
                { { 3840, 1080 }, window, 2.f, 1.5f },
                { { 3840, 1080 }, { 3840, 1080 }, 1.f, 1.5f },
            };
            for (const auto& c : cases)
            {
                const Presentation presentation = present(c.mAsked, c.mWindow);
                const float scale = presentation.interfaceScale(c.mSetting, display);
                EXPECT_FLOAT_EQ(scale, c.mScale) << c.mAsked.x() << " x " << c.mAsked.y() << " in " << c.mWindow.x();
                EXPECT_FLOAT_EQ(scale * presentation.shownScale(), c.mSetting * display)
                    << "a unit's window pixels moved with the frame";
            }
            EXPECT_FLOAT_EQ(present(window, window).interfaceScale(1.f, 1.f), 1.f) << "the display scale had no say";
        }

        /// **A save's thumbnail is the middle of the frame at the thumbnail's aspect**, 518 by 266.
        /// A 1920 by 1080 frame is taller: `1080 - 1920 * 266 / 518` = 94.05 rows over, 94 whole,
        /// 47 off the top and the bottom. A 7680 by 2160 frame is wider:
        /// `7680 - 2160 * 518 / 266` = 3473.68 columns over, 3473 whole, 1736 off each side. A frame
        /// at the thumbnail's own aspect is not cut at all.
        TEST(MiscPresentationTest, aThumbnailIsTheMiddleOfTheFrameAtItsOwnAspect)
        {
            const osg::Vec2i thumbnail(518, 266);
            EXPECT_EQ(cropToAspect({ 1920, 1080 }, thumbnail),
                (Crop{ .mOrigin = osg::Vec2i(0, 47), .mSize = osg::Vec2i(1920, 986) }));
            EXPECT_EQ(cropToAspect({ 7680, 2160 }, thumbnail),
                (Crop{ .mOrigin = osg::Vec2i(1736, 0), .mSize = osg::Vec2i(4208, 2160) }));
            EXPECT_EQ(cropToAspect({ 1036, 532 }, thumbnail),
                (Crop{ .mOrigin = osg::Vec2i(0, 0), .mSize = osg::Vec2i(1036, 532) }));
        }

        /// **A Native file saves Native**, whatever the custom sides hold: loaded, the setting of
        /// nought by nought shows as Native, and saved from Native it is nought by nought again,
        /// where the launcher wrote the custom sides' 800 by 600. A listed mode saves the sides its
        /// text opens with, as `getResolutionText` writes them, and loads as listed; typed sides
        /// save as typed and load as typed where no mode lists them. A listed text that opens with
        /// no sides saves Native rather than a size nobody picked.
        TEST(MiscPresentationTest, aResolutionMenuSavesWhatItShowsAndShowsWhatItSaved)
        {
            const osg::Vec2i typed(800, 600);

            const osg::Vec2i native(0, 0);
            ASSERT_EQ(resolutionPickOf(native, false), ResolutionPick::Native);
            const osg::Vec2i saved = resolutionPicked(ResolutionPick::Native, "Native", typed);
            EXPECT_EQ(saved, native) << "Native saved the custom sides";
            EXPECT_EQ(resolutionPickOf(saved, false), ResolutionPick::Native);

            const std::string mode = getResolutionText(2560, 1440);
            EXPECT_EQ(mode, "2560 × 1440 (16:9)");
            const osg::Vec2i listed = resolutionPicked(ResolutionPick::Listed, mode, typed);
            EXPECT_EQ(listed, osg::Vec2i(2560, 1440));
            EXPECT_EQ(resolutionPickOf(listed, true), ResolutionPick::Listed);

            EXPECT_EQ(resolutionPicked(ResolutionPick::Custom, "Native", typed), typed);
            EXPECT_EQ(resolutionPickOf(typed, false), ResolutionPick::Custom);

            EXPECT_EQ(resolutionPicked(ResolutionPick::Listed, "Native", typed), native);
            EXPECT_EQ(resolutionPicked(ResolutionPick::Listed, "1920 x 1080", typed), native)
                << "a letter x is not the sign the mode list writes";
        }
    }
}
