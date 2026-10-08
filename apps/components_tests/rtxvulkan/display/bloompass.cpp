#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/device/readback.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/display/bloompass.hpp>
#include <components/rtxvulkan/shaders/shared/bloom.h>

namespace Rtx
{
    namespace
    {
        /// Big enough for every level `BLOOM_LEVELS` asks for: six halvings of 320 by 256 end at
        /// 5 by 4, which is the last one no narrower than `BLOOM_NARROWEST`.
        constexpr std::uint32_t sWidth = 320;
        constexpr std::uint32_t sHeight = 256;

        /// Puts `pixels` in the image and leaves it in `VK_IMAGE_LAYOUT_GENERAL`, where the frame
        /// path leaves it.
        void paint(CommandPool& pool, const Device& device, const Image& image, std::span<const float> pixels)
        {
            const Buffer staging
                = Buffer::hostWritten(device, pixels.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test");
            staging.writeAt(0, pixels);

            pool.submitAndWait([&](VkCommandBuffer commands) {
                image.transition(commands, Use::sUndefined, Use::sCopyWrite);

                const VkBufferImageCopy region = wholeLevel(0, 0, VkExtent3D{ image.getWidth(), image.getHeight(), 1 });
                vkCmdCopyBufferToImage(
                    commands, staging.getHandle(), image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                image.transition(commands, Use::sCopyWrite, Use::sComputeSample);
            });
        }

        /// The red channel at a texel, which is the one every frame below varies.
        float redAt(std::span<const float> values, std::uint32_t width, std::uint32_t x, std::uint32_t y)
        {
            return values[(std::size_t{ y } * width + x) * 4];
        }

        /// The pass, a frame the size it was built for, and the pool that drives both.
        ///
        /// Bundled because the three have to agree about the extent: a pyramid built for one frame
        /// and run over another is what `BloomPass::record` asserts against, and a test that got it
        /// wrong would abort rather than fail.
        struct Bloomed
        {
            BloomPass mBloom;
            Image mFrame;

            /// What the frame's halving weighs its squares by: nought, the plain average, unless a
            /// test of the Karis average says otherwise.
            Buffer mExposure;

            Bloomed(const Device& device, std::uint32_t width, std::uint32_t height, float exposure = 0.0f)
                : mBloom(device)
                , mFrame(Testing::makeTestImage(
                      device, VkExtent2D{ width, height }, VK_FORMAT_R32G32B32A32_SFLOAT, "test-bloom-frame"))
                , mExposure(Buffer::hostWritten(device, sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "test"))
            {
                mBloom.resize(width, height);
                mExposure.writable<float>(0, 1).front() = exposure;
            }

            /// What the pyramid's finest level holds after one run, or nothing where there is no
            /// pyramid.
            std::vector<float> over(const Device& device, std::span<const float> pixels)
            {
                paint(device.getPool(), device, mFrame, pixels);
                device.getPool().submitAndWait(
                    [&](VkCommandBuffer commands) { mBloom.record(commands, mFrame, mExposure, nullptr); });

                const Image* pyramid = mBloom.getPyramid();
                return pyramid != nullptr ? Testing::readHalves(*pyramid) : std::vector<float>();
            }
        };

        struct RtxBloomPassTest : Testing::DeviceTest
        {
        };

        /// A frame with nothing to spread comes out of the pyramid as it went in.
        ///
        /// **Both kernels are partitions of one**, so a flat frame halved is the same flat frame at
        /// every level, and mixing a value with itself at any weight is that value. A weight table
        /// that summed to anything else, a mix taken the wrong way round, or a level read before the
        /// dispatch that filled it had finished, all move this.
        TEST_F(RtxBloomPassTest, aFlatFrameComesBackFlat)
        {
            const Device& device = getDevice();

            // Under the Karis average as well: every square of a flat frame weighs the same, so the
            // weights divide back out.
            Bloomed run(device, sWidth, sHeight, 1.0f);
            EXPECT_EQ(run.mBloom.getLevelCount(), Shaders::BLOOM_LEVELS) << "a frame with room for every halving";

            // Quarters, so the half floats the levels are kept in hold each of them exactly and what
            // is compared below is the arithmetic rather than the rounding.
            std::vector<float> flat(std::size_t{ sWidth } * sHeight * 4);
            for (std::size_t at = 0; at < flat.size(); at += 4)
            {
                flat[at] = 0.25f;
                flat[at + 1] = 0.5f;
                flat[at + 2] = 0.75f;
                flat[at + 3] = 1.0f;
            }

            const std::vector<float> spread = run.over(device, flat);

            ASSERT_EQ(spread.size(), std::size_t{ sWidth / 2 } * (sHeight / 2) * 4);
            for (std::size_t at = 0; at < spread.size(); at += 4)
            {
                ASSERT_NEAR(spread[at], 0.25f, 1.0e-3f) << "at " << at;
                ASSERT_NEAR(spread[at + 1], 0.5f, 1.0e-3f) << "at " << at;
                ASSERT_NEAR(spread[at + 2], 0.75f, 1.0e-3f) << "at " << at;
            }
        }

        /// A frame with one bright square in it comes out spread, and falls away with distance.
        ///
        /// **The pyramid is an average of the frame**, so nothing in it may be brighter than the
        /// brightest thing that went in, the square's own texels have to come out dimmer than the
        /// square was, and a texel that was black has to come out lit. Between those the glow must
        /// fall away, which is the whole of what six halvings and five tents are for.
        TEST_F(RtxBloomPassTest, oneBrightSquareSpreadsAndFallsAwayWithDistance)
        {
            const Device& device = getDevice();

            constexpr float sBright = 8.0f;
            constexpr std::uint32_t sBlock = 16;

            std::vector<float> square(std::size_t{ sWidth } * sHeight * 4);
            for (std::size_t at = 3; at < square.size(); at += 4)
                square[at] = 1.0f;

            const std::uint32_t left = sWidth / 2 - sBlock / 2;
            const std::uint32_t top = sHeight / 2 - sBlock / 2;
            for (std::uint32_t y = top; y < top + sBlock; ++y)
                for (std::uint32_t x = left; x < left + sBlock; ++x)
                    square[(std::size_t{ y } * sWidth + x) * 4] = sBright;

            Bloomed run(device, sWidth, sHeight);
            const std::vector<float> spread = run.over(device, square);

            // The level is half the frame across, so the square's own eight texels of it are centred
            // on 80 by 64 and its left edge is at 76.
            const std::uint32_t across = sWidth / 2;
            const std::uint32_t middle = sHeight / 4;
            const std::uint32_t edge = left / 2;

            for (std::size_t at = 0; at < spread.size(); at += 4)
                ASSERT_LE(spread[at], sBright) << "an average of the frame cannot exceed it, at " << at;

            const float centre = redAt(spread, across, across / 2, middle);
            EXPECT_LT(centre, sBright) << "a square that spread nothing is a bloom that did nothing";
            EXPECT_GT(centre, 0.0f);

            // And it falls away: four readings out from the square's edge, each dimmer than the one
            // inside it. A tent that lost its texel size would be flat across these.
            float last = redAt(spread, across, edge - 1, middle);
            EXPECT_GT(last, 0.0f) << "a texel that was black is lit";

            for (const std::uint32_t out : { 4u, 12u, 28u, 60u })
            {
                const float here = redAt(spread, across, edge - 1 - out, middle);
                EXPECT_LT(here, last) << "at " << out << " texels out";
                EXPECT_GT(here, 0.0f) << "and the widest level still reaches there";
                last = here;
            }
        }

        /// Every level stands on its source's corners, an odd source and an odd spread included, so
        /// a ramp comes back the ramp.
        ///
        /// **The arithmetic.** The frame is 83 by 16 and holds its column's index in red, so texel
        /// `i` is `i`. Both kernels are symmetric partitions of one and every tap lands on a texel
        /// corner or a quarter of the way between centres, so over a ramp each is the ramp at the
        /// point it stands on. Level 0, 41 across, puts texel `p` on the corner between frame texels
        /// `2p` and `2p + 1`: `2p + ½`. Level 1, 20 across from 41, puts texel `r` on the corner
        /// between level-0 texels `2r` and `2r + 1`: `4r + 1½`, which is the frame's ramp at that
        /// texel's centre again. The tent back up reads level 1 at `(q + ½) / 2` of its texels:
        /// `4 (q + ½) / 2 - ½ = 2q + ½`, level 0's own value, so the mix leaves it. Exact in half
        /// floats, being halves under 64. Only where no tap reaches a clamped edge — level-0 texels
        /// 8 to 30 — since a clamp is no ramp. Stretched over a level of odd width as it was, a
        /// texel stood up to half a source texel off its corner and read the ramp that far off.
        TEST_F(RtxBloomPassTest, everyLevelStandsOnItsSourcesCorners)
        {
            const Device& device = getDevice();

            constexpr std::uint32_t width = 83;
            constexpr std::uint32_t height = 16;
            Bloomed run(device, width, height);
            ASSERT_EQ(run.mBloom.getLevelCount(), 2u) << "16 high halves to 8 and 4, and no further";

            std::vector<float> ramp(std::size_t{ width } * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    ramp[(std::size_t{ y } * width + x) * 4] = static_cast<float>(x);
                    ramp[(std::size_t{ y } * width + x) * 4 + 3] = 1.0f;
                }

            const std::vector<float> spread = run.over(device, ramp);
            constexpr std::uint32_t across = width / 2;
            ASSERT_EQ(spread.size(), std::size_t{ across } * (height / 2) * 4);
            for (std::uint32_t q = 8; q <= 30; ++q)
                for (std::uint32_t y = 0; y < height / 2; ++y)
                    ASSERT_NEAR(redAt(spread, across, q, y), 2.0f * static_cast<float>(q) + 0.5f, 1.0e-3f)
                        << "level-0 texel " << q << ", row " << y;
        }

        /// The frame's halving is the Karis average, the device's to the host's.
        ///
        /// **The host's reference** stands each tap on its corner, so a tap is the mean of the two
        /// texels either side of it on each axis, held to the frame at its edges as the sampler
        /// holds it: thirteen of them, five squares of four, each weighed by `0.5` or `0.125` and by
        /// `1 / (1 + luminance × exposure)`, over their sum. An 8 by 8 frame halves once, to 4 by
        /// 4, and to nothing more, so what the pass hands back is the halving itself. The frame
        /// is a pattern under one in each channel and a firefly of fifty in red. Half floats keep
        /// eleven bits, so the two agree to a thousandth of the value.
        ///
        /// **And the exposure matters**: at nought the weights are alike and the halving is the
        /// plain average, and at one the squares that hold the firefly weigh less than those
        /// beside them, so less of it spreads into the texels around its own. Its own texel keeps
        /// its value: all five of its squares hold the firefly, and alike weights divide out.
        TEST_F(RtxBloomPassTest, theFramesHalvingIsTheKarisAverage)
        {
            const Device& device = getDevice();

            constexpr int size = 8;
            constexpr int half = size / 2;
            std::vector<float> frame(std::size_t{ size } * size * 4);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    float* const texel = &frame[(std::size_t(y) * size + std::size_t(x)) * 4];
                    texel[0] = static_cast<float>((x * 5 + y * 3) % 8) / 8.0f;
                    texel[1] = static_cast<float>((x * 3 + y * 7) % 8) / 8.0f;
                    texel[2] = static_cast<float>((x + y * 5) % 8) / 8.0f;
                    texel[3] = 1.0f;
                }
            frame[(std::size_t{ 3 } * size + 4) * 4] = 50.0f;

            const auto at = [&](int x, int y, int channel) {
                x = std::clamp(x, 0, size - 1);
                y = std::clamp(y, 0, size - 1);
                return frame[(std::size_t(y) * size + std::size_t(x)) * 4 + std::size_t(channel)];
            };
            // A tap on the corner at `(x, y)` in texels: the four texels around it.
            const auto tap = [&](int x, int y, int channel) {
                return 0.25f
                    * (at(x - 1, y - 1, channel) + at(x, y - 1, channel) + at(x - 1, y, channel) + at(x, y, channel));
            };
            const auto halved = [&](int px, int py, int channel, float exposure) {
                const int cx = 2 * px + 1;
                const int cy = 2 * py + 1;
                struct Square
                {
                    std::array<std::array<int, 2>, 4> mTaps;
                    float mShare;
                };
                const std::array<Square, 5> squares{ {
                    { { { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } } }, 0.5f },
                    { { { { -2, -2 }, { 0, -2 }, { -2, 0 }, { 0, 0 } } }, 0.125f },
                    { { { { 0, -2 }, { 2, -2 }, { 0, 0 }, { 2, 0 } } }, 0.125f },
                    { { { { -2, 0 }, { 0, 0 }, { -2, 2 }, { 0, 2 } } }, 0.125f },
                    { { { { 0, 0 }, { 2, 0 }, { 0, 2 }, { 2, 2 } } }, 0.125f },
                } };
                float weighed = 0.0f;
                float total = 0.0f;
                for (const Square& square : squares)
                {
                    std::array<float, 3> mean{};
                    for (const auto& [dx, dy] : square.mTaps)
                        for (int c = 0; c < 3; ++c)
                            mean[std::size_t(c)] += 0.25f * tap(cx + dx, cy + dy, c);
                    const float luminance = 0.2126f * mean[0] + 0.7152f * mean[1] + 0.0722f * mean[2];
                    const float weight = square.mShare / (1.0f + luminance * exposure);
                    weighed += weight * mean[std::size_t(channel)];
                    total += weight;
                }
                return weighed / total;
            };

            std::array<float, 2> beside{};
            for (const float exposure : { 0.0f, 1.0f })
            {
                Bloomed run(device, size, size, exposure);
                ASSERT_EQ(run.mBloom.getLevelCount(), 1u);
                const std::vector<float> level = run.over(device, frame);
                ASSERT_EQ(level.size(), std::size_t{ half } * half * 4);
                for (int py = 0; py < half; ++py)
                    for (int px = 0; px < half; ++px)
                        for (int c = 0; c < 3; ++c)
                        {
                            const float expected = halved(px, py, c, exposure);
                            EXPECT_NEAR(level[(std::size_t(py) * half + std::size_t(px)) * 4 + std::size_t(c)],
                                expected, 1.0e-3f * expected + 1.0e-4f)
                                << "texel " << px << ", " << py << " channel " << c << " at an exposure of "
                                << exposure;
                        }
                for (int py = 0; py < half; ++py)
                    for (int px = 0; px < half; ++px)
                        if (px != 2 || py != 1)
                            beside[exposure > 0.0f ? 1 : 0] += halved(px, py, 0, exposure);
            }

            // The reference at 15.80 plain and 14.33 weighed, which the device met texel by texel.
            EXPECT_LT(beside[1], beside[0] - 1.0f) << "the Karis average left the firefly its spread";
        }

