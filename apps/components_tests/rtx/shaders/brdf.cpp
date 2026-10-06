#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/radicalinverse.hpp>
#include <components/rtx/frame/specularalbedo.hpp>
#include <components/rtx/shaders/brdf.h>

namespace Rtx
{
    namespace
    {
        /// **A vanilla surface is the Lambert surface it was.** A reflectance of nought has an edge of
        /// nought, reflects exactly nothing at every angle, has a directional albedo of exactly
        /// nothing wherever the table is read, and is scaled by exactly one. A dielectric's 4% reaches
        /// an edge of one: `50 * 0.04` is two, saturated.
        TEST(RtxBrdfTest, aReflectanceOfNoughtReflectsExactlyNothing)
        {
            const float edge = Shaders::specularEdge(0.0f);
            EXPECT_EQ(edge, 0.0f);
            EXPECT_EQ(Shaders::specularEdge(Shaders::DIELECTRIC_F0), 1.0f);

            for (const float weight : { 0.0f, 0.25f, 1.0f })
                EXPECT_EQ(Shaders::fresnelSchlick(0.0f, edge, weight), 0.0f) << weight;

            const SpecularAlbedo& table = SpecularAlbedo::shared();
            for (const float roughness : { 0.0f, 0.3f, 1.0f })
                for (const float cosine : { 0.0f, 0.5f, 1.0f })
                {
                    const osg::Vec2f cell = table.at(cosine, roughness);
                    EXPECT_EQ(Shaders::specularAlbedoOf(0.0f, edge, cell.x(), cell.y()), 0.0f)
                        << cosine << ' ' << roughness;
                    EXPECT_EQ(Shaders::specularCompensation(0.0f, cell.y()), 1.0f) << cosine << ' ' << roughness;
                }
        }

        /// Smith's masking for GGX, `G1 = 1 / (1 + Λ)` with `Λ = (sqrt(1 + α² tan²θ) - 1) / 2`: the form
        /// `brdf.h` does not write, in double, for the tests to hold its forms to.
        double smithMasking(double alpha, double cosine)
        {
            const double tangent = (1.0 - cosine * cosine) / (cosine * cosine);
            return 2.0 / (1.0 + std::sqrt(1.0 + alpha * alpha * tangent));
        }

        /// **The lobe is reciprocal**: its visibility term is the same with the eye and the light
        /// swapped, to the rounding of a sum taken in the other order. **And what a visible-normal
        /// draw leaves of it is the quotient it stands for**: `G2 / G1` with `G2` the visibility
        /// times `4 (n.v) (n.l)` and `G1` through `Λ`, so nothing is shared with the closed form but
        /// the visibility.
        TEST(RtxBrdfTest, theSmithTermsAreReciprocalAndAgree)
        {
            for (const float alpha : { 0.002f, 0.1f, 0.5f, 1.0f })
                for (const float toEye : { 0.05f, 0.3f, 0.7f, 1.0f })
                    for (const float toLight : { 0.02f, 0.4f, 0.9f })
                    {
                        EXPECT_FLOAT_EQ(Shaders::smithVisibility(alpha, toEye, toLight),
                            Shaders::smithVisibility(alpha, toLight, toEye))
                            << alpha << ' ' << toEye << ' ' << toLight;

                        const double eye = toEye;
                        const double light = toLight;
                        const double shadowing
                            = static_cast<double>(Shaders::smithVisibility(alpha, toEye, toLight)) * 4.0 * eye * light;
                        EXPECT_NEAR(Shaders::smithShadowingGivenMasking(alpha, toEye, toLight),
                            shadowing / smithMasking(alpha, eye), 1e-6)
                            << alpha << ' ' << toEye << ' ' << toLight;
                    }
        }

        /// `∫ max(a + b cos φ, 0) dφ` over `[from, to]` inside `[0, π]`, for `b` of nought or more: the
        /// integrand is positive up to `acos(-a / b)` and nought past it, and its antiderivative is
        /// `a φ + b sin φ`.
        double positiveCosineIntegral(double a, double b, double from, double to)
        {
            double end = to;
            if (b > 0.0)
                end = std::min(to, std::acos(std::clamp(-a / b, -1.0, 1.0)));
            else if (!(a > 0.0))
                return 0.0;

            if (!(end > from))
                return 0.0;

            return a * (end - from) + b * (std::sin(end) - std::sin(from));
        }

