#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/present/swapchain.hpp>

namespace Rtx
{
    namespace
    {
        bool operator==(const VkSurfaceFormatKHR& left, const VkSurfaceFormatKHR& right)
        {
            return left.format == right.format && left.colorSpace == right.colorSpace;
        }

        constexpr VkColorSpaceKHR sSrgb = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        constexpr VkColorSpaceKHR sHdr10 = VK_COLOR_SPACE_HDR10_ST2084_EXT;

        /// **A format is chosen with its colour space**: an unsigned-normalised one in sRGB, wherever
        /// the surface lists it, past the same format offered in HDR10 first, which would show the
        /// curve's image as something else; the first format where no such pair is; and a surface
        /// with none is refused.
        TEST(RtxSwapchainTest, aFormatIsChosenWithItsColourSpace)
        {
            const std::vector<VkSurfaceFormatKHR> hdrFirst{ { VK_FORMAT_B8G8R8A8_UNORM, sHdr10 },
                { VK_FORMAT_B8G8R8A8_SRGB, sSrgb }, { VK_FORMAT_R8G8B8A8_UNORM, sSrgb } };
            EXPECT_TRUE(chooseSurfaceFormat(hdrFirst) == (VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_UNORM, sSrgb }));

            const std::vector<VkSurfaceFormatKHR> both{ { VK_FORMAT_B8G8R8A8_UNORM, sSrgb },
                { VK_FORMAT_R8G8B8A8_UNORM, sSrgb } };
            EXPECT_TRUE(chooseSurfaceFormat(both) == both.front()) << "the surface's order decides between two";

            const std::vector<VkSurfaceFormatKHR> none{ { VK_FORMAT_A2B10G10R10_UNORM_PACK32, sHdr10 },
                { VK_FORMAT_B8G8R8A8_SRGB, sSrgb } };
            EXPECT_TRUE(chooseSurfaceFormat(none) == none.front());

            EXPECT_THROW(chooseSurfaceFormat({}), Unsupported);
        }

        /// **Each setting asks for its mode and falls back to FIFO**, which every surface has: off is
        /// mailbox, then immediate; adaptive is FIFO relaxed; on is FIFO whatever else is offered.
        TEST(RtxSwapchainTest, eachVerticalSyncAsksForItsModeAndFallsBackToFifo)
        {
            const struct
            {
                std::vector<VkPresentModeKHR> mOffered;
                VerticalSync mAsked;
                VkPresentModeKHR mChosen;
            } table[] = {
                { { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR },
                    VerticalSync::Off, VK_PRESENT_MODE_MAILBOX_KHR },
                { { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR }, VerticalSync::Off,
                    VK_PRESENT_MODE_IMMEDIATE_KHR },
                { { VK_PRESENT_MODE_FIFO_KHR }, VerticalSync::Off, VK_PRESENT_MODE_FIFO_KHR },
                { { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR }, VerticalSync::Adaptive,
                    VK_PRESENT_MODE_FIFO_RELAXED_KHR },
                { { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR }, VerticalSync::Adaptive,
                    VK_PRESENT_MODE_FIFO_KHR },
                { { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_FIFO_KHR },
                    VerticalSync::On, VK_PRESENT_MODE_FIFO_KHR },
            };
            for (const auto& row : table)
                EXPECT_EQ(presentModeFor(row.mOffered, row.mAsked), row.mChosen)
                    << static_cast<int>(row.mAsked) << " of " << row.mOffered.size();
        }
    }
}
