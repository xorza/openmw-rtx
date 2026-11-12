#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/rtx/image/colour.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>
#include <components/sceneutil/util.hpp>

namespace Rtx
{
    namespace
    {
        /// The byte overload is the float curve at that byte, and not an approximation of it: every one of the
        /// 256, bit for bit. A decoder only ever gives the curve `k / 255`, which is what makes a table over `k`
        /// the same answer rather than a faster one, so a `NEAR` here would let the pictures differ by a rounding.
        TEST(RtxSrgbTest, theByteCurveIsTheFloatCurveAtEveryStoredValue)
        {
            for (std::uint32_t stored = 0; stored < 256; ++stored)
            {
                const auto byte = static_cast<std::uint8_t>(stored);
                EXPECT_EQ(toLinear(byte), toLinear(static_cast<float>(stored) / 255.0f)) << "at " << stored;
            }
        }

        /// A value that is not one of the 256 is not answered as though it were. The float overload finds the
        /// byte a value would round to and divides it back before it believes the table, so a value one place off
        /// a stored byte, or halfway between two, must come out of the curve: snapping would move the picture.
        TEST(RtxSrgbTest, aValueThatIsNotAStoredByteTakesTheCurve)
        {
            // One representable step above 127 / 255, which is as close to a stored byte as a value
            // can be without being one.
            const float justOver = std::nextafter(127.0f / 255.0f, 1.0f);
            EXPECT_NE(toLinear(justOver), toLinear(std::uint8_t{ 127 })) << "a value one place off was snapped";
            EXPECT_GT(toLinear(justOver), toLinear(std::uint8_t{ 127 })) << "the curve does not rise";

            // And halfway between two bytes, which lands strictly between their two answers.
            EXPECT_GT(toLinear(0.5f), toLinear(std::uint8_t{ 127 }));
            EXPECT_LT(toLinear(0.5f), toLinear(std::uint8_t{ 128 }));

            // A negative light's colour is `-k / 255`, which is none of the 256 and takes the
            // linear leg — the sign is what a refusal reads, and the curve is odd about nothing.
            EXPECT_EQ(toLinear(-10.0f / 255.0f), -10.0f / 255.0f / 12.92f);
        }

        /// The curve itself, anchored where it can be stated outright.
        ///
        /// **What the test above cannot say.** That one compares two spellings of the curve, so it
        /// passes whether or not either is right. These are the three points sRGB names.
        TEST(RtxSrgbTest, nothingStaysNothingAndFullStaysFull)
        {
            EXPECT_EQ(toLinear(std::uint8_t{ 0 }), 0.0f);
            EXPECT_EQ(toLinear(std::uint8_t{ 255 }), 1.0f);

            // The knee, which is the one place the curve is two rules: 10 / 255 = 0.0392 is under
            // `0.04045` and takes the linear leg, and 11 / 255 = 0.0431 is over it and takes the
            // power leg, which lands above what the linear one would have given.
            EXPECT_EQ(toLinear(std::uint8_t{ 10 }), 10.0f / 255.0f / 12.92f);
            EXPECT_GT(toLinear(std::uint8_t{ 11 }), 11.0f / 255.0f / 12.92f);
        }

        /// The byte `std::lround` held to the bytes gives, as a chain wrote it before: the same over every
        /// 257th float from −3 to 3 of either sign; a half rounded away from nought, `0.5 * 255 = 127.5`
        /// to 128; the ends; and the values no texel holds, a NaN nought and the infinities held.
        TEST(RtxSrgbTest, aUnitIsTheByteLroundGives)
        {
            const auto lrounded
                = [](float value) { return static_cast<std::byte>(std::clamp(std::lround(value * 255.0f), 0L, 255L)); };
            const std::uint32_t most = std::bit_cast<std::uint32_t>(3.0f);
            std::uint32_t differ = 0;
            for (std::uint32_t bits = 0; bits <= most; bits += 257)
                for (const std::uint32_t sign : { 0u, 0x80000000u })
                {
                    const float value = std::bit_cast<float>(bits | sign);
                    differ += unitByte(value) != lrounded(value) ? 1 : 0;
                }
            EXPECT_EQ(differ, 0u);

            EXPECT_EQ(unitByte(0.5f), std::byte{ 128 });
            EXPECT_EQ(unitByte(0.0f), std::byte{ 0 });
            EXPECT_EQ(unitByte(1.0f), std::byte{ 255 });
            EXPECT_EQ(unitByte(std::numeric_limits<float>::quiet_NaN()), std::byte{ 0 });
            EXPECT_EQ(unitByte(std::numeric_limits<float>::infinity()), std::byte{ 255 });
            EXPECT_EQ(unitByte(-std::numeric_limits<float>::infinity()), std::byte{ 0 });
        }

