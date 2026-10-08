#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/shaders/shared/composite.h>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        using RtxTraceChainTest = Testing::RendererTest;

        /// **A chain asked for the extent it stands at keeps what it holds**: an upscaling mode
        /// changed between two that trace at one size asks again, and the channels made anew for it
        /// were fourteen images for nothing. A new width or a new height is a new chain, and every
        /// chain stores its radiance at the width it was made with.
        TEST_F(RtxTraceChainTest, aResizeToTheExtentItStandsAtRebuildsNothing)
        {
            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
            {
                TraceChain chain(mRenderer.getDevice(), mRenderer.getTracePasses(), 1, radiance, MemoryUse::Essential);

                chain.resize(64, 32);
                const GBuffer* const built = &chain.getChannels();
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance));
                chain.resize(64, 32);
                EXPECT_EQ(&chain.getChannels(), built) << "the same extent made the channels anew";

                chain.resize(48, 32);
                EXPECT_NE(&chain.getChannels(), built) << "another width kept the channels";
                EXPECT_EQ(chain.getWidth(), 48u);
                EXPECT_EQ(chain.getHeight(), 32u);
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance)) << "a resize changed the width";

                const GBuffer* const narrower = &chain.getChannels();
                chain.resize(48, 16);
                EXPECT_NE(&chain.getChannels(), narrower) << "another height kept the channels";
            }

            EXPECT_NE(radianceFormat(RadianceWidth::Summed), radianceFormat(RadianceWidth::Shown))
                << "the two widths store alike, so the format says nothing of which the chain took";
        }

        /// **What a chain says it takes is what it holds once made**, the running sum aside, which
        /// no trace has averaged into: the allocator counts each range at the size the driver asks
        /// of the image, and `bytesAt` asks the driver of the same descriptions. Counted as the use
        /// it was made with and no other, and given back whole on `release`. A wider chain takes
        /// more, and a summed radiance more than a shown one.
        TEST_F(RtxTraceChainTest, whatAChainSaysItTakesIsWhatItHolds)
        {
            const Device& device = mRenderer.getDevice();
            MemoryAllocator& memory = device.getMemory();
            const std::uint32_t heap = memory.getVideoHeap();
            const VkDeviceSize sum = Image::bytesFor(device,
                ImageDescription{ .mWidth = 64,
                    .mHeight = 32,
                    .mFormat = toVulkanFormat(COMPOSITE_SUM_FORMAT),
                    .mUsage = VK_IMAGE_USAGE_STORAGE_BIT });

            // What the tests before buried is freed at the next wait, which a chain's own submits
            // are; freed first, so what moves below is this chain's.
            const auto settle = [&] {
                device.waitIdle();
                device.collectIdle();
            };

            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
            {
                settle();
                const VkDeviceSize frame = memory.getHeld(heap, MemoryUse::Frame);
                const VkDeviceSize essential = memory.getHeld(heap, MemoryUse::Essential);

                TraceChain chain(device, mRenderer.getTracePasses(), 1, radiance, MemoryUse::Frame);
                const VkDeviceSize bins = memory.getHeld(heap, MemoryUse::Essential) - essential;
                chain.resize(64, 32);
                EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame) - frame,
                    TraceChain::bytesAt(device, 64, 32, radiance) - sum);
                EXPECT_EQ(memory.getHeld(heap, MemoryUse::Essential) - essential, bins)
                    << "a frame target was counted as essential memory";

                chain.release();
                settle();
                EXPECT_FALSE(chain.isBuilt());
                EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame), frame) << "a released chain still held its images";
            }

            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed),
                TraceChain::bytesAt(device, 48, 32, RadianceWidth::Summed));
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed),
                TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown));
        }
    }
}
