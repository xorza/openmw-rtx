#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/rendererprobe.hpp>
#include <components/dlss/dlss.hpp>
#include <components/platform/gpu.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/frame/tracepast.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/upscale/dlssupscaler.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        using RtxRayReconstructionTest = Testing::DeviceTest;

        /// Where the test finds Ray Reconstruction's library: beside the test binary, the folder NGX
        /// searches as the game's own (`DlssLibrary::Folders::mGame`).
        DlssLibrary::Folders libraryFolders()
        {
            return DlssLibrary::Folders{ .mRuntime = {}, .mGame = std::filesystem::current_path() };
        }

        constexpr std::uint32_t sWidth = 320;
        constexpr std::uint32_t sHeight = 180;

        /// A renderer of its own that asks for DLSS at `mode`, keeping room for `reserved`.
        RendererOptions dlssAt(const Upscale mode, std::vector<Upscale> reserved)
        {
            // Unvalidated: nothing here records a frame, and the layers at the instance double what the
            // renderer's making takes, past the suite's limit a test.
            RendererOptions options = Testing::describeRenderer(sWidth, sHeight, false);
            options.mRun.mProfile.mUpscaler = UpscalerChoice::Dlss;
            options.mRun.mProfile.mUpscale = mode;
            options.mRun.mReservedUpscales = std::move(reserved);
            options.mDlss = libraryFolders();
            // Shown, as a played run's: a summed run keeps room for a running sum and a deep target
            // that no frame here makes, so the frame would hold less than its reserve for a reason
            // that is not the feature's.
            options.mRun.mProfile.mRadianceWidth = RadianceWidth::Shown;
            // One kernel of each stage, and not a tuple each, whose compiling a renderer let go waits
            // out: no frame is traced here.
            options.mRun.mProfile.mSpecializeKernels = false;
            return options;
        }

        /// **DLSS takes the noisy light, and what its feature holds is frame memory the reserve keeps
        /// room for, measured off NGX.** A run of the quality mode that keeps room for the native one
        /// holds quality's feature, which the allocator is told it holds outside itself, and measures
        /// native's beside it: the frame use is what the allocator holds plus the feature, and native's
        /// feature is larger. Native's measured beside quality's is what native's holds alone once the
        /// mode moves to it, which it is only where NGX's stats, the bytes every feature holds, are
        /// read as what the new feature added; and then the frame holds what the reserve keeps, native
        /// being the larger of the two modes.
        ///
        /// Skipped where the card is not NVIDIA's or the library is not beside the test binary, the
        /// two facts DLSS needs before NGX is asked anything; on such a box nothing here could run.
        TEST_F(RtxRayReconstructionTest, theFeatureIsFrameMemoryTheReserveKeepsRoomFor)
        {
            if (!Platform::Gpu::has(Platform::Gpu::Vendor::Nvidia))
                GTEST_SKIP() << "DLSS needs an NVIDIA card";
            if (!DlssLibrary::installedIn(libraryFolders()))
                GTEST_SKIP() << "Ray Reconstruction's library is not beside the test binary: "
                             << DlssLibrary::download().mFileName;

            VulkanRenderer renderer(dlssAt(Upscale::Quality, { Upscale::Quality, Upscale::Native }));
            Testing::RendererProbe probe(renderer);
            const Upscaler& upscaler = probe.getUpscaler();
            EXPECT_EQ(renderer.getUpscalerInput(), UpscalerInput::Noisy);
            EXPECT_TRUE(
                std::ranges::find(renderer.getUpscalers(), UpscalerChoice::Dlss) != renderer.getUpscalers().end());

            const auto extentsOf = [](const Upscale mode) {
                const FrameExtents extents = extentsFor(sWidth, sHeight, mode);
                return VkExtent2D{ extents.mRenderWidth, extents.mRenderHeight };
            };
            const VkExtent2D output{ sWidth, sHeight };
            const VkDeviceSize quality = upscaler.bytesAt(extentsOf(Upscale::Quality), output);
            const VkDeviceSize native = upscaler.bytesAt(extentsOf(Upscale::Native), output);
            EXPECT_GT(quality, 0u);
            EXPECT_GT(native, quality) << "native reconstructs more pixels than quality";
            EXPECT_EQ(upscaler.getHeldOutside(), quality) << "the running mode's feature stands";

            const MemoryAllocator& memory = probe.getDevice().getMemory();
            const auto heldInside = [&] { return memory.getHeld(memory.getVideoHeap(), MemoryUse::Frame); };
            EXPECT_EQ(renderer.getMemoryReport().mUses.mFrame, heldInside() + quality);
            EXPECT_GT(renderer.getMemoryReport().mUses.mFrameReserve, renderer.getMemoryReport().mUses.mFrame)
                << "room kept for native's larger frame";

            renderer.setUpscale(Upscale::Native);
            EXPECT_EQ(upscaler.getHeldOutside(), native) << "native's measured beside quality's is native's alone";
            const MemoryUses uses = renderer.getMemoryReport().mUses;
            EXPECT_EQ(uses.mFrame, heldInside() + native);
            EXPECT_EQ(uses.mFrame, uses.mFrameReserve)
                << "the frame holds what the reserve keeps, the feature among it";
        }

        /// **RR's guides cost the frame's arena nothing at any mode**: they stand over the upscale's span
        /// alone, under the trace's stage images, which an arena lays them over, so the world's arena
        /// at a 1920×1080 output under RR is as large with the five guides as without them.
        TEST_F(RtxRayReconstructionTest, theGuidesStandInTheArenaUnderTheTracesStageImages)
        {
            const Device& device = getDevice();
            for (const Upscale mode : sUpscaleMenu)
            {
                if (!upscales(mode))
                    continue;
                const FrameExtents extents = extentsFor(1920, 1080, mode);
                const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };
                const VkExtent2D output{ 1920, 1080 };

                std::vector<ArenaImage> declared;
                TraceChain::declareShared(
                    render.width, render.height, RadianceWidth::Shown, 1, RendererFilter::Absent, declared);
                const std::size_t chained = declared.size();
                DlssUpscaler::declareShared(render, output, declared);
                ASSERT_EQ(declared.size(), chained + DlssUpscaler::getSharedCount());

                // The output alone, the first of the upscaler's: the guides are the rest.
                const std::vector<ArenaImage> unguided(declared.begin(), declared.begin() + chained + 1);
                EXPECT_EQ(ImageArena::bytesFor(device, declared), ImageArena::bytesFor(device, unguided))
                    << sUpscaleNames.name(mode);
            }
        }
    }
}