        /// **A bounded draw spends fewer samples under the horizon and keeps the lobe's mean.** The
        /// reflection of the eye about each drawn facet, over 2^16 Hammersley points, at an eye 84
        /// degrees off the normal and three alphas: under the whole cap a share of the
        /// reflections leave under the horizon and weigh nought; under the bounded cap
        /// (`boundedCap`) fewer do, and each weighs `boundedWeight` of what it did. Both estimate one
        /// integral, the lobe's directional albedo with no Fresnel, `E[G2 / G1]`, so their means
        /// agree to the points' own error, 2e-3.
        TEST(RtxBrdfTest, aBoundedDrawWastesFewerReflectionsAndKeepsTheLobesMean)
        {
            constexpr std::uint32_t samples = 1u << 16;
            const osg::Vec3f eye(std::sqrt(1.0f - 0.1f * 0.1f), 0.0f, 0.1f);
            for (const float alpha : { 0.3f, 0.6f, 1.0f })
            {
                const auto drawn = [&](float bound) {
                    std::pair<double, double> wastedAndMean{ 0.0, 0.0 };
                    for (std::uint32_t at = 0; at < samples; ++at)
                    {
                        const float turn = Shaders::TAU * (static_cast<float>(at) + 0.5f) / static_cast<float>(samples);
                        const osg::Vec3f facet = Shaders::visibleNormal(
                            eye, alpha, radicalInverse(at, 2), osg::Vec2f(std::cos(turn), std::sin(turn)), bound);
                        const osg::Vec3f reflected = facet * (2.0f * (eye * facet)) - eye;
                        if (!(reflected.z() > 0.0f))
                        {
                            wastedAndMean.first += 1.0 / samples;
                            continue;
                        }
                        wastedAndMean.second
                            += static_cast<double>(Shaders::smithShadowingGivenMasking(alpha, eye.z(), reflected.z())
                                   * Shaders::boundedWeight(eye, alpha, bound))
                            / samples;
                    }
                    return wastedAndMean;
                };

                const auto [wholeWasted, wholeMean] = drawn(1.0f);
                const auto [boundedWasted, boundedMean] = drawn(Shaders::boundedCap(eye, alpha));
                EXPECT_GT(wholeWasted, 0.01) << "alpha " << alpha << ": nothing to bound, or this proves nothing";
                EXPECT_LT(boundedWasted, 0.5 * wholeWasted) << "alpha " << alpha;
                EXPECT_NEAR(boundedMean, wholeMean, 2e-3) << "alpha " << alpha;
            }
        }