        /// A frame too small to halve is one the pass builds no pyramid for.
        ///
        /// The levels are counted down from `BLOOM_LEVELS` and stop at `BLOOM_NARROWEST`, so a
        /// thumbnail gets a narrower pyramid and something smaller than one texel of it gets none —
        /// which the display pass reads as no lens rather than as a sampled stand-in.
        TEST_F(RtxBloomPassTest, aFrameTooSmallToHalveGetsNoPyramid)
        {
            const Device& device = getDevice();

            // 40 by 32 halves to 20, 10 and 5 across, and to 16, 8 and 4 down — three levels, where
            // the fourth would be 2 high.
            BloomPass counting(device);
            counting.resize(40, 32);
            EXPECT_EQ(counting.getLevelCount(), 3u);
            EXPECT_NE(counting.getPyramid(), nullptr);

            counting.resize(sWidth, sHeight);
            EXPECT_EQ(counting.getLevelCount(), Shaders::BLOOM_LEVELS) << "and it grows back";

            counting.resize(6, 6);
            EXPECT_EQ(counting.getLevelCount(), 0u) << "three across is under the narrowest level";
            EXPECT_EQ(counting.getPyramid(), nullptr);

            // **And a frame with no pyramid closes its zone**, so the zone after it opens and both
            // come back: the pass returned before its close, and the next open was an assert.
            Bloomed small(device, 6, 6);
            GpuTimer timer(device);
            timer.beginFrame();
            device.getPool().submitAndWait([&](VkCommandBuffer commands) {
                small.mBloom.record(commands, small.mFrame, small.mExposure, &timer);
                const GpuZone after(&timer, commands, FrameZone::Tone);
            });
            GpuZones zones;
            timer.resolve(zones);
            if (zones.spans().empty())
                GTEST_SKIP() << "this device cannot write timestamps";
            ASSERT_EQ(zones.spans().size(), 2u);
            EXPECT_EQ(zones.spans()[0].mZone, FrameZone::Bloom);
            EXPECT_EQ(zones.spans()[1].mZone, FrameZone::Tone);
        }
    }
}
