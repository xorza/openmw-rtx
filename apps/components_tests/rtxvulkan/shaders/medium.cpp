#include <cmath>

#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/medium.h>

namespace Rtx
{
    namespace
    {
        /// **What a stretch keeps of an even source, to its digits however thin the air.** Against
        /// `-expm1(-x) / x` in double, which loses nothing: within two parts in a million at every
        /// optical depth from nought to ten, on both sides of the switch at a sixteenth, where
        /// `(1 - e^-x) / x` in floats was a fifth off at a ten-millionth — and at negative depths, which
        /// the water's beam looking up toward the sun reaches. One at nought, the limit where there is
        /// no medium, and `(1 - e^-1)` at one.
        TEST(RtxMediumTest, aStretchKeepsItsSourceToTheDigitsAtEveryDepth)
        {
            EXPECT_EQ(Shaders::mediumKept(0.0f), 1.0f);
            EXPECT_NEAR(Shaders::mediumKept(1.0f), 1.0f - std::exp(-1.0f), 1e-6f);

            for (const float x : { 1e-7f, 1e-5f, 1e-3f, 0.03f, 0.0624f, 0.0625f, 0.07f, 0.5f, 2.0f, 10.0f, -1e-5f,
                     -0.0624f, -0.0625f, -0.5f, -3.0f })
            {
                const double depth = static_cast<double>(x);
                const double truth = -std::expm1(-depth) / depth;
                EXPECT_NEAR(static_cast<double>(Shaders::mediumKept(x)), truth, truth * 2e-6) << "at " << x;
            }

            const float thin = 1e-7f;
            const double naive = static_cast<double>((1.0f - std::exp(-thin)) / thin);
            EXPECT_GT(std::abs(naive - 1.0), 0.05)
                << "the closed form in floats holds a ten-millionth, so this proves nothing";
        }

        /// **What the surface lets in of a light, and what the beam it lets in carries across its own
        /// line.** Overhead, Schlick's weight is nought, so the surface reflects `F0` and lets in
        /// `1 - F0`, and the beam goes on straight, its irradiance unchanged: both `0.97963`. At 45°,
        /// the weight is `(1 - 0.70711)^5 = 0.0021555`, so the surface reflects
        /// `0.020373 + 0.97963 * 0.0021555 = 0.022485` and lets in `0.97752`. Snell's law bends the
        /// beam to a sine of `0.70711 / 1.333 = 0.53046`, a cosine of `0.84771`, and the beam's
        /// irradiance is the light's in times `0.70711 / 0.84771`: `0.81539`. On the horizon and under
        /// it, the light arrives at grazing and nothing gets in.
        TEST(RtxMediumTest, theWaterLetsInWhatItsFresnelDoesNotReflect)
        {
            const Shaders::WaterCrossing overhead = Shaders::waterCrossingOf(Shaders::vec3(0.0f, 0.0f, 1.0f));
            EXPECT_NEAR(overhead.mInto, 0.979627f, 1e-6f);
            EXPECT_NEAR(overhead.mBeam, 0.979627f, 1e-6f);

            const float diagonal = std::sqrt(0.5f);
            const Shaders::WaterCrossing slant = Shaders::waterCrossingOf(Shaders::vec3(diagonal, 0.0f, diagonal));
            EXPECT_NEAR(slant.mInto, 0.97752f, 1e-5f);
            EXPECT_NEAR(slant.mBeam, 0.81539f, 1e-5f);

            for (const Shaders::vec3 grazing :
                { Shaders::vec3(1.0f, 0.0f, 0.0f), Shaders::vec3(0.0f, 0.6f, -0.8f), Shaders::vec3(0.0f, 0.0f, -1.0f) })
            {
                const Shaders::WaterCrossing crossing = Shaders::waterCrossingOf(grazing);
                EXPECT_EQ(crossing.mInto, 0.0f);
                EXPECT_EQ(crossing.mBeam, 0.0f);
            }
        }
    }
}