        /// **The visible normals are drawn by their density**, `G1 max(v.h, 0) D(h) / (n.v)`: a
        /// histogram of `visibleNormal`'s draws, bin by bin, against that density integrated over
        /// the bin.
        ///
        /// The bins are even in `s = tan²θ / (α² + tan²θ)` — the projected distribution's own
        /// cumulative share, so each holds some of the lobe at every alpha — and in the facet's
        /// azimuth from the eye's, folded about the eye's plane, which the density is symmetric
        /// about. `D cosθ dω` is `ds dφ / 2π` in them, so the density folded is
        /// `G1 max(v_z + v_x α τ cos φ, 0) / (π v_z)` per `ds dφ` with `τ = sqrt(s / (1 - s))`.
        /// Taking `s = sin²β` makes `τ = tan β` and `ds = 2 sin β cos β dβ`, which leaves
        /// `v_z sin 2β + 2 v_x α sin²β cos φ` and no singularity at the lobe's edge; the azimuth is
        /// integrated exactly by `positiveCosineIntegral` and `β` by a midpoint rule. `G1` is
        /// `smithMasking`'s, so the expectation shares nothing with the draw — and it integrates to
        /// one within 6e-7 in every case, which this also holds it to.
        ///
        /// **2e-4 a bin is twice the worst the draw is off, and a twenty-fifth of a wrong draw.**
        /// 2^18 Hammersley points a case — the table's own set — come within 1e-4 of every bin, where
        /// independent draws would scatter a bin of the mean mass by 1.2e-4 a standard deviation. A
        /// draw that ignores the eye, the distribution projected onto the normal, is 5e-3 to 5e-2 off
        /// wherever the eye is not square on.
        TEST(RtxBrdfTest, theVisibleNormalsAreDrawnByTheirDensity)
        {
            constexpr std::uint32_t samples = 1u << 18;
            constexpr std::uint32_t bins = 16;
            constexpr std::uint32_t steps = 64;
            constexpr double pi = 3.14159265358979323846;

            std::vector<double> drawn(std::size_t{ bins } * bins);
            for (const float alpha : { 0.1f, 0.5f, 1.0f })
                for (const float cosine : { 1.0f, 0.5f, 0.1f })
                {
                    const osg::Vec3f eye(std::sqrt(1.0f - cosine * cosine), 0.0f, cosine);
                    const double stretch = alpha;

                    std::fill(drawn.begin(), drawn.end(), 0.0);
                    for (std::uint32_t at = 0; at < samples; ++at)
                    {
                        const float turn = Shaders::TAU * (static_cast<float>(at) + 0.5f) / static_cast<float>(samples);
                        const osg::Vec3f facet = Shaders::visibleNormal(
                            eye, alpha, radicalInverse(at, 2), osg::Vec2f(std::cos(turn), std::sin(turn)), 1.0f);

                        const double x = facet.x();
                        const double y = facet.y();
                        const double z = facet.z();
                        const double share = (x * x + y * y) / (stretch * stretch * z * z + x * x + y * y);
                        const double azimuth = std::abs(std::atan2(y, x));

                        const auto row = std::min(static_cast<std::uint32_t>(share * bins), bins - 1);
                        const auto column = std::min(static_cast<std::uint32_t>(azimuth / pi * bins), bins - 1);
                        drawn[std::size_t{ row } * bins + column] += 1.0 / samples;
                    }

                    const double eyeX = eye.x();
                    const double eyeZ = eye.z();
                    const double masking = smithMasking(alpha, eyeZ);

                    double total = 0.0;
                    for (std::uint32_t row = 0; row < bins; ++row)
                    {
                        const double low = std::asin(std::sqrt(static_cast<double>(row) / bins));
                        const double high = std::asin(std::sqrt(static_cast<double>(row + 1) / bins));
                        for (std::uint32_t column = 0; column < bins; ++column)
                        {
                            double mass = 0.0;
                            for (std::uint32_t step = 0; step < steps; ++step)
                            {
                                const double beta = low + (high - low) * (step + 0.5) / steps;
                                const double sine = std::sin(beta);
                                mass += positiveCosineIntegral(eyeZ * std::sin(2.0 * beta),
                                    2.0 * eyeX * stretch * sine * sine, pi * column / bins, pi * (column + 1) / bins);
                            }
                            mass *= (high - low) / steps * masking / (pi * eyeZ);
                            total += mass;

                            EXPECT_NEAR(drawn[std::size_t{ row } * bins + column], mass, 2e-4)
                                << "alpha " << alpha << ", eye cosine " << cosine << ", bin " << row << ' ' << column;
                        }
                    }

                    EXPECT_NEAR(total, 1.0, 1e-6) << "alpha " << alpha << ", eye cosine " << cosine;
                }
        }

        /// **The cone's edge is where the distribution falls to half**, at a quarter of the width:
        /// `D(θ) / D(0) = α⁴ / (cos²θ (α² - 1) + 1)²`, in double. By hand at `α = 0.3`,
        /// `tan²θ = 0.09 (√2 - 1) / (1 - 0.09 √2) = 0.0427161`, so `θ = 0.2038091` and the width is
        /// `0.8152363`. Past `α² = 1 / √2` — 0.85 and 1 — the lobe never falls to half above the
        /// surface and the answer is the widest, and it is never wider than that.
        TEST(RtxBrdfTest, theConeWidthIsWhereTheDistributionFallsToHalf)
        {
            for (const float alpha : { 0.002f, 0.05f, 0.3f, 0.8f })
            {
                const double edge = static_cast<double>(Shaders::ggxConeWidth(alpha, 10.0f)) / 4.0;
                const double squared = static_cast<double>(alpha) * static_cast<double>(alpha);
                const double cosine = std::cos(edge);
                const double falloff = cosine * cosine * (squared - 1.0) + 1.0;

                EXPECT_NEAR(squared * squared / (falloff * falloff), 0.5, 1e-5) << alpha;
            }

            EXPECT_NEAR(Shaders::ggxConeWidth(0.3f, 10.0f), 0.8152363f, 1e-6f);
            EXPECT_EQ(Shaders::ggxConeWidth(0.3f, 0.5f), 0.5f);
            EXPECT_EQ(Shaders::ggxConeWidth(0.85f, 10.0f), 10.0f);
            EXPECT_EQ(Shaders::ggxConeWidth(1.0f, 1.0f), 1.0f);
        }

