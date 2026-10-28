#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxImageTest : Testing::DeviceTest
        {
        };

        /// A whole number below 2048 as a half's bits, which holds it exactly: the exponent is its
        /// highest bit, and the ten bits below it the mantissa.
        std::uint16_t halfOfWhole(const std::uint32_t whole)
        {
            if (whole == 0)
                return 0;
            const int exponent = std::bit_width(whole) - 1;
            const std::uint32_t mantissa = ((whole << 10) >> exponent) & 0x3FFu;
            return static_cast<std::uint16_t>((static_cast<std::uint32_t>(exponent + 15) << 10) | mantissa);
        }

        /// **An image moved out of takes everything it is with it, and the husk is a default-made
        /// image**, by construction and by assignment alike, as a buffer's is: a husk that still
        /// answered its width and format read as an image that was there.
        TEST_F(RtxImageTest, aMovedImageLeavesADefaultMadeHusk)
        {
            const Device& device = getDevice();
            const ImageDescription described{ .mWidth = 8,
                .mHeight = 4,
                .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .mUsage = VK_IMAGE_USAGE_SAMPLED_BIT,
                .mMipLevels = 2 };

            Image first(MemoryUse::Essential, device, described, "moved");
            const VkImage handle = first.getHandle();

            const auto expectHusk = [](const Image& husk, const char* how) {
                EXPECT_TRUE(husk.isEmpty()) << how;
                EXPECT_EQ(husk.getWidth(), 0u) << how;
                EXPECT_EQ(husk.getHeight(), 0u) << how;
                EXPECT_EQ(husk.getFormat(), VK_FORMAT_UNDEFINED) << how;
                EXPECT_EQ(husk.getUsage(), VkImageUsageFlags{ 0 }) << how;
                EXPECT_EQ(husk.getMipLevels(), 1u) << how;
            };

            Image second = std::move(first);
            EXPECT_EQ(second.getHandle(), handle);
            EXPECT_EQ(second.getWidth(), 8u);
            EXPECT_EQ(second.getHeight(), 4u);
            EXPECT_EQ(second.getFormat(), VK_FORMAT_R8G8B8A8_UNORM);
            EXPECT_EQ(second.getMipLevels(), 2u);
            expectHusk(first, "moved by construction");

            Image third;
            third = std::move(second);
            EXPECT_EQ(third.getHandle(), handle);
            EXPECT_EQ(third.getWidth(), 8u);
            expectHusk(second, "moved by assignment");
        }

        /// **Each level is the box mean of the one above it, and chains of different depths step
        /// together.** An 8×4 chain of four levels holds `x + 8y` at its first: its second holds
        /// the mean of each two by two, `2i + 16j + (0 + 1 + 8 + 9) / 4 = 2i + 16j + 4.5`; its third
        /// the mean of those, `4k + 13.5` (13.5, 17.5); its last the mean of all 32, 15.5. A 4×4
        /// chain of three levels holds `2(x + 4y)`: its second `4i + 16j + 5` (5, 9, 21, 25), and
        /// its last 15, the mean of every even number to 30. Every figure is a half exactly, and a
        /// blit that halves samples halfway between two texels, so the filter's weights are exact.
        TEST_F(RtxImageTest, eachLevelOfAChainIsTheBoxMeanOfTheOneAboveIt)
        {
            const Device& device = getDevice();
            const auto described = [](std::uint32_t width, std::uint32_t height, std::uint32_t levels) {
                return ImageDescription{ .mWidth = width,
                    .mHeight = height,
                    .mFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .mUsage
                    = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .mMipLevels = levels };
            };
            const Image wide(MemoryUse::Essential, device, described(8, 4, 4), "wide chain");
            const Image square(MemoryUse::Essential, device, described(4, 4, 3), "square chain");

            // Four halves a texel, red alone carrying the figure.
            const auto texels = [](std::uint32_t width, std::uint32_t height, auto figure) {
                std::vector<std::uint16_t> halves(std::size_t{ width } * height * 4, 0);
                for (std::uint32_t y = 0; y < height; ++y)
                    for (std::uint32_t x = 0; x < width; ++x)
                        halves[(std::size_t{ y } * width + x) * 4] = halfOfWhole(figure(x, y));
                return halves;
            };
            const std::vector<std::uint16_t> wideTexels
                = texels(8, 4, [](std::uint32_t x, std::uint32_t y) { return x + 8 * y; });
            const std::vector<std::uint16_t> squareTexels
                = texels(4, 4, [](std::uint32_t x, std::uint32_t y) { return 2 * (x + 4 * y); });

            Batch batch(getPool());
            const VkCommandBuffer commands = batch.getCommands();
            for (const auto& [image, halves] : { std::pair{ &wide, &wideTexels }, std::pair{ &square, &squareTexels } })
            {
                const StagingRun run = batch.stage(std::as_bytes(std::span(*halves)));
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
            }
            Image::buildMips(commands, std::array<const Image*, 2>{ &wide, &square });
            batch.flush();

            // Red of each texel of `level`, row by row.
            const auto redAt = [](const Image& image, std::uint32_t level) {
                std::vector<std::uint8_t> bytes;
                image.read(VK_IMAGE_LAYOUT_GENERAL, bytes, level);
                std::vector<float> red(bytes.size() / 8);
                for (std::size_t at = 0; at < red.size(); ++at)
                {
                    std::uint16_t half = 0;
                    std::memcpy(&half, bytes.data() + at * 8, sizeof(half));
                    red[at] = fromHalf(half);
                }
                return red;
            };

            EXPECT_EQ(redAt(wide, 1), (std::vector<float>{ 4.5f, 6.5f, 8.5f, 10.5f, 20.5f, 22.5f, 24.5f, 26.5f }));
            EXPECT_EQ(redAt(wide, 2), (std::vector<float>{ 13.5f, 17.5f }));
            EXPECT_EQ(redAt(wide, 3), (std::vector<float>{ 15.5f }));
            EXPECT_EQ(redAt(square, 1), (std::vector<float>{ 5.0f, 9.0f, 21.0f, 25.0f }));
            EXPECT_EQ(redAt(square, 2), (std::vector<float>{ 15.0f }));
        }
    }
}
