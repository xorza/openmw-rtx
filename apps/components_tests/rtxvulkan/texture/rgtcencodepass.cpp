#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <components/rtxvulkan/shaders/shared/rgtc.h>
#include <components/rtxvulkan/texture/blockchain.hpp>
#include <components/rtxvulkan/texture/rgtcencodepass.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sWidth = 8;
        constexpr std::uint32_t sHeight = 4;

        using Texel = std::array<std::uint8_t, 4>;

        /// One channel's block as the format defines BC4, read on the host: two end points of a byte,
        /// then an index of three bits a texel from the lowest bit up.
        struct ChannelBlock
        {
            std::uint32_t mFirst = 0;
            std::uint32_t mSecond = 0;
            std::array<std::uint32_t, 16> mIndices{};
        };

        ChannelBlock readChannel(std::span<const std::byte, Shaders::RGTC_CHANNEL_BYTES> bytes)
        {
            std::uint64_t bits = 0;
            for (std::size_t at = 0; at < bytes.size(); ++at)
                bits |= std::uint64_t{ std::to_integer<std::uint8_t>(bytes[at]) } << (8 * at);
            ChannelBlock block{ .mFirst = static_cast<std::uint32_t>(bits & 0xff),
                .mSecond = static_cast<std::uint32_t>((bits >> 8) & 0xff) };
            for (std::size_t texel = 0; texel < 16; ++texel)
                block.mIndices[texel] = static_cast<std::uint32_t>((bits >> (16 + 3 * texel)) & 7);
            return block;
        }

        /// The value index `index` of `block` names, in sevenths of a byte: the format's eight values
        /// where the first end point is the larger, written here and not taken from the encoder.
        std::uint32_t sevenths(const ChannelBlock& block, std::uint32_t index)
        {
            EXPECT_GT(block.mFirst, block.mSecond) << "not the eight-value mode";
            if (index == 0)
                return 7 * block.mFirst;
            if (index == 1)
                return 7 * block.mSecond;
            return (8 - index) * block.mFirst + (index - 1) * block.mSecond;
        }

        struct RtxRgtcEncodePassTest : Testing::DeviceTest
        {
            /// Encodes `texels`, one level of `sWidth` by `sHeight`, into `format`, and gives back the
            /// blocks and the image as the device's sampler decodes it.
            struct Encoded
            {
                std::vector<std::byte> mBlocks;
                std::vector<Texel> mDevice;
            };

            Encoded encode(std::span<const Texel> texels, VkFormat format)
            {
                Device& device = getDevice();
                const RgtcEncodePass pass(device);
                const Image source(MemoryUse::Essential, device,
                    ImageDescription{ .mWidth = sWidth,
                        .mHeight = sHeight,
                        .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                        .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT },
                    "rgtc source");
                const Image target(MemoryUse::Essential, device,
                    ImageDescription{ .mWidth = sWidth,
                        .mHeight = sHeight,
                        .mFormat = format,
                        .mUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                            | VK_IMAGE_USAGE_SAMPLED_BIT },
                    "rgtc target");
                const Image decoded(MemoryUse::Essential, device,
                    ImageDescription{ .mWidth = sWidth,
                        .mHeight = sHeight,
                        .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                        .mUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                            | VK_IMAGE_USAGE_SAMPLED_BIT },
                    "rgtc decoded");

                const std::uint32_t channels = format == VK_FORMAT_BC5_UNORM_BLOCK ? 2 : 1;
                const BlockChain chain = BlockChain::of(sWidth, sHeight, 1, channels * Shaders::RGTC_CHANNEL_BYTES);
                const Buffer blocks = Buffer::deviceLocal(device, chain.mBytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "rgtc blocks");
                const Buffer landing
                    = Buffer::readBack(device, chain.mBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "rgtc blocks back");
                const Buffer staging
                    = Buffer::staging(device, texels.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "rgtc texels");
                staging.write(texels);

                const VkOffset3D end{ static_cast<std::int32_t>(sWidth), static_cast<std::int32_t>(sHeight), 1 };
                const VkImageBlit blit{
                    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .srcOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .dstOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                };
                const VkBufferImageCopy region = wholeLevel(0, 0, VkExtent3D{ sWidth, sHeight, 1 });
                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    source.transition(commands, Use::sUndefined, Use::sCopyWrite);
                    vkCmdCopyBufferToImage(commands, staging.getHandle(), source.getHandle(),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    source.transition(commands, Use::sCopyWrite, Use::sComputeRead);

                    pass.record(commands, source, blocks, target);
                    blocks.copyTo(commands, landing, chain.mBytes);

                    target.transition(commands, Use::sTextureSample, Use::sBlitRead);
                    decoded.transition(commands, Use::sUndefined, Use::sBlitWrite);
                    vkCmdBlitImage(commands, target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        decoded.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
                    decoded.transition(commands, Use::sBlitWrite, Use::sCopyRead);
                });

                Encoded encoded;
                const auto* const bytes = static_cast<const std::byte*>(landing.map());
                encoded.mBlocks.assign(bytes, bytes + chain.mBytes);
                std::vector<std::uint8_t> pixels;
                decoded.read(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixels);
                encoded.mDevice.resize(std::size_t{ sWidth } * sHeight);
                std::memcpy(encoded.mDevice.data(), pixels.data(), pixels.size());
                return encoded;
            }
        };

        /// Two blocks side by side, each texel's red and green from a rule with no pattern a block
        /// could fit by luck, blue nought and alpha one as a file of two channels decodes.
        std::vector<Texel> twoChannels()
        {
            std::vector<Texel> texels(std::size_t{ sWidth } * sHeight);
            for (std::uint32_t y = 0; y < sHeight; ++y)
                for (std::uint32_t x = 0; x < sWidth; ++x)
                    texels[y * sWidth + x] = Texel{ static_cast<std::uint8_t>((x * 37 + y * 11 + 18) % 256),
                        static_cast<std::uint8_t>((x * 13 + y * 59 + 7) % 256), 0, 255 };
            return texels;
        }

        /// **Each channel is a BC4 block between its least and its most, each texel at the value
        /// nearest it**: the blocks read back on the host have the block's most as their first end
        /// point and its least as the second, which names the eight values, and no texel's index
        /// names a value farther from it than another of the eight — in sevenths, exactly. BC5 is
        /// red's block then green's; BC4 is red's alone, the same block. The sampler reads every
        /// blue as nought and every alpha as one, and an end point exactly.
        TEST_F(RtxRgtcEncodePassTest, eachChannelIsABlockBetweenItsLeastAndMostEachTexelAtItsNearest)
        {
            const std::vector<Texel> texels = twoChannels();
            const Encoded bc5 = encode(texels, VK_FORMAT_BC5_UNORM_BLOCK);
            const Encoded bc4 = encode(texels, VK_FORMAT_BC4_UNORM_BLOCK);
            ASSERT_EQ(bc5.mBlocks.size(), 2 * 2 * Shaders::RGTC_CHANNEL_BYTES);
            ASSERT_EQ(bc4.mBlocks.size(), 2 * Shaders::RGTC_CHANNEL_BYTES);

            for (std::uint32_t blockX = 0; blockX < 2; ++blockX)
                for (std::uint32_t channel = 0; channel < 2; ++channel)
                {
                    const ChannelBlock block = readChannel(std::span<const std::byte, Shaders::RGTC_CHANNEL_BYTES>(
                        bc5.mBlocks.data() + (blockX * 2 + channel) * Shaders::RGTC_CHANNEL_BYTES,
                        Shaders::RGTC_CHANNEL_BYTES));
                    std::uint32_t least = 255;
                    std::uint32_t most = 0;
                    for (std::uint32_t texel = 0; texel < 16; ++texel)
                    {
                        const std::uint32_t value = texels[(texel / 4) * sWidth + blockX * 4 + texel % 4][channel];
                        least = std::min(least, value);
                        most = std::max(most, value);
                    }
                    EXPECT_EQ(block.mFirst, most) << "block " << blockX << ", channel " << channel;
                    EXPECT_EQ(block.mSecond, least) << "block " << blockX << ", channel " << channel;

                    for (std::uint32_t texel = 0; texel < 16; ++texel)
                    {
                        const std::uint32_t target = 7 * texels[(texel / 4) * sWidth + blockX * 4 + texel % 4][channel];
                        const auto apart = [&](std::uint32_t index) {
                            return std::abs(static_cast<int>(sevenths(block, index)) - static_cast<int>(target));
                        };
                        for (std::uint32_t other = 0; other < 8; ++other)
                            EXPECT_LE(apart(block.mIndices[texel]), apart(other))
                                << "block " << blockX << ", channel " << channel << ", texel " << texel;
                    }

                    if (channel == 0)
                    {
                        EXPECT_EQ(std::memcmp(bc4.mBlocks.data() + blockX * Shaders::RGTC_CHANNEL_BYTES,
                                      bc5.mBlocks.data() + blockX * 2 * Shaders::RGTC_CHANNEL_BYTES,
                                      Shaders::RGTC_CHANNEL_BYTES),
                            0)
                            << "BC4 is BC5's red";
                    }
                }

            for (std::size_t texel = 0; texel < texels.size(); ++texel)
            {
                EXPECT_EQ(bc5.mDevice[texel][2], 0) << "texel " << texel;
                EXPECT_EQ(bc5.mDevice[texel][3], 255) << "texel " << texel;
                for (std::uint32_t channel = 0; channel < 2; ++channel)
                {
                    const std::uint8_t value = texels[texel][channel];
                    const std::uint32_t blockX = (texel % sWidth) / 4;
                    std::uint8_t least = 255;
                    std::uint8_t most = 0;
                    for (std::uint32_t other = 0; other < 16; ++other)
                    {
                        const std::uint8_t at = texels[(other / 4) * sWidth + blockX * 4 + other % 4][channel];
                        least = std::min(least, at);
                        most = std::max(most, at);
                    }
                    if (value == least || value == most)
                    {
                        EXPECT_EQ(bc5.mDevice[texel][channel], value) << "an end point, texel " << texel;
                    }
                }
            }
        }
    }
}
