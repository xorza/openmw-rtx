#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/shaders/shared/bc7.h>
#include <components/rtxvulkan/texture/bc7encodepass.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sWidth = 8;
        constexpr std::uint32_t sHeight = 4;

        using Texel = std::array<std::uint8_t, 4>;

        /// One block as the format defines mode 6, decoded on the host from its sixteen bytes: the
        /// mode's seven bits, two end points of seven bits a channel, a lowest bit each, and an
        /// index a texel — three bits for the first, whose top bit is nought — read from the lowest
        /// bit up. The weights are the format's own table, written here and not taken from the
        /// encoder, so a table the encoder got wrong is caught.
        std::array<Texel, 16> decodeMode6(std::span<const std::byte, Shaders::BC7_BLOCK_BYTES> block)
        {
            constexpr std::array<std::uint32_t, 16> weights{ 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60,
                64 };
            std::uint32_t at = 0;
            const auto take = [&](std::uint32_t bits) {
                std::uint32_t value = 0;
                for (std::uint32_t bit = 0; bit < bits; ++bit, ++at)
                    value |= ((std::to_integer<std::uint32_t>(block[at / 8]) >> (at % 8)) & 1u) << bit;
                return value;
            };

            EXPECT_EQ(take(7), 1u << 6) << "not mode 6";
            std::array<std::uint32_t, 4> low{};
            std::array<std::uint32_t, 4> high{};
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                low[channel] = take(7);
                high[channel] = take(7);
            }
            const std::uint32_t lowBit = take(1);
            const std::uint32_t highBit = take(1);

            std::array<Texel, 16> texels{};
            for (std::size_t texel = 0; texel < 16; ++texel)
            {
                const std::uint32_t weight = weights[take(texel == 0 ? 3 : 4)];
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    const std::uint32_t from = low[channel] * 2 + lowBit;
                    const std::uint32_t to = high[channel] * 2 + highBit;
                    texels[texel][channel] = static_cast<std::uint8_t>(((64 - weight) * from + weight * to + 32) >> 6);
                }
            }
            EXPECT_EQ(at, 128u);
            return texels;
        }

        /// What one encode came to: the blocks it wrote, decoded on the host, and the image decoded
        /// by the device's sampler, each level by level, each level's texels row by row.
        struct Encoded
        {
            std::vector<Texel> mHost;
            std::vector<Texel> mDevice;
        };

        struct RtxBc7EncodePassTest : Testing::DeviceTest
        {
            /// Encodes `texels`, a chain of `levels` from `sWidth` by `sHeight` laid level after level,
            /// and decodes it both ways.
            Encoded encode(std::span<const Texel> texels, std::uint32_t levels, bool weighsAlpha)
            {
                Device& device = getDevice();
                const Bc7EncodePass pass(device);

                const Image source(device, sWidth, sHeight, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "bc7 source", levels);
                const Image target(device, sWidth, sHeight, VK_FORMAT_BC7_UNORM_BLOCK,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    "bc7 target", levels);
                const Image decoded(device, sWidth, sHeight, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    "bc7 decoded", levels);

                const Bc7Chain chain = Bc7Chain::of(sWidth, sHeight, levels);
                const Buffer blocks = Buffer::deviceLocal(device, chain.mBytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "bc7 blocks");
                const Buffer landing
                    = Buffer::readBack(device, chain.mBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "bc7 blocks back");
                const Buffer staging
                    = Buffer::staging(device, texels.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "bc7 texels");
                staging.write(texels);

                std::vector<VkBufferImageCopy> regions;
                std::vector<VkImageBlit> blits;
                VkDeviceSize offset = 0;
                for (std::uint32_t level = 0; level < levels; ++level)
                {
                    const Bc7Chain::Level& shape = chain.mLevels[level];
                    regions.push_back(wholeLevel(offset, level, VkExtent3D{ shape.mWidth, shape.mHeight, 1 }));
                    offset += VkDeviceSize{ shape.mWidth } * shape.mHeight * sizeof(Texel);
                    const VkOffset3D end{ static_cast<std::int32_t>(shape.mWidth),
                        static_cast<std::int32_t>(shape.mHeight), 1 };
                    blits.push_back(VkImageBlit{
                        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                        .srcOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                        .dstOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                    });
                }
                EXPECT_EQ(offset, texels.size_bytes()) << "texels for another chain";

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    source.transition(commands, Use::sUndefined, Use::sCopyWrite);
                    vkCmdCopyBufferToImage(commands, staging.getHandle(), source.getHandle(),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, regions.data());
                    source.transition(commands, Use::sCopyWrite, Use::sComputeRead);

                    pass.record(commands, source, blocks, target, weighsAlpha);
                    blocks.copyTo(commands, landing, chain.mBytes);

                    // The sampler's own decode, texel for texel: a blit of each level at its size,
                    // nearest.
                    target.transition(commands, Use::sTextureSample, Use::sBlitRead);
                    decoded.transition(commands, Use::sUndefined, Use::sBlitWrite);
                    vkCmdBlitImage(commands, target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        decoded.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, blits.data(),
                        VK_FILTER_NEAREST);
                    decoded.transition(commands, Use::sBlitWrite, Use::sCopyRead);
                });

                Encoded encoded;
                const auto* const bytes = static_cast<const std::byte*>(landing.map());
                for (std::uint32_t level = 0; level < levels; ++level)
                {
                    const Bc7Chain::Level& shape = chain.mLevels[level];
                    const std::size_t start = encoded.mHost.size();
                    encoded.mHost.resize(start + std::size_t{ shape.mWidth } * shape.mHeight);
                    const std::uint32_t across = (shape.mWidth + 3) / 4;
                    for (std::uint32_t block = 0; block < shape.mBlocks; ++block)
                    {
                        const std::array<Texel, 16> texelsOf
                            = decodeMode6(std::span<const std::byte, Shaders::BC7_BLOCK_BYTES>(
                                bytes + shape.mOffset + block * Shaders::BC7_BLOCK_BYTES, Shaders::BC7_BLOCK_BYTES));
                        for (std::uint32_t texel = 0; texel < 16; ++texel)
                        {
                            const std::uint32_t x = block % across * 4 + texel % 4;
                            const std::uint32_t y = block / across * 4 + texel / 4;
                            if (x < shape.mWidth && y < shape.mHeight)
                                encoded.mHost[start + y * shape.mWidth + x] = texelsOf[texel];
                        }
                    }

                    std::vector<std::uint8_t> pixels;
                    decoded.read(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixels, level);
                    encoded.mDevice.resize(encoded.mHost.size());
                    std::memcpy(encoded.mDevice.data() + start, pixels.data(), pixels.size());
                }
                return encoded;
            }
        };

        /// Two blocks side by side: one colour, and a ramp across each row.
        std::vector<Texel> twoBlocks(const Texel& colour)
        {
            constexpr std::array<std::uint8_t, 4> ramp{ 0, 85, 170, 255 };
            std::vector<Texel> texels(std::size_t{ sWidth } * sHeight);
            for (std::uint32_t y = 0; y < sHeight; ++y)
                for (std::uint32_t x = 0; x < sWidth; ++x)
                    texels[y * sWidth + x]
                        = x < 4 ? colour : Texel{ ramp[x - 4], ramp[x - 4], ramp[x - 4], std::uint8_t{ 255 } };
            return texels;
        }

        /// **The blocks the encoder writes are mode 6 as the format defines it, and the sampler
        /// decodes them to what the host's decoder does**, texel for texel, which says the bits are
        /// laid where the format puts them.
        ///
        /// **One colour comes back exactly.** (137, 225, 0) mixes odd and even channels, which two
        /// equal end points sharing a lowest bit cannot hold; the end points either side of each
        /// channel reach it at the index whose weight is 30: `(34·136 + 30·138 + 32) >> 6 = 137`,
        /// `(34·224 + 30·226 + 32) >> 6 = 225`, and nought from two noughts. The alpha, which this
        /// image's reader does not read, gives way.
        ///
        /// **A ramp of 0, 85, 170 and 255 across each row comes back as 0, 84, 171 and 255**: end
        /// points at nought and 255, and the levels nearest the middle two at weights 21 and 43,
        /// `(43·0 + 21·255 + 32) >> 6 = 84` and `(21·0 + 43·255 + 32) >> 6 = 171`, a byte off each.
        ///
        /// **And where the alpha is read, it counts**: an opaque even colour, (136, 224, 0, 254),
        /// one lowest bit of nought under all four channels, comes back exactly, alpha and all.
        TEST_F(RtxBc7EncodePassTest, aBlockIsModeSixAndOneColourComesBackExactly)
        {
            const Encoded opaque = encode(twoBlocks(Texel{ 137, 225, 0, 255 }), 1, false);
            ASSERT_EQ(opaque.mHost.size(), opaque.mDevice.size());
            for (std::size_t texel = 0; texel < opaque.mHost.size(); ++texel)
                EXPECT_EQ(opaque.mHost[texel], opaque.mDevice[texel]) << "texel " << texel;

            constexpr std::array<std::uint8_t, 4> ramped{ 0, 84, 171, 255 };
            constexpr Texel colour{ 137, 225, 0, 255 };
            for (std::uint32_t y = 0; y < sHeight; ++y)
                for (std::uint32_t x = 0; x < sWidth; ++x)
                {
                    const Texel& got = opaque.mDevice[y * sWidth + x];
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const std::uint8_t owed = x < 4 ? colour[channel] : ramped[x - 4];
                        EXPECT_EQ(got[channel], owed) << "texel " << x << ", " << y << ", channel " << channel;
                    }
                }

            const Encoded weighed = encode(twoBlocks(Texel{ 136, 224, 0, 254 }), 1, true);
            for (std::size_t texel = 0; texel < weighed.mHost.size(); ++texel)
                EXPECT_EQ(weighed.mHost[texel], weighed.mDevice[texel]) << "texel " << texel;
            for (std::uint32_t y = 0; y < sHeight; ++y)
                for (std::uint32_t x = 0; x < 4; ++x)
                    EXPECT_EQ(weighed.mDevice[y * sWidth + x], (Texel{ 136, 224, 0, 254 }))
                        << "texel " << x << ", " << y;
        }
    }
}

