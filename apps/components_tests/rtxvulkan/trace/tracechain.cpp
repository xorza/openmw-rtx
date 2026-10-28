#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/trace/tracepasses.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        using RtxTraceChainTest = Testing::RendererTest;

        /// The arena a chain at this extent takes its shared images from, as a picture's keeps one.
        ImageArena arenaFor(
            const Device& device, const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
        {
            std::vector<ArenaImage> declared;
            TraceChain::declareShared(width, height, radiance, declared);
            return ImageArena(device, declared);
        }

        ArenaRun everyOf(const ImageArena& arena)
        {
            return ArenaRun(arena, 0, arena.size());
        }

        /// **A chain asked for the extent it stands at keeps what it holds**: an upscaling mode
        /// changed between two that trace at one size asks again, and the channels made anew for it
        /// were fourteen images for nothing. A new width or a new height is a new chain, and every
        /// chain stores its radiance at the width it was made with.
        TEST_F(RtxTraceChainTest, aResizeToTheExtentItStandsAtRebuildsNothing)
        {
            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
            {
                const Device& device = mProbe.getDevice();
                TraceChain chain(device, mProbe.getTracePasses(), 1, radiance, TracePast::Dropped);

                const ImageArena wide = arenaFor(device, 64, 32, radiance);
                chain.resize(64, 32, everyOf(wide));
                const GBuffer* const built = &chain.getChannels();
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance));
                chain.resize(64, 32, everyOf(wide));
                EXPECT_EQ(&chain.getChannels(), built) << "the same extent made the channels anew";

                const ImageArena narrow = arenaFor(device, 48, 32, radiance);
                chain.resize(48, 32, everyOf(narrow));
                EXPECT_NE(&chain.getChannels(), built) << "another width kept the channels";
                EXPECT_EQ(chain.getWidth(), 48u);
                EXPECT_EQ(chain.getHeight(), 32u);
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance)) << "a resize changed the width";

                const GBuffer* const narrower = &chain.getChannels();
                const ImageArena low = arenaFor(device, 48, 16, radiance);
                chain.resize(48, 16, everyOf(low));
                EXPECT_NE(&chain.getChannels(), narrower) << "another height kept the channels";
            }

            EXPECT_NE(radianceFormat(RadianceWidth::Summed), radianceFormat(RadianceWidth::Shown))
                << "the two widths store alike, so the format says nothing of which the chain took";
        }

        /// **A frame holds its histories to the surfaces the frame before wrote**: the trace writes one
        /// of each surface channel's two images and every temporal filter reads the other, which the
        /// frame before wrote (`GBuffer::getHeld`), and the set the trace binds names the image it
        /// writes. Every other channel is one image whatever the frame. Where the past is dropped,
        /// each surface channel is one image too, and the frame's own is what it holds.
        TEST_F(RtxTraceChainTest, aFrameHoldsItsHistoriesToTheSurfacesTheFrameBeforeWrote)
        {
            const Device& device = mProbe.getDevice();
            for (const TracePast past : { TracePast::Kept, TracePast::Dropped })
            {
                std::vector<ArenaImage> declared;
                GBuffer::declareStages(16, 8, declared);
                const ImageArena stages(device, declared);
                GBuffer channels(
                    device, mProbe.getTracePasses().mChannels, 16, 8, RadianceWidth::Shown, past, everyOf(stages));
                const auto turn = [&] {
                    Batch batch(device.getPool());
                    channels.begin(batch.getCommands(), ChannelWrites{});
                    channels.handOver(batch.getCommands());
                    batch.flush();
                };

                turn();
                for (const Channel channel : { Channel::Surface, Channel::PaneSurface })
                {
                    const Image* const written = &channels.get(channel);
                    const Image* const direct = &channels.get(Channel::Direct);
                    const VkDescriptorSet set = channels.getSet();
                    turn();

                    EXPECT_EQ(&channels.get(Channel::Direct), direct) << "a channel no filter holds to was paired";
                    if (past == TracePast::Kept)
                    {
                        EXPECT_EQ(&channels.getHeld(channel), written) << "the frame before's surface was not held";
                        EXPECT_NE(&channels.get(channel), written) << "a frame wrote over the surface it holds";
                        EXPECT_NE(channels.getSet(), set) << "the trace bound the surface the filters read";
                    }
                    else
                    {
                        EXPECT_EQ(&channels.getHeld(channel), &channels.get(channel));
                        EXPECT_EQ(channels.getSet(), set);
                    }
                }
            }

            device.waitIdle();
            device.collectIdle();
        }

        /// **What a chain says it takes is what it holds once made**, the running sum aside, which
        /// no trace has averaged into: the allocator counts each range at the size the driver asks
        /// of the image, and `bytesAt` asks the driver of the same descriptions, its arena apart.
        /// Counted as the use it was made with and no other — the tile lists, a buffer, as the images
        /// are; the sprite bins, made with the chain, as essential memory — and given back whole on
        /// `release`, whether it keeps its past or drops it. A wider chain takes more, a summed radiance more than a
        /// shown one, and a chain that keeps its past more than one that drops it, which holds one image a pair.
        TEST_F(RtxTraceChainTest, whatAChainSaysItTakesIsWhatItHolds)
        {
            const Device& device = mProbe.getDevice();
            MemoryAllocator& memory = device.getMemory();
            const std::uint32_t heap = memory.getVideoHeap();
            // What the tests before buried is freed at the next wait, which a chain's own submits
            // are; freed first, so what moves below is this chain's.
            const auto settle = [&] {
                device.waitIdle();
                device.collectIdle();
            };

            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
                for (const TracePast past : { TracePast::Kept, TracePast::Dropped })
                {
                    settle();
                    const VkDeviceSize frame = memory.getHeld(heap, MemoryUse::Frame);
                    const VkDeviceSize essential = memory.getHeld(heap, MemoryUse::Essential);

                    TraceChain chain(device, mProbe.getTracePasses(), 1, radiance, past);
                    const VkDeviceSize bins = memory.getHeld(heap, MemoryUse::Essential) - essential;
                    const VkDeviceSize arenaHeld = memory.getHeld(heap, MemoryUse::Frame);
                    const ImageArena arena = arenaFor(device, 64, 32, radiance);
                    const VkDeviceSize shared = memory.getHeld(heap, MemoryUse::Frame) - arenaHeld;
                    chain.resize(64, 32, everyOf(arena));
                    EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame) - frame - shared,
                        TraceChain::bytesAt(device, 64, 32, radiance, past));
                    EXPECT_EQ(memory.getHeld(heap, MemoryUse::Essential) - essential, bins)
                        << "a frame target was counted as essential memory";

                    chain.release();
                    settle();
                    EXPECT_FALSE(chain.isBuilt());
                    EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame), frame + shared)
                        << "a released chain still held its images";
                }

            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed, TracePast::Kept),
                TraceChain::bytesAt(device, 48, 32, RadianceWidth::Summed, TracePast::Kept));
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed, TracePast::Kept),
                TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Kept));
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Kept),
                TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Dropped));
        }
    }
}
