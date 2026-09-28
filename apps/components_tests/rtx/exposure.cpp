#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/shaders/exposure.h>

namespace Rtx
{
    namespace
    {
        /// The two halves of the bin mapping are inverses of each other, and a luminance lands in
        /// the bin that stands for it.
        ///
        /// **Hand-placed at the scale's ends and at one.** The scale runs from `2^-10` at bin one to
        /// `2^(6 + log2 DAYLIGHT_GAIN)` at bin `EXPOSURE_BINS - 1`, so a luminance of one is
        /// `10 / (16 + log2 DAYLIGHT_GAIN)` of the way along it. At a gain of ten that is
        /// `10 / 19.3219 * 254 = 131.46`, and the bin under it plus the black bin is 132.
        TEST(RtxExposureBinTest, aLuminanceLandsInTheBinThatStandsForIt)
        {
            EXPECT_EQ(Shaders::luminanceBin(std::exp2(Shaders::MIN_LOG_LUMINANCE)), 1u) << "the bottom of the scale";
            EXPECT_EQ(Shaders::luminanceBin(std::exp2(Shaders::MAX_LOG_LUMINANCE)), Shaders::EXPOSURE_BINS - 1u)
                << "the top of it";
            const float span = 16.0f + std::log2(Shaders::DAYLIGHT_GAIN);
            EXPECT_EQ(Shaders::luminanceBin(1.0f), static_cast<std::uint32_t>(10.0f / span * 254.0f) + 1u) << "and one";

            // **A whole bin stands for its middle and not its edge**, so a frame binned into one
            // bin is metered at what that bin holds on average, and the round trip lands back in it
            // by a margin of half a bin either side.
            for (std::uint32_t bin = 1; bin < Shaders::EXPOSURE_BINS; ++bin)
            {
                const float middle = Shaders::binLuminance(static_cast<float>(bin));
                EXPECT_EQ(Shaders::luminanceBin(middle), bin) << "a bin's middle lands in it, at " << bin;
            }

            // By hand: bin one is the scale's first 254th, so its middle is half of that above
            // `2^-10`: `2^(-10 + 0.5 / 254 × span)`. Bin 254 is the scale's last 254th, and its
            // middle is half a 254th below the top, `253.5 / 254` of the way along; bin 255 holds only
            // what is held at the top.
            EXPECT_FLOAT_EQ(Shaders::binLuminance(1.0f), std::exp2(-10.0f + 0.5f / 254.0f * span));
            EXPECT_FLOAT_EQ(Shaders::binLuminance(254.0f), std::exp2(253.5f / 254.0f * span - 10.0f));
        }

        /// Bin nought is black and off the scale: below `EXPOSURE_BLACK` nothing is binned, and
        /// a luminance past either end is held to the end rather than to a bin that does not exist.
        TEST(RtxExposureBinTest, blackIsTheBinBelowTheScaleAndTheEndsHold)
        {
            EXPECT_EQ(Shaders::luminanceBin(0.0f), 0u);
            EXPECT_EQ(Shaders::luminanceBin(std::nextafter(Shaders::EXPOSURE_BLACK, 0.0f)), 0u);

            EXPECT_EQ(Shaders::luminanceBin(std::exp2(Shaders::MIN_LOG_LUMINANCE) * 0.5f), 1u) << "under the scale";
            EXPECT_EQ(Shaders::luminanceBin(1.0e6f), Shaders::EXPOSURE_BINS - 1u) << "over it";
        }
    }
}