namespace Rtx
{
    namespace
    {
        /// **Every level of a chain is encoded in its own place, in the one dispatch**: a chain from 8
        /// by 4 to one texel, each level one colour of its own, comes back each level exactly, the
        /// last three each a block that only part of stands inside its level. Each colour is reached
        /// by the single colour (`BC7_SINGLE_INDEX`): under lowest bits of nought, an even byte by
        /// two equal end points and an odd one by the two either side, `(34·30 + 30·32 + 32) >> 6 =
        /// 31`; and 255 beside 128 and 1 under a lowest bit of one below and nought above, `(34·255
        /// + 30·254 + 32) >> 6 = 255`, `(34·127 + 30·130 + 32) >> 6 = 128`, `(34·1 + 30·2 + 32) >> 6
        /// = 1`. The alpha is not read.
        TEST_F(RtxBc7EncodePassTest, everyLevelOfAChainIsEncodedInItsOwnPlace)
        {
            constexpr std::array<Texel, 4> colours{ Texel{ 137, 225, 0, 255 }, Texel{ 10, 20, 31, 255 },
                Texel{ 200, 3, 77, 255 }, Texel{ 255, 128, 1, 255 } };
            std::vector<Texel> texels;
            const Bc7Chain chain = Bc7Chain::of(sWidth, sHeight, 4);
            for (std::uint32_t level = 0; level < 4; ++level)
                texels.insert(texels.end(), std::size_t{ chain.mLevels[level].mWidth } * chain.mLevels[level].mHeight,
                    colours[level]);

            const Encoded encoded = encode(texels, 4, false);
            ASSERT_EQ(encoded.mHost.size(), texels.size());
            std::size_t at = 0;
            for (std::uint32_t level = 0; level < 4; ++level)
                for (std::uint32_t texel = 0; texel < chain.mLevels[level].mWidth * chain.mLevels[level].mHeight;
                     ++texel, ++at)
                {
                    EXPECT_EQ(encoded.mHost[at], encoded.mDevice[at]) << "level " << level << ", texel " << texel;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        EXPECT_EQ(encoded.mDevice[at][channel], colours[level][channel])
                            << "level " << level << ", texel " << texel << ", channel " << channel;
                }
        }
    }
}
