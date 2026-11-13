#include <utility>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxImageTest : Testing::DeviceTest
        {
        };

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
    }
}
