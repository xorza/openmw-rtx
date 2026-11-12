#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/rendererprobe.hpp>
#include <components/dlss/dlss.hpp>
#include <components/platform/gpu.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>
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

        /// `options` asking for DLSS at `mode`.
        RendererOptions withDlss(RendererOptions options, const Upscale mode)
        {
            options.mRun.mProfile.mUpscaler = UpscalerChoice::Dlss;
            options.mRun.mProfile.mUpscale = mode;
            options.mDlss = libraryFolders();
            // Shown, as a played run's: a summed run keeps room for a running sum and a deep target
            // that no frame here makes, so the frame would hold less than its reserve for a reason
            // that is not the feature's.
            options.mRun.mProfile.mRadianceWidth = RadianceWidth::Shown;
            // One kernel of each stage, and not a tuple each, whose compiling a renderer let go waits
            // out.
            options.mRun.mProfile.mSpecializeKernels = false;
            return options;
        }

        /// A renderer of its own that asks for DLSS at `mode` and records no frame. Unvalidated: the
        /// layers at the instance double what the renderer's making takes, past the suite's limit a
        /// test.
        RendererOptions unrecordedDlssAt(const Upscale mode)
        {
            return withDlss(Testing::describeRenderer(sWidth, sHeight, false), mode);
        }

        /// Whether DLSS can run here, and if not why: the card is not NVIDIA's, or the library is not
        /// beside the test binary, the two facts DLSS needs before NGX is asked anything.
        std::optional<std::string> dlssMissing()
        {
            if (!Platform::Gpu::has(Platform::Gpu::Vendor::Nvidia))
                return "DLSS needs an NVIDIA card";
            if (!DlssLibrary::installedIn(libraryFolders()))
                return "Ray Reconstruction's library is not beside the test binary: "
                    + std::string(DlssLibrary::download().mFileName);
            return std::nullopt;
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
        /// Skipped where DLSS cannot run (`dlssMissing`), as is every test here that makes it.
        TEST_F(RtxRayReconstructionTest, theFeatureIsFrameMemoryTheReserveKeepsRoomFor)
        {
            if (const std::optional<std::string> missing = dlssMissing())
                GTEST_SKIP() << *missing;

            RendererOptions options = unrecordedDlssAt(Upscale::Quality);
            options.mRun.mReservedUpscales = { Upscale::Quality, Upscale::Native };
            VulkanRenderer renderer(options);
            Testing::RendererProbe probe(renderer);
            const Upscaler& upscaler = probe.getUpscaler();
            EXPECT_EQ(renderer.getUpscalerInput(), UpscalerInput::Noisy);
            EXPECT_TRUE(
                std::ranges::find(renderer.getUpscalers(), UpscalerChoice::Dlss) != renderer.getUpscalers().end());

            const auto extentsOf = [&](const Upscale mode) {
                const FrameExtents extents = upscaler.extentsFor(sWidth, sHeight, mode);
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

        /// **Ray Reconstruction traces at the extents NGX gives a mode**, its ratios rounded to the
        /// nearest pixel, and not at FSR's truncated ones: at 320×180, balanced is 58% a side, 185.6 by
        /// 104.4, so 186 by 104, where FSR's 1.7 gives 188 by 105; quality two thirds, 213.3 by 120, so
        /// 213 by 120; performance half, 160 by 90; ultra performance a third, 106.7 by 60, so 107 by 60;
        /// native the output. A renderer made at balanced traces at balanced's.
        TEST_F(RtxRayReconstructionTest, theTraceIsAtTheExtentsNgxGivesTheMode)
        {
            if (const std::optional<std::string> missing = dlssMissing())
                GTEST_SKIP() << *missing;

            VulkanRenderer renderer(unrecordedDlssAt(Upscale::Balanced));
            Testing::RendererProbe probe(renderer);
            const Upscaler& upscaler = probe.getUpscaler();
            const auto traced = [&](const Upscale mode) {
                const FrameExtents extents = upscaler.extentsFor(sWidth, sHeight, mode);
                EXPECT_EQ(extents.mOutputWidth, sWidth);
                EXPECT_EQ(extents.mOutputHeight, sHeight);
                return std::pair(extents.mRenderWidth, extents.mRenderHeight);
            };
            EXPECT_EQ(traced(Upscale::Balanced), std::pair(186u, 104u));
            EXPECT_EQ(traced(Upscale::Quality), std::pair(213u, 120u));
            EXPECT_EQ(traced(Upscale::Performance), std::pair(160u, 90u));
            EXPECT_EQ(traced(Upscale::UltraPerformance), std::pair(107u, 60u));
            EXPECT_EQ(traced(Upscale::Native), std::pair(sWidth, sHeight));
            EXPECT_EQ(traced(Upscale::Off), std::pair(sWidth, sHeight));

            const FrameExtents running = renderer.getExtents();
            EXPECT_EQ(std::pair(running.mRenderWidth, running.mRenderHeight), std::pair(186u, 104u));
        }

        using RtxRayReconstructionFrameTest = Testing::RendererTest;

        /// **A Ray Reconstruction frame leaves the trace's composite where the trace handed it, through
        /// a cut and a change of mode**: NGX reads the composite in the layout its wrapper names, and
        /// the upscaler puts it back, so a read of it after the frame meets the layout the trace said
        /// it left. A frame at quality from a cut, the composite read at quality's 43 by 24 (64 by 36
        /// at two thirds), then one at balanced from a cut, read at its 37 by 21 (58% of 64 is 37.1,
        /// of 36 is 20.9), with nothing the layers raise in all of it.
        TEST_F(RtxRayReconstructionFrameTest, aFrameLeavesTheCompositeAsTheTraceHandedItThroughACutAndAModeChange)
        {
            if (const std::optional<std::string> missing = dlssMissing())
                GTEST_SKIP() << *missing;

            constexpr std::uint32_t width = 64;
            constexpr std::uint32_t height = 36;
            VulkanRenderer renderer(withDlss(Testing::describeRenderer(width, height), Upscale::Quality));
            Testing::awaitKernels(renderer);
            forgetErrors(renderer);
            Testing::RendererProbe probe(renderer);

            SceneDesc scene;
            Testing::addQuad(scene, Testing::sheetAt(4000.0f, 0.0f));
            renderer.setScene(SceneSlot::world(), scene, {});

            const auto draw = [&] {
                const FrameExtents extents = renderer.getExtents();
                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(),
                    60.0f, extents.mRenderWidth, extents.mRenderHeight, 100000.0f);
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
                probe.traceFrame(camera, FrameOptions{ .mLoss = HistoryLoss::Cut, .mExposure = FixedExposure{ 1.0f } });
                EXPECT_TRUE(renderer.finishFrame().has_value());
                std::vector<float> composite;
                probe.readComposite(composite);
                return std::pair(extents, composite.size());
            };

            const auto [quality, qualityRead] = draw();
            EXPECT_EQ(std::pair(quality.mRenderWidth, quality.mRenderHeight), std::pair(43u, 24u));
            EXPECT_EQ(qualityRead, std::size_t{ 43 } * 24 * 4);
            renderer.setUpscale(Upscale::Balanced);
            const auto [balanced, balancedRead] = draw();
            EXPECT_EQ(std::pair(balanced.mRenderWidth, balanced.mRenderHeight), std::pair(37u, 21u));
            EXPECT_EQ(balancedRead, std::size_t{ 37 } * 21 * 4);
            reportErrors(renderer, "a DLSS frame");
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