        /// **A refracted cone is the reflected one times half the bend**, checked against Snell's law
        /// itself in double: a ray square on to a surface whose normal is tilted by a thousandth of a
        /// radian. The reflection turns by twice the tilt; the refraction into water by
        /// `1 - 1 / 1.333 = 0.2498125` of it, and out of water by `0.333` of it, so the two cones are
        /// `0.1249062` and `0.1665` of the reflected one. By hand at a reflected cone of 0.4:
        /// `0.0499625` entering and `0.0666` leaving.
        TEST(RtxBrdfTest, aRefractedConeIsTheReflectedOneTimesHalfTheBend)
        {
            const double tilt = 1e-3;
            const double normal[3] = { 0.0, std::sin(tilt), std::cos(tilt) };

            // Snell's law in vector form, for a unit `incident` against a unit `normal` facing it:
            // the turn of the refracted ray off the incident.
            const auto turned = [&](const double(&incident)[3], const double eta) {
                const double facing = -(incident[0] * normal[0] + incident[1] * normal[1] + incident[2] * normal[2]);
                const double sign = facing < 0.0 ? -1.0 : 1.0;
                const double cosine = facing * sign;
                const double k = 1.0 - eta * eta * (1.0 - cosine * cosine);
                double bent[3];
                for (int axis = 0; axis < 3; ++axis)
                    bent[axis] = eta * incident[axis] + sign * (eta * cosine - std::sqrt(k)) * normal[axis];
                const double along = bent[0] * incident[0] + bent[1] * incident[1] + bent[2] * incident[2];
                return std::acos(std::min(along, 1.0));
            };

            const double ior = 1.333;
            const double down[3] = { 0.0, 0.0, -1.0 };
            const double up[3] = { 0.0, 0.0, 1.0 };
            const double entering = turned(down, 1.0 / ior) / (2.0 * tilt);
            const double leaving = turned(up, ior) / (2.0 * tilt);

            EXPECT_NEAR(Shaders::refractedConeWidth(1.0f, 1.333f, false), entering, 1e-4);
            EXPECT_NEAR(Shaders::refractedConeWidth(1.0f, 1.333f, true), leaving, 1e-4);
            EXPECT_NEAR(Shaders::refractedConeWidth(0.4f, 1.333f, false), 0.0499625f, 1e-6f);
            EXPECT_NEAR(Shaders::refractedConeWidth(0.4f, 1.333f, true), 0.0666f, 1e-6f);
            EXPECT_NE(
                Shaders::refractedConeWidth(0.4f, 1.333f, false), Shaders::refractedConeWidth(0.4f, 1.333f, true));
        }

