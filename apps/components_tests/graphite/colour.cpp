#include <gtest/gtest.h>

#include <QRgb>

#include <components/graphite/colour.hpp>

namespace Graphite
{
    namespace
    {
        /// **WCAG's ratio at its two ends**: black on white is 21:1, (1 + 0.05) / (0 + 0.05), either
        /// way round, and a colour against itself is 1:1 whatever its alpha says.
        TEST(GraphiteColourTest, contrastIsWcagsRatioOfTheDecodedLuminances)
        {
            EXPECT_DOUBLE_EQ(luminance(qRgb(0, 0, 0)), 0.0);
            EXPECT_DOUBLE_EQ(luminance(qRgb(255, 255, 255)), 1.0);
            EXPECT_DOUBLE_EQ(contrast(qRgb(0, 0, 0), qRgb(255, 255, 255)), 21.0);
            EXPECT_DOUBLE_EQ(contrast(qRgb(255, 255, 255), qRgb(0, 0, 0)), 21.0);
            EXPECT_DOUBLE_EQ(contrast(qRgba(69, 69, 69, 0), qRgb(69, 69, 69)), 1.0);
        }

        /// **A translucent ink blends as `QPainter` blends it, in encoded sRGB**: red at 128 of 255
        /// over black is 255 · 128 / 255 = 128, and over white each of green and blue is
        /// 255 · 127 / 255 = 127. Opaque either way.
        TEST(GraphiteColourTest, aTranslucentInkBlendsInEncodedSrgb)
        {
            EXPECT_EQ(over(qRgba(255, 0, 0, 128), qRgb(0, 0, 0)), qRgb(128, 0, 0));
            EXPECT_EQ(over(qRgba(255, 0, 0, 128), qRgb(255, 255, 255)), qRgb(255, 127, 127));
            EXPECT_EQ(over(qRgb(10, 20, 30), qRgb(200, 200, 200)), qRgb(10, 20, 30));
        }

        /// **A mix runs in linear light**: halfway from black to white is linear 0.5, encoded
        /// 1.055 · 0.5^(1/2.4) − 0.055 = 0.73536, 187.52 of 255, which rounds to 188, where an encoded
        /// mix would give 128.
        TEST(GraphiteColourTest, aMixRunsInLinearLight)
        {
            EXPECT_EQ(mixLinear(qRgb(0, 0, 0), qRgb(255, 255, 255), 0.5), qRgb(188, 188, 188));
            EXPECT_EQ(mixLinear(qRgb(0, 0, 0), qRgb(255, 255, 255), 0.0), qRgb(0, 0, 0));
            EXPECT_EQ(mixLinear(qRgb(0, 0, 0), qRgb(255, 255, 255), 1.0), qRgb(255, 255, 255));
        }

        /// **An ink is lifted only as far as its floor asks**: Ayu Graphite's red reads 4.26:1 on a
        /// field's grey, so it stands under a floor of 4 and is lifted under one of 4.5, to the
        /// least colour at its hue that clears, which the next step down the grid misses.
        TEST(GraphiteColourTest, anInkIsLiftedAtItsHueOnlyAsFarAsItsFloorAsks)
        {
            constexpr QRgb red = qRgb(0xff, 0x8b, 0x8c);
            constexpr QRgb field = qRgb(0x45, 0x45, 0x45);
            EXPECT_EQ(liftedToClear(red, field, 4.0), red);

            const QRgb lifted = liftedToClear(red, field, 4.5);
            EXPECT_EQ(lifted, qRgb(0xff, 0x94, 0x94));
            EXPECT_GE(contrast(lifted, field), 4.5);
            EXPECT_LT(contrast(qRgb(0xff, 0x93, 0x93), field), 4.5) << "a lift past the least that clears";
        }
    }
}
