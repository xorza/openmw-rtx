#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtxvulkan/denoise/denoisehistory.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/frame/tracepast.hpp>
#include <components/rtxvulkan/shaders/shared/fields.h>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/trace/tracepasses.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        using RtxTraceChainTest = Testing::RendererTest;

        /// What the shared passes were built with, which every chain keeps the masks by: FSR's, the
        /// shared renderer's.
        constexpr UpscaleMasks sMasks = UpscaleMasks::Written;

        /// The arena a chain at this extent takes its shared images from, as a picture's keeps one.
        ImageArena arenaFor(const Device& device, const std::uint32_t width, const std::uint32_t height,
            const RadianceWidth radiance, const RendererFilter filter = RendererFilter::Runs)
        {
            std::vector<ArenaImage> declared;
            TraceChain::declareShared(width, height, radiance, 1, filter, declared);
            return ImageArena(device, declared);
        }

        ArenaRun everyOf(const ImageArena& arena)
        {
            return ArenaRun(arena, 0, arena.size());
        }

        /// **A chain asked for the extent it stands at keeps what it holds**: an upscaling mode changed between
        /// two that trace at one size asks again, and remaking the channels would be wasted. A chain let go of
        /// stands again at a new width, a new height or with the filter gone, and stores its radiance at the
        /// width it was made with.
        TEST_F(RtxTraceChainTest, aResizeToTheExtentItStandsAtRebuildsNothing)
        {
            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
            {
                const Device& device = mProbe.getDevice();
                TraceChain chain(device, mProbe.getTracePasses(), 1, radiance, TracePast::Dropped);

                const ImageArena wide = arenaFor(device, 64, 32, radiance);
                chain.resize(64, 32, RendererFilter::Runs, everyOf(wide));
                const VkImage built = chain.getChannels().get(Channel::Surface).getHandle();
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance));
                chain.resize(64, 32, RendererFilter::Runs, everyOf(wide));
                EXPECT_EQ(chain.getChannels().get(Channel::Surface).getHandle(), built)
                    << "the same extent made the channels anew";

                chain.release();
                const ImageArena low = arenaFor(device, 48, 16, radiance);
                chain.resize(48, 16, RendererFilter::Runs, everyOf(low));
                EXPECT_EQ(chain.getWidth(), 48u);
                EXPECT_EQ(chain.getHeight(), 16u);
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance)) << "a resize changed the width";

                // **The same extent with the filter gone is a new chain**: `native` and `off` trace at
                // one size, and under an upscaler that filters the light itself only the second
                // keeps a filter. Its histories go with it, and come back with it.
                chain.release();
                const ImageArena bare = arenaFor(device, 48, 16, radiance, RendererFilter::Absent);
                chain.resize(48, 16, RendererFilter::Absent, everyOf(bare));
                EXPECT_EQ(chain.getFilter(), RendererFilter::Absent);
                for (const Channel channel : { Channel::PaneSurface, Channel::PaneMotion })
                {
                    EXPECT_FALSE(chain.getChannels().keepsWhole(channel)) << channelName(channel);
                    EXPECT_EQ(chain.getChannels().get(channel).getWidth(), 1u)
                        << "the pane filter's tracks, with no filter";
                    EXPECT_EQ(chain.getChannels().get(channel).getHeight(), 1u);
                }
                EXPECT_TRUE(chain.getChannels().keepsWhole(Channel::Surface));
                std::array<const Image*, sDenoiserImageCount> written{};
                written.fill(&chain.getColour());
                chain.denoiserWritten(written);
                for (const Image* image : written)
                    EXPECT_EQ(image, nullptr) << "a chain with no filter named an image of one";

                const VkImage unfiltered = chain.getChannels().get(Channel::Surface).getHandle();
                chain.resize(48, 16, RendererFilter::Absent, everyOf(bare));
                EXPECT_EQ(chain.getChannels().get(Channel::Surface).getHandle(), unfiltered)
                    << "the same extent and filter made the channels anew";
                chain.release();
                chain.resize(48, 16, RendererFilter::Runs, everyOf(low));
                EXPECT_EQ(chain.getFilter(), RendererFilter::Runs);
                EXPECT_EQ(
                    chain.getChannels().get(Channel::PaneSurface).getWidth(), channelWidth(Channel::PaneSurface, 48))
                    << "the pane filter's tracks whole again";
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
                GBuffer::declareStages(16, 8, 1, declared);
                const ImageArena stages(device, declared);
                GBuffer channels(device, mProbe.getTracePasses().mChannels, 16, 8, RadianceWidth::Shown,
                    ChannelKeeping{ .mPast = past }, everyOf(stages));
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

        /// **The filter's scratch is declared only where it runs**: the stage images and the canvas
        /// either way, and `DenoiseHistory`'s scratch between them under `Runs` alone, which is the
        /// whole difference in count.
        TEST_F(RtxTraceChainTest, theFiltersScratchIsDeclaredOnlyWhereItRuns)
        {
            std::vector<ArenaImage> runs;
            TraceChain::declareShared(64, 32, RadianceWidth::Shown, 1, RendererFilter::Runs, runs);
            std::vector<ArenaImage> absent;
            TraceChain::declareShared(64, 32, RadianceWidth::Shown, 1, RendererFilter::Absent, absent);

            EXPECT_EQ(runs.size(), TraceChain::getSharedCount(RendererFilter::Runs));
            EXPECT_EQ(absent.size(), TraceChain::getSharedCount(RendererFilter::Absent));
            EXPECT_EQ(runs.size() - absent.size(), DenoiseHistory::getScratchCount());
            EXPECT_EQ(absent.size(), GBuffer::sStageImages + 1) << "the stage images and the canvas";
            for (std::size_t at = 0; at < GBuffer::sStageImages; ++at)
                EXPECT_EQ(absent[at], runs[at]) << "a stage image moved with the filter";
            EXPECT_EQ(absent.back(), runs.back()) << "the canvas moved with the filter";

            // The rule the world's chain is built by: the filter is absent only under an upscaler
            // that takes the noisy light, at a mode that upscales.
            EXPECT_EQ(rendererFilterUnder(Upscale::Quality, UpscalerInput::Noisy), RendererFilter::Absent);
            EXPECT_EQ(rendererFilterUnder(Upscale::Native, UpscalerInput::Noisy), RendererFilter::Absent);
            EXPECT_EQ(rendererFilterUnder(Upscale::Off, UpscalerInput::Noisy), RendererFilter::Runs);
            EXPECT_EQ(rendererFilterUnder(Upscale::Quality, UpscalerInput::Filtered), RendererFilter::Runs);
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
                    for (const RendererFilter filter : { RendererFilter::Runs, RendererFilter::Absent })
                    {
                        settle();
                        const VkDeviceSize frame = memory.getHeld(heap, MemoryUse::Frame);
                        const VkDeviceSize essential = memory.getHeld(heap, MemoryUse::Essential);

                        TraceChain chain(device, mProbe.getTracePasses(), 1, radiance, past);
                        const VkDeviceSize bins = memory.getHeld(heap, MemoryUse::Essential) - essential;
                        const VkDeviceSize arenaHeld = memory.getHeld(heap, MemoryUse::Frame);
                        const ImageArena arena = arenaFor(device, 64, 32, radiance, filter);
                        const VkDeviceSize shared = memory.getHeld(heap, MemoryUse::Frame) - arenaHeld;
                        chain.resize(64, 32, filter, everyOf(arena));
                        EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame) - frame - shared,
                            TraceChain::bytesAt(device, 64, 32, radiance, past, filter, sMasks));
                        EXPECT_EQ(memory.getHeld(heap, MemoryUse::Essential) - essential, bins)
                            << "a frame target was counted as essential memory";

                        chain.release();
                        settle();
                        EXPECT_FALSE(chain.isBuilt());
                        EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame), frame + shared)
                            << "a released chain still held its images";
                    }

            constexpr RendererFilter sRuns = RendererFilter::Runs;
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed, TracePast::Kept, sRuns, sMasks),
                TraceChain::bytesAt(device, 48, 32, RadianceWidth::Summed, TracePast::Kept, sRuns, sMasks));
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Summed, TracePast::Kept, sRuns, sMasks),
                TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Kept, sRuns, sMasks));
            EXPECT_GT(TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Kept, sRuns, sMasks),
                TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, TracePast::Dropped, sRuns, sMasks));

            // **The filter's absence is its histories, the frame before's surfaces and the pane filter's
            // tracks, exactly**: `DenoiseHistory` at the stored width, the G-buffer's held pair where the
            // chain keeps its past, and the nearest layer's surface and motion each a texel wide; its
            // histories and the tracks alone where it drops its past, as a picture's.
            const auto gbufferAt = [&](const TracePast past, const RendererFilter filter, const UpscaleMasks masks) {
                return GBuffer::bytesAt(device, 64, 32, RadianceWidth::Shown,
                    ChannelKeeping{ .mPast = past, .mFilter = filter, .mMasks = masks });
            };
            const VkDeviceSize heldPair
                = gbufferAt(TracePast::Kept, sRuns, sMasks) - gbufferAt(TracePast::Dropped, sRuns, sMasks);
            const VkDeviceSize tracks = gbufferAt(TracePast::Dropped, sRuns, sMasks)
                - gbufferAt(TracePast::Dropped, RendererFilter::Absent, sMasks);
            EXPECT_GT(heldPair, 0u);
            EXPECT_GT(tracks, 0u);
            const auto chainAt = [&](const TracePast past, const RendererFilter filter) {
                return TraceChain::bytesAt(device, 64, 32, RadianceWidth::Shown, past, filter, sMasks);
            };
            EXPECT_EQ(chainAt(TracePast::Kept, sRuns) - chainAt(TracePast::Kept, RendererFilter::Absent),
                DenoiseHistory::bytesAt(device, Shaders::storedWidth(64), 32, TracePast::Kept) + heldPair + tracks);
            EXPECT_EQ(chainAt(TracePast::Dropped, sRuns) - chainAt(TracePast::Dropped, RendererFilter::Absent),
                DenoiseHistory::bytesAt(device, Shaders::storedWidth(64), 32, TracePast::Dropped) + tracks);

            // **And the masks only where the upscaler reads them**: whatever the filter, a chain under an
            // upscaler that reads none keeps them a texel wide.
            EXPECT_GT(gbufferAt(TracePast::Kept, sRuns, UpscaleMasks::Written),
                gbufferAt(TracePast::Kept, sRuns, UpscaleMasks::Skipped));
            EXPECT_EQ(gbufferAt(TracePast::Kept, sRuns, UpscaleMasks::Written)
                    - gbufferAt(TracePast::Kept, sRuns, UpscaleMasks::Skipped),
                gbufferAt(TracePast::Dropped, RendererFilter::Absent, UpscaleMasks::Written)
                    - gbufferAt(TracePast::Dropped, RendererFilter::Absent, UpscaleMasks::Skipped));
        }
    }
}
