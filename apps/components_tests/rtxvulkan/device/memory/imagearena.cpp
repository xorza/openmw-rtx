#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/imagearena.hpp>

namespace Rtx
{
    namespace
    {
        template <std::size_t Count>
        std::array<VkDeviceSize, Count> layOut(const std::array<ArenaNeed, Count>& needs, VkDeviceSize& covered)
        {
            std::array<VkDeviceSize, Count> offsets{};
            covered = ImageArena::layOut(needs, offsets);
            return offsets;
        }

        /// **Images in use at once stand side by side, each on its alignment**, the largest first:
        /// 300 at nought; 100 past it, at 300 rounded up to 64, 320; and 50 neither at nought nor at
        /// 300 rounded to 16, 304, which runs into the 100 at 320, so at 420 rounded to 16, 432. The
        /// placement covers 432 + 50 = 482 bytes.
        TEST(RtxImageArenaTest, imagesInUseAtOnceStandSideBySideEachOnItsAlignment)
        {
            const ArenaSpan once{ .mFirst = 0, .mLast = 0 };
            VkDeviceSize covered = 0;
            const auto offsets = layOut(std::array{ ArenaNeed{ .mSize = 100, .mAlignment = 64, .mSpan = once },
                                            ArenaNeed{ .mSize = 300, .mAlignment = 256, .mSpan = once },
                                            ArenaNeed{ .mSize = 50, .mAlignment = 16, .mSpan = once } },
                covered);

            EXPECT_EQ(offsets, (std::array<VkDeviceSize, 3>{ 320, 0, 432 }));
            EXPECT_EQ(covered, 482u);
        }

        /// **Images whose spans do not meet share memory**, and an arena covers the largest set in use
        /// at once: two stage images of 400 side by side (800), three denoiser images of 300 laid over
        /// them (900), and an upscaler image of 500 over both. 900 bytes, where images of their own
        /// took 2 700.
        ///
        /// **Each image's alignment moves it**: the second denoiser image at 256 goes from 300 to 512,
        /// and the third, which fitted at 600, past it to 812, so the arena grows to 1 112.
        TEST(RtxImageArenaTest, imagesWhoseSpansDoNotMeetShareMemory)
        {
            const ArenaSpan trace{ .mFirst = 0, .mLast = 0 };
            const ArenaSpan denoise{ .mFirst = 1, .mLast = 2 };
            const ArenaSpan upscale{ .mFirst = 3, .mLast = 3 };
            const auto needs = [&](VkDeviceSize alignment) {
                return std::array{
                    ArenaNeed{ .mSize = 400, .mAlignment = 1, .mSpan = trace },
                    ArenaNeed{ .mSize = 400, .mAlignment = 1, .mSpan = trace },
                    ArenaNeed{ .mSize = 300, .mAlignment = 1, .mSpan = denoise },
                    ArenaNeed{ .mSize = 300, .mAlignment = alignment, .mSpan = denoise },
                    ArenaNeed{ .mSize = 300, .mAlignment = 1, .mSpan = denoise },
                    ArenaNeed{ .mSize = 500, .mAlignment = 1, .mSpan = upscale },
                };
            };

            VkDeviceSize covered = 0;
            EXPECT_EQ(layOut(needs(1), covered), (std::array<VkDeviceSize, 6>{ 0, 400, 0, 300, 600, 0 }));
            EXPECT_EQ(covered, 900u);

            EXPECT_EQ(layOut(needs(256), covered), (std::array<VkDeviceSize, 6>{ 0, 400, 0, 512, 812, 0 }));
            EXPECT_EQ(covered, 1112u);
        }

        /// **Spans meet where they share a step**, at either end, and not where one ends the step
        /// before the other begins.
        TEST(RtxImageArenaTest, spansMeetWhereTheyShareAStep)
        {
            const ArenaSpan denoise{ .mFirst = 1, .mLast = 2 };
            EXPECT_TRUE(denoise.meets(ArenaSpan{ .mFirst = 2, .mLast = 3 }));
            EXPECT_TRUE(denoise.meets(ArenaSpan{ .mFirst = 0, .mLast = 1 }));
            EXPECT_TRUE(denoise.meets(ArenaSpan{ .mFirst = 0, .mLast = 4 }));
            EXPECT_FALSE(denoise.meets(ArenaSpan{ .mFirst = 3, .mLast = 3 }));
            EXPECT_FALSE(denoise.meets(ArenaSpan{ .mFirst = 0, .mLast = 0 }));
        }
    }
}
