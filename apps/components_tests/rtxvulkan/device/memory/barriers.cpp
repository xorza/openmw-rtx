#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxBarriersTest : Testing::DeviceTest
        {
        };

        /// **An image barrier is kept for a layout that moves, and for nothing else.** Two images
        /// ordered from one use in `GENERAL` to another are one memory barrier holding both
        /// dependencies: the compute write before a compute load, and the trace's write before a
        /// sample by either stage, merge to both writes' stages and access before both reads'.
        /// The same two images leaving `UNDEFINED`, and leaving `GENERAL` for the layout a sampler
        /// wants, are two image barriers and no memory barrier. Recorded and submitted, so the
        /// fixture's layers read what was emitted.
        TEST_F(RtxBarriersTest, aBarrierThatMovesNoLayoutIsMergedIntoOneMemoryBarrier)
        {
            const Device& device = *mHarness.mDevice;
            constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            const Image first(device, 4, 4, VK_FORMAT_R8G8B8A8_UNORM, usage, "first");
            const Image second(device, 4, 4, VK_FORMAT_R8G8B8A8_UNORM, usage, "second");

            getPool().submitAndWait([&](VkCommandBuffer commands) {
                Barriers moved(commands);
                first.addTransition(moved, Use::sUndefined, Use::sComputeWrite);
                second.addTransition(moved, Use::sUndefined, Use::sTraceWrite);
                EXPECT_EQ(moved.getImageCount(), 2u);
                EXPECT_EQ(moved.getMemory(), nullptr);
                moved.flush();

                Barriers merged(commands);
                first.addTransition(merged, Use::sComputeWrite, Use::sComputeRead);
                second.addTransition(merged, Use::sTraceWrite, Use::sShaderSample);
                EXPECT_EQ(merged.getImageCount(), 0u);

                const VkMemoryBarrier2* const memory = merged.getMemory();
                ASSERT_NE(memory, nullptr);
                EXPECT_EQ(memory->srcStageMask,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
                EXPECT_EQ(memory->srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                EXPECT_EQ(memory->dstStageMask,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
                EXPECT_EQ(
                    memory->dstAccessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                merged.flush();
                EXPECT_EQ(merged.getMemory(), nullptr) << "a flush empties the run";

                Barriers handed(commands);
                first.addTransition(handed, Use::sComputeRead, Use::sTextureSample);
                second.addTransition(handed, Use::sShaderSample, Use::sTextureSample);
                EXPECT_EQ(handed.getImageCount(), 2u);
                EXPECT_EQ(handed.getMemory(), nullptr);
                handed.flush();
            });
        }
    }
}