        /// **The encoded byte off its table is the curve's**, `unitByte(toEncoded(x))`: on both sides of
        /// every byte's first float, each found here by bisection on the bits; over every 1021st float
        /// from nought to one and a half; below nought and past one, held; and a NaN nought.
        TEST(RtxSrgbTest, anEncodedByteIsTheCurvesByte)
        {
            const auto curve = [](float linear) { return unitByte(toEncoded(linear)); };
            for (std::uint32_t byte = 1; byte < 256; ++byte)
            {
                std::uint32_t low = 0;
                std::uint32_t high = std::bit_cast<std::uint32_t>(1.0f);
                while (low < high)
                {
                    const std::uint32_t middle = low + (high - low) / 2;
                    if (std::to_integer<std::uint32_t>(curve(std::bit_cast<float>(middle))) >= byte)
                        high = middle;
                    else
                        low = middle + 1;
                }
                const float first = std::bit_cast<float>(low);
                const float before = std::bit_cast<float>(low - 1);
                EXPECT_EQ(encodedByte(first), static_cast<std::byte>(byte)) << byte;
                EXPECT_EQ(encodedByte(before), curve(before)) << byte;
            }

            std::uint32_t differ = 0;
            for (std::uint32_t bits = 0; bits <= std::bit_cast<std::uint32_t>(1.5f); bits += 1021)
            {
                const float linear = std::bit_cast<float>(bits);
                differ += encodedByte(linear) != curve(linear) ? 1 : 0;
            }
            EXPECT_EQ(differ, 0u);

            EXPECT_EQ(encodedByte(-0.25f), std::byte{ 0 });
            EXPECT_EQ(encodedByte(2.0f), std::byte{ 255 });
            EXPECT_EQ(encodedByte(std::numeric_limits<float>::quiet_NaN()), std::byte{ 0 });
        }
    }

    namespace
    {
        /// The packing is `0xAABBGGRR`: red in the low byte.
        ///
        /// Reading it the other way round turns every candle in the game blue, which is the kind of
        /// wrong that looks deliberate.
        TEST(RtxDecodeColourTest, aColourIsRedFirstAndDecodedOutOfDisplaySpace)
        {
            EXPECT_EQ(decodeColour(0x00FFFFFF), osg::Vec3f(1.0f, 1.0f, 1.0f));
            EXPECT_EQ(decodeColour(0), osg::Vec3f(0.0f, 0.0f, 0.0f));

            const osg::Vec3f candle = decodeColour(0x000080FF);
            EXPECT_FLOAT_EQ(candle.x(), 1.0f) << "red is the low byte";
            EXPECT_EQ(candle.z(), 0.0f) << "and blue the third";

            // Mid grey is where the two spaces diverge most, so it is where skipping the decode is
            // most visible: 128 of 255 is 0.50196 encoded and
            // ((0.50196 + 0.055) / 1.055)^2.4 = 0.21586 linear.
            EXPECT_NEAR(candle.y(), 0.21586f, 1e-5f);
            EXPECT_NEAR(decodeColour(0x00808080).x(), 0.21586f, 1e-5f);
        }

        /// The colour the game hands over is the same colour, and takes the same decode.
        ///
        /// **OpenMW's own comment calls its pipeline linear and it is not**: `SceneUtil::colourFromRGB` divides a
        /// record's bytes by 255 and stops, so what settles on a light, a fog or the sky is display-encoded as
        /// the record was. The game path decodes, and must land on the same value as a screenshot's.
        TEST(RtxDecodeColourTest, aColourTheGameHasAlreadyUnpackedDecodesToTheSameLight)
        {
            for (const std::uint32_t packed : { 0x00000000u, 0x00808080u, 0x000080FFu, 0x00FFFFFFu })
                EXPECT_EQ(decodeColour(packed), decodeColour(SceneUtil::colourFromRGB(packed))) << "packed " << packed;

            // The alpha is dropped rather than carried: nothing downstream of a light has a use for
            // one, and a fog colour arrives with its own.
            EXPECT_EQ(decodeColour(osg::Vec4f(1.0f, 1.0f, 1.0f, 0.25f)), osg::Vec3f(1.0f, 1.0f, 1.0f));

            // The same mid grey, reached the other way: 128 of 255 encoded is 0.21586 linear.
            EXPECT_NEAR(decodeColour(osg::Vec4f(128.0f / 255.0f, 0.0f, 0.0f, 1.0f)).x(), 0.21586f, 1e-5f);
        }

