#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/texture/mipchainpass.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxMipChainPassTest : Testing::DeviceTest
        {
            /// Writes `texels` into the first level of each of `images` and builds their chains in one record, as a
            /// pass that wrote the first level by a dispatch builds them.
            void build(std::span<const std::pair<const Image*, std::span<const std::byte>>> images)
            {
                const MipChainPass mips(getDevice());
                std::vector<const Image*> chains;
                Batch batch(getPool());
                const VkCommandBuffer commands = batch.getCommands();
                for (const auto& [image, texels] : images)
                {
                    const StagingRun run = batch.stage(texels);
                    image->transition(commands, Use::sUndefined, Use::sCopyWrite);
                    const VkBufferImageCopy region{
                        .bufferOffset = run.mOffset,
                        .bufferRowLength = 0,
                        .bufferImageHeight = 0,
                        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                        .imageOffset = {},
                        .imageExtent = { image->getWidth(), image->getHeight(), 1 },
                    };
                    vkCmdCopyBufferToImage(
                        commands, run.mBuffer, image->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                    image->transition(commands, Use::sCopyWrite, Use::sComputeWrite);
                    chains.push_back(image);
                }
                mips.record(commands, chains);
                batch.flush();
            }

            /// `level` of `image`, its bytes row by row.
            static std::vector<std::uint8_t> read(const Image& image, std::uint32_t level)
            {
                std::vector<std::uint8_t> bytes;
                image.read(VK_IMAGE_LAYOUT_GENERAL, bytes, level);
                return bytes;
            }

            /// Channel `channel` of every texel of `level` of a chain of four halves a texel.
            static std::vector<float> halvesAt(const Image& image, std::uint32_t level, std::size_t channel)
            {
                const std::vector<std::uint8_t> bytes = read(image, level);
                std::vector<float> values(bytes.size() / 8);
                for (std::size_t at = 0; at < values.size(); ++at)
                {
                    std::uint16_t half = 0;
                    std::memcpy(&half, bytes.data() + at * 8 + channel * 2, sizeof(half));
                    values[at] = fromHalf(half);
                }
                return values;
            }

            static ImageDescription chainOf(std::uint32_t side, VkFormat format, VkFormat storage = VK_FORMAT_UNDEFINED)
            {
                return ImageDescription{ .mWidth = side,
                    .mHeight = side,
                    .mFormat = format,
                    .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    .mMipLevels = static_cast<std::uint32_t>(std::bit_width(side)),
                    .mStorageFormat = storage };
            }
        };

        /// **Each level is the box mean of the one above it, and chains of different depths are built side by
        /// side.** A 256-texel chain holds `x` in red and `y` in green at its first level, so level `k` holds the mean
        /// of a run of `2^k` whole numbers at texel `(i, j)`: `2^k i + (2^k - 1) / 2` and `2^k j + (2^k - 1) / 2`,
        /// every one of them a half exactly. Four workgroups a side, so its last two levels are the last workgroup's,
        /// from what the others stored. A 128-texel chain beside it holds the two the other way round, and its last
        /// level is its own last workgroup's as well, at a counter of its own, as the sea's two cascades are. A 4×4
        /// chain holds `2(x + 4y)`: its second level `4i + 16j + 5`, its last 15. A chain of one level is left as it
        /// was.
        TEST_F(RtxMipChainPassTest, eachLevelIsTheBoxMeanOfTheOneAboveIt)
        {
            const Device& device = getDevice();
            const Image large(MemoryUse::Essential, device, chainOf(256, VK_FORMAT_R16G16B16A16_SFLOAT), "large chain");
            const Image swapped(
                MemoryUse::Essential, device, chainOf(128, VK_FORMAT_R16G16B16A16_SFLOAT), "swapped chain");
            const Image square(MemoryUse::Essential, device, chainOf(4, VK_FORMAT_R16G16B16A16_SFLOAT), "square chain");
            const Image single(MemoryUse::Essential, device,
                ImageDescription{ .mWidth = 1,
                    .mHeight = 1,
                    .mFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .mUsage
                    = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT },
                "chain of one");
            ASSERT_EQ(large.getMipLevels(), 9u);

            // Four halves a texel.
            const auto texels = [](std::uint32_t side, auto red, auto green) {
                std::vector<std::uint16_t> halves(std::size_t{ side } * side * 4, 0);
                for (std::uint32_t y = 0; y < side; ++y)
                    for (std::uint32_t x = 0; x < side; ++x)
                    {
                        halves[(std::size_t{ y } * side + x) * 4] = toHalf(static_cast<float>(red(x, y)));
                        halves[(std::size_t{ y } * side + x) * 4 + 1] = toHalf(static_cast<float>(green(x, y)));
                    }
                return halves;
            };
            const std::vector<std::uint16_t> largeTexels = texels(
                256, [](std::uint32_t x, std::uint32_t) { return x; },
                [](std::uint32_t, std::uint32_t y) { return y; });
            const std::vector<std::uint16_t> swappedTexels = texels(
                128, [](std::uint32_t, std::uint32_t y) { return y; },
                [](std::uint32_t x, std::uint32_t) { return x; });
            const std::vector<std::uint16_t> squareTexels = texels(
                4, [](std::uint32_t x, std::uint32_t y) { return 2 * (x + 4 * y); },
                [](std::uint32_t, std::uint32_t) { return 0; });
            const std::vector<std::uint16_t> singleTexels{ toHalf(3.0f), 0, 0, 0 };
            build(std::vector<std::pair<const Image*, std::span<const std::byte>>>{
                { &large, std::as_bytes(std::span(largeTexels)) },
                { &swapped, std::as_bytes(std::span(swappedTexels)) },
                { &square, std::as_bytes(std::span(squareTexels)) },
                { &single, std::as_bytes(std::span(singleTexels)) } });

            // `across` is the channel holding `x` at the first level, the other `y`.
            const auto expectMeans = [](const Image& image, std::size_t across, const char* name) {
                for (std::uint32_t level = 1; level < image.getMipLevels(); ++level)
                {
                    const std::uint32_t side = image.getWidth() >> level;
                    const float run = static_cast<float>(1u << level);
                    const std::vector<float> xs = halvesAt(image, level, across);
                    const std::vector<float> ys = halvesAt(image, level, 1 - across);
                    ASSERT_EQ(xs.size(), std::size_t{ side } * side) << name << " level " << level;
                    std::size_t wrong = 0;
                    for (std::uint32_t j = 0; j < side; ++j)
                        for (std::uint32_t i = 0; i < side; ++i)
                        {
                            const std::size_t at = std::size_t{ j } * side + i;
                            wrong += xs[at] != run * static_cast<float>(i) + (run - 1.0f) / 2.0f ? 1 : 0;
                            wrong += ys[at] != run * static_cast<float>(j) + (run - 1.0f) / 2.0f ? 1 : 0;
                        }
                    EXPECT_EQ(wrong, 0u) << name << " level " << level << ", whose first texel holds " << xs[0] << ", "
                                         << ys[0];
                }
            };
            ASSERT_EQ(swapped.getMipLevels(), 8u);
            expectMeans(large, 0, "large");
            expectMeans(swapped, 1, "swapped");

            EXPECT_EQ(halvesAt(square, 1, 0), (std::vector<float>{ 5.0f, 9.0f, 21.0f, 25.0f }));
            EXPECT_EQ(halvesAt(square, 2, 0), (std::vector<float>{ 15.0f }));
            EXPECT_EQ(halvesAt(single, 0, 0), (std::vector<float>{ 3.0f }))
                << "a chain of one level, ordered against the read as every chain is";
        }

        /// **A display-encoded chain is averaged in light**, as a blit through its encoded format filters, and one
        /// whose bytes are linear is averaged as bytes. Red at 0 and 254 in alternate columns: as bytes the mean is
        /// 127; in light it is half of `decodeSrgb(254 / 255)`, encoded back, which is brighter. Green at 128
        /// everywhere stands where it was through the curve and back, and alpha, which carries no curve, at 0 and 254
        /// is 127 either way.
        TEST_F(RtxMipChainPassTest, anEncodedChainIsAveragedInLight)
        {
            const Device& device = getDevice();
            const Image encoded(MemoryUse::Essential, device,
                chainOf(2, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM), "encoded chain");
            const Image linear(MemoryUse::Essential, device, chainOf(2, VK_FORMAT_R8G8B8A8_UNORM), "linear chain");

            const std::vector<std::uint8_t> texels{ 0, 128, 0, 0, 254, 128, 0, 254, 0, 128, 0, 0, 254, 128, 0, 254 };
            build(std::vector<std::pair<const Image*, std::span<const std::byte>>>{
                { &encoded, std::as_bytes(std::span(texels)) }, { &linear, std::as_bytes(std::span(texels)) } });

            const float light = 0.5f * (Shaders::decodeSrgb(0.0f) + Shaders::decodeSrgb(254.0f / 255.0f));
            const auto inLight = static_cast<std::uint8_t>(std::lround(Shaders::encodeSrgb(light) * 255.0f));
            ASSERT_GT(inLight, 127u) << "the curve leaves the mean where the bytes' is, so this proves nothing";

            EXPECT_EQ(read(encoded, 1), (std::vector<std::uint8_t>{ inLight, 128, 0, 127 }));
            EXPECT_EQ(read(linear, 1), (std::vector<std::uint8_t>{ 127, 128, 0, 127 }));
        }
    }
}