        /// **A field of slopes is the roughness a painted map would state for it**, so water's guide
        /// holds the quantity a solid's does and the shore blends two of one thing. By hand: slopes
        /// of `sigma = 0.1` along each axis are a variance of `0.02` over both, which is GGX's
        /// `alpha = sqrt(2) * 0.1 = 0.1414214` and a roughness of its root, `0.3760603`; a variance
        /// of `0.09` is `alpha = 0.3`, the cone test's; none is a mirror; and a sea rougher than GGX
        /// states is held at a roughness of one. A painted roughness widened by slopes a footprint
        /// averages is the same sum, and a classic map's exponent is a roughness by Walter's match.
        TEST(RtxBrdfTest, aFieldOfSlopesIsTheRoughnessAMapWouldStateForIt)
        {
            EXPECT_NEAR(Shaders::slopeRoughness(0.02f), 0.3760603f, 1e-6f);
            EXPECT_NEAR(Shaders::slopeRoughness(0.09f), std::sqrt(0.3f), 1e-6f);
            EXPECT_NEAR(Shaders::ggxAlpha(Shaders::slopeRoughness(0.09f)), 0.3f, 1e-6f);
            EXPECT_EQ(Shaders::slopeRoughness(0.0f), 0.0f);
            EXPECT_EQ(Shaders::slopeRoughness(2.0f), 1.0f);

            // Widened by lost slopes, which add to `alpha²`: a roughness of a half is `alpha² =
            // 0.0625`, and `0.1875` more is `0.25`, whose root's root is `√0.5`. A mirror takes the
            // lost slopes' roughness whole, one stays one, and nothing lost is the roughness itself.
            EXPECT_NEAR(Shaders::widenedRoughness(0.5f, 0.1875f), std::sqrt(0.5f), 1e-6f);
            EXPECT_NEAR(Shaders::widenedRoughness(0.0f, 0.09f), std::sqrt(0.3f), 1e-6f);
            EXPECT_EQ(Shaders::widenedRoughness(1.0f, 0.3f), 1.0f);
            EXPECT_EQ(Shaders::widenedRoughness(0.37f, 0.0f), 0.37f);

            // And the slopes a normal map's level lost: normals that agree lose none; a mean nine
            // tenths long, a loss of `1 - 0.81 = 0.19`, stands for `2 · 0.19 / (0.9 · 2.19) =
            // 0.192796`, which is `2 (1 - r²) / (r (3 - r²))` at `r = 0.9` as well; and normals that
            // cancel are past the widest roughness there is.
            EXPECT_EQ(Shaders::normalSpreadSlopes(0.0f), 0.0f);
            EXPECT_NEAR(Shaders::normalSpreadSlopes(0.19f), 0.38f / (0.9f * 2.19f), 1e-6f);
            EXPECT_EQ(Shaders::slopeRoughness(Shaders::normalSpreadSlopes(1.0f)), 1.0f);

            // A classic map's exponent by Walter's match: nought is `alpha = 1`, a roughness of one;
            // two is `alpha = √0.5`, a roughness of `0.5^(1/4) = 0.8408964`; and 254, the steepest an
            // alpha of a byte reaches short of 255, is `alpha = √(2 / 256) = 0.0883883`, a roughness
            // of `0.2973018`. Steeper is smoother.
            EXPECT_EQ(Shaders::roughnessOfExponent(0.0f), 1.0f);
            EXPECT_NEAR(Shaders::roughnessOfExponent(2.0f), 0.8408964f, 1e-6f);
            EXPECT_NEAR(Shaders::roughnessOfExponent(254.0f), 0.2973018f, 1e-6f);
            EXPECT_NEAR(Shaders::ggxAlpha(Shaders::roughnessOfExponent(254.0f)), std::sqrt(2.0f / 256.0f), 1e-6f);
        }

        /// **The distribution is normalised**: `∫ D (n.h) dω` is one for every alpha, which in
        /// `s = cos²` is `∫ D(√s) π ds` over nought to one — worked out by hand, the antiderivative
        /// of `α² / (s (α² - 1) + 1)²` is `-α² / ((α² - 1) (s (α² - 1) + 1))`, which rises by
        /// exactly one across the interval. A million midpoint steps put two and a half thousand
        /// across the narrowest peak, `α² = 0.0025` wide, and the rule is then good to a part in
        /// ten million.
        TEST(RtxBrdfTest, theDistributionProjectsToOne)
        {
            constexpr std::uint32_t steps = 1000000;
            for (const float alpha : { 0.05f, 0.2f, 0.5f, 1.0f })
            {
                double sum = 0.0;
                for (std::uint32_t at = 0; at < steps; ++at)
                {
                    const float squared = (static_cast<float>(at) + 0.5f) / static_cast<float>(steps);
                    sum += static_cast<double>(Shaders::ggxDistribution(alpha, std::sqrt(squared)));
                }

                EXPECT_NEAR(sum * static_cast<double>(Shaders::PI) / steps, 1.0, 1e-5) << alpha;
            }
        }
    }
}