        /// The saturation grade: one is the channel to the bit, nought the luminance, and between
        /// and past them a straight line through both that keeps the luminance.
        ///
        /// A red of `(0.8, 0.2, 0.1)` weighs `0.2126 * 0.8 + 0.7152 * 0.2 + 0.0722 * 0.1 = 0.32034`.
        /// At a half each channel is half way to that: `(0.56017, 0.26017, 0.21017)`. At two it is as
        /// far again on the other side: `(1.27966, 0.07966, -0.12034)`, and the blue, carried below
        /// nothing, is held at nought.
        TEST(RtxSaturationTest, oneKeepsTheChannelNoughtIsTheLuminanceAndTheLuminanceHolds)
        {
            const osg::Vec3f red(0.8f, 0.2f, 0.1f);
            const float luminance = red * Shaders::LUMINANCE_WEIGHTS;
            EXPECT_NEAR(luminance, 0.32034f, 1e-6f);

            const auto graded = [&](float saturation) {
                return osg::Vec3f(Shaders::saturatedChannel(red.x(), luminance, saturation),
                    Shaders::saturatedChannel(red.y(), luminance, saturation),
                    Shaders::saturatedChannel(red.z(), luminance, saturation));
            };

            // **Equal and not near**, for values that round: what the dial as shipped leaves of a
            // picture is every pixel as it was.
            for (const float channel : { 0.0f, 0.1f, 0.3333333f, 1.0f / 3.0f, 7.77f, 1.0e-30f })
                for (const float weighed : { 0.0f, 0.2f, 0.6180339f, 12.5f })
                {
                    EXPECT_EQ(Shaders::saturatedChannel(channel, weighed, 1.0f), channel)
                        << channel << " at " << weighed;
                    EXPECT_EQ(Shaders::saturatedChannel(channel, weighed, 0.0f), weighed)
                        << channel << " at " << weighed;
                }

            const osg::Vec3f half = graded(0.5f);
            EXPECT_NEAR(half.x(), 0.56017f, 1e-5f);
            EXPECT_NEAR(half.y(), 0.26017f, 1e-5f);
            EXPECT_NEAR(half.z(), 0.21017f, 1e-5f);

            const osg::Vec3f twice = graded(2.0f);
            EXPECT_NEAR(twice.x(), 1.27966f, 1e-5f);
            EXPECT_NEAR(twice.y(), 0.07966f, 1e-5f);
            EXPECT_EQ(twice.z(), 0.0f) << "a channel carried below nothing is held at nought";

            for (const float saturation : { 0.0f, 0.5f, 1.0f })
                EXPECT_NEAR(graded(saturation) * Shaders::LUMINANCE_WEIGHTS, luminance, 1e-6f)
                    << "the grade moved the luminance at " << saturation;

            EXPECT_NE(half, graded(0.25f)) << "the saturation made no difference";
        }

        /// The contrast grade: a luminance `n` stops from the key lands `n * contrast` stops from it,
        /// the key itself does not move, and a contrast of one multiplies by exactly one.
        ///
        /// Two stops over the key is `4 * 0.18 = 0.72`. At a half it lands one stop over, 0.36, so
        /// the colour is multiplied by a half; two stops under, 0.045, lands one stop under, 0.09,
        /// a multiple of two. At one and a half the two stops over become three, 1.44: twice.
        TEST(RtxContrastTest, aStopFromTheKeyBecomesContrastStopsAndTheKeyHolds)
        {
            const float key = Shaders::EXPOSURE_KEY;

            for (const float luminance : { 1.0e-6f, 0.045f, 0.18f, 0.2391f, 0.72f, 55.5f })
                EXPECT_EQ(Shaders::contrastScale(luminance, 1.0f), 1.0f) << "at " << luminance;

            for (const float contrast : { 0.5f, 0.9f, 1.5f })
                EXPECT_FLOAT_EQ(Shaders::contrastScale(key, contrast), 1.0f) << "the key moved at " << contrast;

            EXPECT_NEAR(Shaders::contrastScale(4.0f * key, 0.5f), 0.5f, 1e-6f);
            EXPECT_NEAR(Shaders::contrastScale(0.25f * key, 0.5f), 2.0f, 1e-5f);
            EXPECT_NEAR(Shaders::contrastScale(4.0f * key, 1.5f), 2.0f, 1e-5f);

            EXPECT_EQ(Shaders::contrastScale(0.0f, 0.5f), 1.0f) << "black has no ratio to take";
            EXPECT_EQ(Shaders::contrastScale(-0.01f, 0.5f), 1.0f) << "nor has less than black";
            EXPECT_EQ(Shaders::contrastScale(1.0e-39f, 0.5f), 1.0f) << "nor has a subnormal a device may flush";
        }

