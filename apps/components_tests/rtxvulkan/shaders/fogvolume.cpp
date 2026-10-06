#include <cmath>

#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/fogvolume.h>

namespace Rtx
{
    namespace
    {
        /// **What a stretch keeps of an even source, to its digits however thin the air.** Against
        /// `-expm1(-x) / x` in double, which loses nothing: within two parts in a million at every
        /// optical depth from nought to ten, on both sides of the switch at a sixteenth, where
        /// `(1 - e^-x) / x` in floats was a fifth off at a ten-millionth. One at nought, the limit
        /// where there is no air, and `(1 - e^-1)` at one.
        TEST(RtxFogKeptTest, aStretchKeepsItsSourceToTheDigitsAtEveryDepth)
        {
            EXPECT_EQ(Shaders::fogKept(0.0f), 1.0f);
            EXPECT_NEAR(Shaders::fogKept(1.0f), 1.0f - std::exp(-1.0f), 1e-6f);

            for (const float x : { 1e-7f, 1e-5f, 1e-3f, 0.03f, 0.0624f, 0.0625f, 0.07f, 0.5f, 2.0f, 10.0f })
            {
                const double depth = static_cast<double>(x);
                const double truth = -std::expm1(-depth) / depth;
                EXPECT_NEAR(static_cast<double>(Shaders::fogKept(x)), truth, truth * 2e-6) << "at " << x;
            }

            const float thin = 1e-7f;
            const double naive = static_cast<double>((1.0f - std::exp(-thin)) / thin);
            EXPECT_GT(std::abs(naive - 1.0), 0.05)
                << "the closed form in floats holds a ten-millionth, so this proves nothing";
        }
    }
}
