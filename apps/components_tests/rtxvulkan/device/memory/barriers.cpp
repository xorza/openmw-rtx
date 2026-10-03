#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/death.hpp>
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

        /// **A batch dropped with a dependency in it is a forgotten `flush`**, asserted where it goes
        /// rather than found as a race; one flushed, or never given anything, goes quietly.
        TEST_F(RtxBarriersTest, aBatchDroppedWithADependencyInItDies)
        {
            const Device& device = *mHarness.mDevice;
            const Image image(device, 4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, "dropped");

            getPool().submitAndWait([&](VkCommandBuffer commands) {
                {
                    Barriers flushed(commands);
                    image.addTransition(flushed, Use::sUndefined, Use::sComputeWrite);
                    flushed.flush();
                }
                {
                    Barriers empty(commands);
                }
                Testing::expectAssertDies(
                    [&] {
                        Barriers forgotten(commands);
                        image.addTransition(forgotten, Use::sComputeWrite, Use::sComputeRead);
                    },
                    "a barrier batch went out of scope with dependencies it never recorded");
            });
        }

        /// **A clear in the general layout writes every texel and records no barrier of its own.**
        /// An image a clear of another colour left in `GENERAL` for compute, ordered ahead of the
        /// clear by the caller as the function asks, cleared in place to
        /// `(64, 128, 191, 255) / 255` — bytes the format rounds back exactly — and ordered by one
        /// hand-over before it is read back: every texel is the second colour, and the layers, which
        /// the fixture reads, saw nothing a clear in that layout breaks.
        TEST_F(RtxBarriersTest, aClearInTheGeneralLayoutWritesEveryTexel)
        {
            const Device& device = *mHarness.mDevice;
            const Image image(device, 4, 3, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                "cleared");

            getPool().submitAndWait([&](VkCommandBuffer commands) {
                image.clear(commands, Use::sUndefined, VkClearColorValue{ .float32 = { 1.0f, 0.0f, 0.0f, 0.0f } },
                    Use::sComputeReadWrite);

                // The caller's half: what used the image before is ordered ahead of the clear, as
                // the head barrier orders the last frame's passes ahead of the upscaler's.
                handOver(commands,
                    BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT },
                    BufferUse{ VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT });
                image.clearInGeneral(commands,
                    VkClearColorValue{ .float32 = { 64.0f / 255.0f, 128.0f / 255.0f, 191.0f / 255.0f, 1.0f } });
                handOver(commands, BufferUse{ VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT },
                    BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT });
            });

            std::vector<std::uint8_t> bytes;
            image.read(VK_IMAGE_LAYOUT_GENERAL, bytes);
            ASSERT_EQ(bytes.size(), std::size_t{ 4 } * 3 * 4);
            for (std::size_t at = 0; at < bytes.size(); at += 4)
            {
                EXPECT_EQ(bytes[at], 64) << "texel " << at / 4;
                EXPECT_EQ(bytes[at + 1], 128) << "texel " << at / 4;
                EXPECT_EQ(bytes[at + 2], 191) << "texel " << at / 4;
                EXPECT_EQ(bytes[at + 3], 255) << "texel " << at / 4;
            }
        }
    }
}
