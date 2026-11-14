#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/hostwritten.hpp>
#include <apps/components_tests/rtxvulkan/support/device/probekernel.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/shaders/shared/sharedexponent.h>

namespace Rtx
{
    namespace
    {
        /// What the probe made of each colour: its word, and the colour the word holds.
        struct Packed
        {
            std::vector<std::uint32_t> mWords;
            std::vector<osg::Vec3f> mColours;
        };

        struct RtxSharedExponentTest : Testing::DeviceTest
        {
            Packed pack(std::span<const osg::Vec3f> colours)
            {
                const Device& device = getDevice();
                const auto count = static_cast<std::uint32_t>(colours.size());

                const Buffer in = Testing::hostWritten(getPool(), colours, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                const Buffer words = Buffer::readBack(
                    device, count * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shared exponent words");
                const Buffer out = Buffer::readBack(
                    device, colours.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shared exponent unpacked");

                const std::array buffers{ Testing::ProbeBuffer{ Shaders::SHARED_EXPONENT_BIND_COLOURS, &in },
                    Testing::ProbeBuffer{ Shaders::SHARED_EXPONENT_BIND_PACKED, &words },
                    Testing::ProbeBuffer{ Shaders::SHARED_EXPONENT_BIND_UNPACKED, &out } };
                Testing::runProbe(getPool(), "sharedexponent.comp.spv", buffers,
                    Shaders::SharedExponentConstants{ .mCount = count }, count, Shaders::SHARED_EXPONENT_WORKGROUP);

                const auto* wordsOut = static_cast<const std::uint32_t*>(words.map());
                const auto* coloursOut = static_cast<const osg::Vec3f*>(out.map());
                return Packed{
                    .mWords = std::vector<std::uint32_t>(wordsOut, wordsOut + count),
                    .mColours = std::vector<osg::Vec3f>(coloursOut, coloursOut + count),
                };
            }
        };

        /// A word of three nine-bit mantissas, red lowest, under a five-bit exponent biased by 15.
        constexpr std::uint32_t wordOf(std::uint32_t r, std::uint32_t g, std::uint32_t b, int exponent)
        {
            return r | (g << 9u) | (b << 18u) | (static_cast<std::uint32_t>(exponent + 15) << 27u);
        }

        /// **Each case by hand**, a colour being `m × 2^(e − 9)` a channel, `e` the brightest
        /// channel's `frexp` exponent:
        ///
        /// - `(1, 0.5, 0.25)` is `e = 1` and mantissas `(256, 128, 64)`, back to the bit;
        /// - 0.9995 rounds to 512 at `e = 0`, which nine bits cannot hold, so it is `e = 1` and
        ///   256, which is 1;
        /// - a channel under the brightest's step is nought: `(1, 2^-10, 0)` is `(256, 0, 0)`,
        ///   since `2^-10 × 2^8` rounds to nought;
        /// - below nought is nought, and past `511 × 2^7 = 65408` is that, at `e = 16`;
        /// - the exponent stops at −15, so `2^-24` is the mantissa 1 there, and `2^-26` rounds to
        ///   nought.
        TEST_F(RtxSharedExponentTest, aColourIsNineBitsAChannelUnderTheExponentOfItsBrightest)
        {
            const std::array colours{
                osg::Vec3f(1.0f, 0.5f, 0.25f),
                osg::Vec3f(0.9995f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, std::ldexp(1.0f, -10), 0.0f),
                osg::Vec3f(-1.0f, 1.0e6f, 0.0f),
                osg::Vec3f(std::ldexp(1.0f, -24), std::ldexp(1.0f, -26), 0.0f),
            };
            const Packed packed = pack(colours);

            EXPECT_EQ(packed.mWords[0], wordOf(256, 128, 64, 1));
            EXPECT_EQ(packed.mColours[0], colours[0]) << "an exact colour did not come back to the bit";

            EXPECT_EQ(packed.mWords[1], wordOf(256, 0, 0, 1)) << "the carry to 512 kept its exponent";
            EXPECT_EQ(packed.mColours[1], osg::Vec3f(1.0f, 0.0f, 0.0f));

            EXPECT_EQ(packed.mWords[2], wordOf(256, 0, 0, 1));
            EXPECT_EQ(packed.mColours[2], osg::Vec3f(1.0f, 0.0f, 0.0f));

            EXPECT_EQ(packed.mWords[3], wordOf(0, 511, 0, 16));
            EXPECT_EQ(packed.mColours[3], osg::Vec3f(0.0f, 65408.0f, 0.0f));

            EXPECT_EQ(packed.mWords[4], wordOf(1, 0, 0, -15));
            EXPECT_EQ(packed.mColours[4], osg::Vec3f(std::ldexp(1.0f, -24), 0.0f, 0.0f));
        }
    }
}
