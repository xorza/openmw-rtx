#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <random>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/frame/bluenoise.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/tables/bluenoisemaker.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::size_t sExtent = Shaders::BLUE_NOISE_EXTENT;
        constexpr std::size_t sCount = sExtent * sExtent;

        /// One channel of the tile, as a plain field.
        std::vector<float> channel(std::uint32_t which)
        {
            const std::span<const float> values = sBlueNoise;

            std::vector<float> field(sCount);
            for (std::size_t i = 0; i < sCount; ++i)
                field[i] = values[i * Shaders::RANDOM_STREAMS + which];

            return field;
        }

        /// How much of the field's power sits at each distance from the origin in frequency. The mean is taken
        /// out first so the zero-frequency term does not drown the rest, and distances wrap, because the tile does.
        /// A plain O(n^2) transform over four thousand cells: a fast one would be a second implementation to be
        /// right about for no gain here.
        std::vector<double> radialPower(const std::vector<float>& field)
        {
            const double mean = std::accumulate(field.begin(), field.end(), 0.0) / static_cast<double>(sCount);

            std::vector<double> power(sExtent / 2 + 1, 0.0);
            std::vector<double> counts(sExtent / 2 + 1, 0.0);

            for (std::size_t v = 0; v < sExtent; ++v)
                for (std::size_t u = 0; u < sExtent; ++u)
                {
                    std::complex<double> sum;
                    for (std::size_t y = 0; y < sExtent; ++y)
                        for (std::size_t x = 0; x < sExtent; ++x)
                        {
                            const double turns = static_cast<double>(u * x + v * y) / static_cast<double>(sExtent);
                            sum += (static_cast<double>(field[y * sExtent + x]) - mean)
                                * std::polar(1.0, -2.0 * std::numbers::pi_v<double> * turns);
                        }

                    const std::size_t du = std::min(u, sExtent - u);
                    const std::size_t dv = std::min(v, sExtent - v);
                    const auto radius = static_cast<std::size_t>(
                        std::lround(std::hypot(static_cast<double>(du), static_cast<double>(dv))));

                    if (radius < power.size())
                    {
                        power[radius] += std::norm(sum);
                        counts[radius] += 1.0;
                    }
                }

            for (std::size_t r = 0; r < power.size(); ++r)
                if (counts[r] > 0.0)
                    power[r] /= counts[r];

            return power;
        }

        /// The share of a field's power below `sExtent / 8` — the blotches a filter cannot remove.
        ///
        /// Everything but the zero-frequency term, which the transform has already taken out.
        double lowFrequencyShare(const std::vector<float>& field)
        {
            const std::vector<double> power = radialPower(field);
            const double total = std::accumulate(power.begin() + 1, power.end(), 0.0);
            const double low = std::accumulate(power.begin() + 1, power.begin() + sExtent / 8, 0.0);

            return low / total;
        }

        /// The same values in a random order, which is what the tile is an improvement on.
        std::vector<float> shuffledSameValues()
        {
            std::vector<std::uint32_t> ranks(sCount);
            std::iota(ranks.begin(), ranks.end(), std::uint32_t{ 0 });
            std::shuffle(ranks.begin(), ranks.end(), std::mt19937(12345));

            std::vector<float> field(sCount);
            for (std::size_t i = 0; i < sCount; ++i)
                field[i] = (static_cast<float>(ranks[i]) + 0.5f) / static_cast<float>(sCount);

            return field;
        }

        /// Every channel holds each rank exactly once, which is what makes the values uniform.
        ///
        /// **Not a statistical claim but an exact one.** Void-and-cluster ranks the pixels, so the
        /// values are the stratified sequence `(i + 0.5) / count` dealt out over the tile — every
        /// draw from it is uniform by construction rather than on average, and a sampler offset by
        /// it inherits that. A generator that lost or repeated a rank would still look like noise.
        TEST(RtxBlueNoiseTest, everyChannelIsAPermutationOfTheSameStratifiedValues)
        {
            const std::span<const float> values = sBlueNoise;
            ASSERT_EQ(values.size(), sCount * Shaders::RANDOM_STREAMS);

            for (std::uint32_t which = 0; which < Shaders::RANDOM_STREAMS; ++which)
            {
                std::vector<std::uint32_t> ranks;
                ranks.reserve(sCount);
                for (const float value : channel(which))
                {
                    EXPECT_GT(value, 0.0f) << "channel " << which;
                    EXPECT_LT(value, 1.0f) << "channel " << which;
                    ranks.push_back(static_cast<std::uint32_t>(std::lround(value * sCount - 0.5f)));
                }

                std::sort(ranks.begin(), ranks.end());

                std::vector<std::uint32_t> expected(sCount);
                std::iota(expected.begin(), expected.end(), std::uint32_t{ 0 });
                EXPECT_EQ(ranks, expected) << "channel " << which << " is a permutation of its ranks";
            }
        }

        /// **The tile the build compiled in is the one the maker makes**, bit for bit: a table the
        /// build kept from an older maker would trace other noise than the tests here hold to.
        TEST(RtxBlueNoiseTest, theCompiledTileIsTheOneTheMakerMakes)
        {
            const std::vector<float> made = makeBlueNoise();
            ASSERT_EQ(made.size(), sBlueNoiseValues);

            const auto differs = std::ranges::mismatch(sBlueNoise, made, [](float compiled, float maker) {
                return std::bit_cast<std::uint32_t>(compiled) == std::bit_cast<std::uint32_t>(maker);
            }).in1;
            EXPECT_EQ(differs, sBlueNoise.end()) << "the first value apart is " << (differs - sBlueNoise.begin());
        }

        /// The tile is the same tile on every toolchain: its first two pixels and its last, rank by rank.
        ///
        /// **Pinned, because the seeds alone did not pin it.** `std::shuffle`'s algorithm is each standard
        /// library's own, so one seed shuffled into two tiles. These are the ranks the written-out shuffle deals,
        /// the same on GCC and Clang, libstdc++ and libc++.
        TEST(RtxBlueNoiseTest, theTileIsTheSameOnEveryToolchain)
        {
            ASSERT_EQ(Shaders::RANDOM_STREAMS, 14u) << "the pinned ranks are fourteen channels a pixel";

            const std::span<const float> values = sBlueNoise;
            const auto rankAt = [&](std::size_t pixel, std::uint32_t which) {
                return static_cast<std::uint32_t>(
                    std::lround(values[pixel * Shaders::RANDOM_STREAMS + which] * static_cast<float>(sCount) - 0.5f));
            };

            constexpr std::array<std::array<std::uint32_t, 14>, 3> pinned{ {
                { 2482, 1759, 429, 2938, 2345, 2700, 2500, 2014, 262, 2884, 2238, 1636, 3908, 2579 },
                { 3266, 3057, 3117, 697, 2832, 2169, 187, 920, 3918, 2065, 1313, 2816, 2614, 2084 },
                { 2081, 2646, 2559, 3383, 2957, 4082, 3350, 547, 2428, 928, 984, 692, 1772, 492 },
            } };
            const std::array<std::size_t, 3> pixels{ 0, 1, sCount - 1 };
            for (std::size_t at = 0; at < pixels.size(); ++at)
                for (std::uint32_t which = 0; which < Shaders::RANDOM_STREAMS; ++which)
                    EXPECT_EQ(rankAt(pixels[at], which), pinned[at][which])
                        << "pixel " << pixels[at] << " channel " << which;
        }

        /// The channels differ, which is the only thing that keeps two draws by one pixel apart.
        ///
        /// They were the same number once — the fog's march offset and the bounce's elevation — and
        /// a tile whose channels agreed would put that back without changing a line of the shader.
        TEST(RtxBlueNoiseTest, theChannelsAreNotCopiesOfEachOther)
        {
            for (std::uint32_t which = 1; which < Shaders::RANDOM_STREAMS; ++which)
            {
                const std::vector<float> first = channel(0);
                const std::vector<float> other = channel(which);

                std::size_t same = 0;
                for (std::size_t i = 0; i < sCount; ++i)
                    same += first[i] == other[i] ? 1u : 0u;

                // Two independent permutations of four thousand values agree in one place on
                // average, so anything past a handful is not chance.
                EXPECT_LT(same, std::size_t{ 16 }) << "channel " << which << " against channel 0";
            }
        }

        /// The tile has next to no low-frequency power, and the same values shuffled have plenty.
        ///
        /// **This is a claim about arrangement only.** Both fields below hold the identical values, so a histogram
        /// could not tell them apart: void-and-cluster spends its power above the eighth of the spectrum measured
        /// here and a shuffle puts a fifth of it underneath. That fifth survives a denoiser, which cannot tell a
        /// low-frequency blotch from smooth lighting.
        ///
        /// The bar is a hundredth of the shuffle's share. Measured, the two are 0.01% against 20%, and a mask left
        /// at its initial random arrangement lands on the shuffle's figure.
        TEST(RtxBlueNoiseTest, theTileSpendsItsPowerHighWhereTheSameValuesShuffledSpreadItEvenly)
        {
            const double shuffled = lowFrequencyShare(shuffledSameValues());
            const double tiled = lowFrequencyShare(channel(0));

            EXPECT_GT(shuffled, 0.1) << "a shuffle puts a good share of its power low";
            EXPECT_LT(tiled, shuffled * 0.01) << "and the tile puts a hundredth of that there";
        }
    }
}
