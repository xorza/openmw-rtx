#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <gtest/gtest.h>

#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>

namespace Rtx::Shaders
{
    namespace
    {
        /// Henyey-Greenstein in double, the curve written out: `(1 - g^2) / (4 pi (1 + g^2 - 2 g c)^(3/2))`.
        double phaseOf(const double g, const double cosine)
        {
            const double denominator = 1.0 + g * g - 2.0 * g * cosine;
            return (1.0 - g * g) / (4.0 * std::numbers::pi * denominator * std::sqrt(denominator));
        }

        /// **Henyey-Greenstein is the even share at no asymmetry and its own curve at any other**: `1 / 4 pi`
        /// at `g = 0` whatever the angle; at six tenths, head-on `0.64 / (4 pi 0.064) = 0.7958`, broadside
        /// `0.64 / (4 pi 1.36^1.5) = 0.0321`, and backward `0.64 / (4 pi 2.56^1.5) = 0.0124`.
        TEST(RtxScatteringTest, henyeyGreensteinIsTheEvenShareWithNoAsymmetryAndItsCurveWithOne)
        {
            for (const float cosine : { -1.0f, 0.0f, 0.5f, 1.0f })
                EXPECT_NEAR(henyeyGreenstein(0.0f, cosine), 0.25 / std::numbers::pi, 1.0e-7) << cosine;
            for (const float cosine : { -1.0f, 0.0f, 1.0f })
                EXPECT_NEAR(henyeyGreenstein(0.6f, cosine), phaseOf(0.6, cosine), 1.0e-6 * phaseOf(0.6, cosine))
                    << cosine;
        }

        /// **Smoke's scattering is eight octaves, each a half of the light and of the asymmetry before it**:
        /// `sum (1/2)^i HG(0.6 (1/2)^i, c)` for `i` below eight, against the sum written out in double at the
        /// three angles that bound it; and its integral over the sphere is the octaves' light, `2 (1 - 2^-8)`,
        /// each lobe integrating to one, so its mean is `(1 - 2^-8) / 2 pi`, a half of a card's `1 / pi` less
        /// the eighth octave's tail. The octaves matter, the more the further off the light's line: against the
        /// first lobe alone the sum is `0.95561 / 0.79577 = 1.20086` head-on, `0.10185 / 0.03211 = 3.17179`
        /// broadside and `0.05828 / 0.01243 = 4.68744` backward, each from the sum written out in double.
        TEST(RtxScatteringTest, smokeScattersAsEightOctavesOfHalvingLightAndAsymmetry)
        {
            const auto summed = [](const double cosine) {
                double total = 0.0;
                for (std::uint32_t octave = 0; octave < 8; ++octave)
                {
                    const double kept = std::pow(0.5, static_cast<double>(octave));
                    total += kept * phaseOf(0.6 * kept, cosine);
                }
                return total;
            };
            for (const float cosine : { -1.0f, 0.0f, 1.0f })
                EXPECT_NEAR(smokeScatter(cosine), summed(cosine), 1.0e-5 * summed(cosine)) << cosine;

            // Simpson's rule over the cosine, `2 pi` for the azimuth: the solid angle's measure is `dc dphi`.
            constexpr std::int32_t sSteps = 4000;
            double integral = 0.0;
            for (std::int32_t step = 0; step <= sSteps; ++step)
            {
                const double cosine = -1.0 + 2.0 * step / sSteps;
                const double weight = step == 0 || step == sSteps ? 1.0 : (step % 2 == 1 ? 4.0 : 2.0);
                integral += weight * smokeScatter(static_cast<float>(cosine));
            }
            integral *= 2.0 * std::numbers::pi * (2.0 / sSteps) / 3.0;
            EXPECT_NEAR(integral, 2.0 * (1.0 - 1.0 / 256.0), 2.0e-3);

            constexpr std::array<std::array<float, 2>, 3> sGained{ {
                { 1.0f, 1.20086f },
                { 0.0f, 3.17179f },
                { -1.0f, 4.68744f },
            } };
            for (const auto& [cosine, gained] : sGained)
                EXPECT_NEAR(smokeScatter(cosine) / henyeyGreenstein(0.6f, cosine), gained, 1.0e-4f) << cosine;
        }
    }
}