        /// The display gamma: a gamma of one gives every stored value back to the bit, black and
        /// white stay where they are, and a value between moves to its power of one over the gamma.
        ///
        /// At a gamma of two a quarter is lifted to its square root, a half; at a half a half is
        /// lowered to its square, a quarter.
        TEST(RtxDisplayGammaTest, oneIsTheValueTheEndsHoldAndBetweenIsThePower)
        {
            for (std::uint32_t stored = 0; stored < 256; ++stored)
            {
                const float encoded = static_cast<float>(stored) / 255.0f;
                EXPECT_EQ(Shaders::displayGamma(encoded, 1.0f), encoded) << "at " << stored;
            }

            for (const float gamma : { 0.1f, 0.5f, 2.0f, 3.0f })
            {
                EXPECT_EQ(Shaders::displayGamma(0.0f, 1.0f / gamma), 0.0f) << "black moved at " << gamma;
                EXPECT_EQ(Shaders::displayGamma(1.0f, 1.0f / gamma), 1.0f) << "white moved at " << gamma;
            }

            EXPECT_FLOAT_EQ(Shaders::displayGamma(0.25f, 1.0f / 2.0f), 0.5f);
            EXPECT_FLOAT_EQ(Shaders::displayGamma(0.5f, 1.0f / 0.5f), 0.25f);
            EXPECT_GT(Shaders::displayGamma(0.25f, 1.0f / 2.0f), Shaders::displayGamma(0.25f, 1.0f / 1.5f))
                << "a greater gamma did not lift the value further";
        }

        /// **The curve's grey, by hand in each of its stretches**, the offset 0.04 and the
        /// compression starting at 0.76 (`look.h`). Below the ramp's end, 3 × 0.04 = 0.12, the offset
        /// comes off in proportion: 0.06 loses 0.04 × 0.06 / 0.12 = 0.02, to 0.04. At and past it the
        /// whole offset comes off: 0.12 goes to 0.08, 0.5 to 0.46, and 0.8 to 0.76, the compression's
        /// start, where the hyperbola meets the line. Past it, 1.0 is 0.96 lifted, brought to
        /// 1 − 0.24² / (0.96 + 0.24 − 0.76) = 1 − 0.0576 / 0.44. Black stays black. Within four ulps,
        /// since 0.04 and 0.76 are not floats.
        TEST(RtxToneMapTest, aGreyLosesTheRampedOffsetAndIsBroughtUnderOnePastTheCompression)
        {
            EXPECT_EQ(Shaders::toneMapGrey(0.0f), 0.0f);
            EXPECT_FLOAT_EQ(Shaders::toneMapGrey(0.06f), 0.04f);
            EXPECT_FLOAT_EQ(Shaders::toneMapGrey(0.12f), 0.08f);
            EXPECT_FLOAT_EQ(Shaders::toneMapGrey(0.5f), 0.46f);
            EXPECT_FLOAT_EQ(Shaders::toneMapGrey(0.8f), 0.76f);
            EXPECT_FLOAT_EQ(Shaders::toneMapGrey(1.0f), static_cast<float>(1.0 - 0.0576 / 0.44));
            EXPECT_LT(Shaders::toneMapGrey(1000.0f), 1.0f) << "the compression reached the display's end";
        }

        /// **YCoCg, by hand, and back.** Red `(1, 0, 0)` is `(1/4, 1/2, -1/4)`, green `(0, 1, 0)` is
        /// `(1/2, 0, 1/2)` and blue `(0, 0, 1)` is `(1/4, -1/2, -1/4)`: luma `(R + 2G + B) / 4` and the
        /// two differences. A grey has no chroma, and its luma is itself. Every coefficient is a power of
        /// two, so a colour of quarters goes there and back to the bit.
        TEST(RtxYcocgTest, theAxesAreLumaAndTwoDifferencesAndTheWayBackIsExact)
        {
            EXPECT_EQ(Shaders::ycocgOf(osg::Vec3f(1.0f, 0.0f, 0.0f)), osg::Vec3f(0.25f, 0.5f, -0.25f));
            EXPECT_EQ(Shaders::ycocgOf(osg::Vec3f(0.0f, 1.0f, 0.0f)), osg::Vec3f(0.5f, 0.0f, 0.5f));
            EXPECT_EQ(Shaders::ycocgOf(osg::Vec3f(0.0f, 0.0f, 1.0f)), osg::Vec3f(0.25f, -0.5f, -0.25f));
            EXPECT_EQ(Shaders::ycocgOf(osg::Vec3f(0.75f, 0.75f, 0.75f)), osg::Vec3f(0.75f, 0.0f, 0.0f));

            const osg::Vec3f quarters(0.25f, 1.5f, 0.75f);
            EXPECT_EQ(Shaders::rgbOfYcocg(Shaders::ycocgOf(quarters)), quarters);
        }
    }
}
