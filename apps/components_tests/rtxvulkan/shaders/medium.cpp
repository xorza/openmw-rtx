#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

#include <gtest/gtest.h>

#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/shaders/shared/medium.h>

namespace Rtx
{
    namespace
    {
        /// Simpson's rule over `[0, length]` in double, at enough intervals that its own error is
        /// under any tolerance below.
        double integral(const std::function<double(double)>& integrand, double length)
        {
            constexpr int intervals = 20000;
            const double step = length / intervals;
            double sum = integrand(0.0) + integrand(length);
            for (int at = 1; at < intervals; ++at)
                sum += (at % 2 == 1 ? 4.0 : 2.0) * integrand(at * step);
            return sum * step / 3.0;
        }

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

        /// **A froxel holds the mean of what the air integrates, and a stretch integrates it.** Two
        /// froxels at a bank's edge, the dense one shadowed and the thin one lit: densities 2 and 0.5,
        /// lights 0.25 and 1, products 0.5 and 0.5. Half way between, the products' blend is 0.5, the
        /// mean of what was drawn; the factors' blends make `1.25 · 0.625 = 0.78125`, a half again.
        ///
        /// **And through a stretch whose density and product run linear** from those two to 200 units
        /// on, under an extinction of a hundredth a unit, `fogThrough` cut at each piece's middle as its
        /// callers cut a slice: the transmittance is exact at any cut, `exp(-0.01 · 1.25 · 200) =
        /// exp(-2.5)`, and the scattered light is the midpoint rule's, so its error quarters at every
        /// halving of the pieces, 0.2466, 0.0617, 0.0156, 0.0039 of the integral in double for one to
        /// eight pieces, and 6.1e-5 at sixty-four. The factors blended apart, at sixty-four pieces, scatter
        /// 0.3889 where the air scatters 0.2945.
        TEST(RtxMediumTest, aStretchIntegratesTheProductItsFroxelsHold)
        {
            constexpr float extinction = 0.01f;
            constexpr float length = 200.0f;
            constexpr std::array<float, 2> density{ 2.0f, 0.5f };
            constexpr std::array<float, 2> light{ 0.25f, 1.0f };

            const auto froxel = [&](std::size_t at) {
                Shaders::FogSlice slice;
                slice.mSource = Shaders::vec3(1.0f, 1.0f, 1.0f) * (density[at] * light[at]);
                slice.mDensity = density[at];
                slice.mSunSource = density[at] * light[at];
                return slice;
            };
            const Shaders::FogSlice dense = froxel(0);
            const Shaders::FogSlice thin = froxel(1);

            const Shaders::FogSlice between = Shaders::fogSliceBetween(dense, thin, 0.5f);
            EXPECT_EQ(between.mSource[0], 0.5f);
            EXPECT_EQ(between.mSunSource, 0.5f);
            EXPECT_EQ(between.mDensity, 1.25f);
            EXPECT_EQ((density[0] + density[1]) / 2.0f * ((light[0] + light[1]) / 2.0f), 0.78125f);

            const double denseD = density[0];
            const double thinD = density[1];
            const double denseProduct = denseD * static_cast<double>(light[0]);
            const double thinProduct = thinD * static_cast<double>(light[1]);
            const double per = static_cast<double>(extinction);
            const double span = static_cast<double>(length);
            const double scattered = integral(
                [&](double t) {
                    const double product = denseProduct + (thinProduct - denseProduct) * t / span;
                    const double depth = per * (denseD * t + (thinD - denseD) * t * t / (2.0 * span));
                    return per * product * std::exp(-depth);
                },
                span);
            const double kept = std::exp(-2.5);

            const auto through = [&](int pieces, bool factors) {
                Shaders::FogColumn column;
                column.mScattered = Shaders::vec3();
                column.mTransmittance = 1.0f;
                column.mSunward = 0.0f;
                for (int piece = 0; piece < pieces; ++piece)
                {
                    const float middle = (static_cast<float>(piece) + 0.5f) / static_cast<float>(pieces);
                    Shaders::FogSlice slice = Shaders::fogSliceBetween(dense, thin, middle);
                    if (factors)
                        slice.mSunSource = slice.mDensity * Shaders::mix(light[0], light[1], middle);
                    column = Shaders::fogThrough(column, slice, length / static_cast<float>(pieces), extinction);
                }
                return column;
            };

            double before = 0.0;
            for (const int pieces : { 1, 2, 4, 8, 64 })
            {
                const Shaders::FogColumn column = through(pieces, false);
                EXPECT_NEAR(static_cast<double>(column.mTransmittance), kept, kept * 1e-5) << pieces << " pieces";
                EXPECT_FLOAT_EQ(column.mScattered[0], column.mSunward) << pieces << " pieces";

                const double error = std::abs(static_cast<double>(column.mSunward) - scattered) / scattered;
                if (pieces == 1)
                    EXPECT_NEAR(error, 0.2466, 1e-4);
                else if (pieces < 64)
                    EXPECT_NEAR(before / error, 4.0, 0.05) << "the midpoint rule's order at " << pieces;
                else
                    EXPECT_LT(error, 1e-4);
                before = error;
            }

            EXPECT_GT(static_cast<double>(through(64, true).mSunward), scattered * 1.3)
                << "the factors blended apart scatter the same";
        }

        /// **The water's column gathers what its closed form says it does.** A light arriving `k` units
        /// of water per unit of depth reaches a point `t` along a ray of direction `d` through
        /// `k (h - t d.z)` of water and leaves toward the eye through `t`, so the stretch scatters
        /// `σ exp(-σ k h) exp(-σ (1 - k d.z) t)` per unit of it. Over a stretch, against Simpson's rule in
        /// double: looking down, level, up with `g` at nought (a light overhead, `k` one, looking straight
        /// at it), and up past it (`k` 1.3, `g` -0.3, a step further along nearer the light), over 500
        /// units, where the red's optical depth is 1.87, and over a thousandth of a unit, where every
        /// channel takes `mediumKept`'s series. Within the series' two parts in a million and a float's
        /// rounding.
        TEST(RtxMediumTest, theWatersColumnGathersItsClosedForm)
        {
            const float level = std::sqrt(0.5f);
            for (const float path : { 500.0f, 1e-3f })
                for (const Shaders::vec3 direction :
                    { Shaders::vec3(0.0f, 0.0f, -1.0f), Shaders::vec3(level, 0.0f, -level),
                        Shaders::vec3(1.0f, 0.0f, 0.0f), Shaders::vec3(0.0f, 0.0f, 1.0f) })
                    for (const float slant : { 1.0f, 1.3f })
                    {
                        const Shaders::vec3 gathered = Shaders::gatheredAlong(direction, slant, path);
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            const double extinction = static_cast<double>(Shaders::WATER_EXTINCTION[channel]);
                            const double rising = 1.0 - static_cast<double>(slant) * static_cast<double>(direction[2]);
                            const double truth
                                = integral([&](double t) { return extinction * std::exp(-extinction * rising * t); },
                                    static_cast<double>(path));
                            EXPECT_NEAR(static_cast<double>(gathered[channel]), truth, truth * 5e-6)
                                << "over " << path << " toward " << direction[2] << " at a slant of " << slant
                                << ", channel " << channel;
                        }
                    }
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
