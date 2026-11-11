#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/framesinflight.hpp>
#include <components/rtx/renderer/nullrenderer.hpp>
#include <components/rtx/renderer/picturerenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        RendererOptions optionsOf(const std::uint32_t width, const std::uint32_t height)
        {
            RendererOptions options;
            options.mWidth = width;
            options.mHeight = height;
            return options;
        }

        Viewpoint viewOf(const std::uint32_t width, const std::uint32_t height)
        {
            Viewpoint view;
            view.mEyes.mWorld.mWidth = width;
            view.mEyes.mWorld.mHeight = height;
            return view;
        }

        /// The ring a backend keeps, kept by a renderer with no device: each frame reported once and
        /// in order, the frame behind collected only where the next would not fit, and a skipped
        /// frame numbered where a placement opened it, and never reported.
        TEST(RtxNullRendererTest, eachFrameIsReportedOnceInOrderAndCollectedOnlyWhereTheRingIsFull)
        {
            static_assert(sFramesInFlight == 2, "the frames below fill a ring of two");
            NullRenderer renderer(optionsOf(8, 4));
            const SceneDesc scene;
            renderer.setScene(SceneSlot::world(), scene, {});

            EXPECT_FALSE(renderer.finishFrame().has_value()) << "a report of a frame never drawn";

            static_cast<void>(renderer.renderFrame(FrameRequest{}));
            EXPECT_FALSE(renderer.collectFrame().has_value()) << "collected with room for the next frame";
            static_cast<void>(renderer.renderFrame(FrameRequest{}));

            // Full: the oldest comes back, which the ring held alone when it closed.
            const std::optional<FrameResult> oldest = renderer.collectFrame();
            ASSERT_TRUE(oldest.has_value());
            EXPECT_EQ(oldest->mFrame, 0u);
            EXPECT_EQ(oldest->mInFlight, 1u);
            EXPECT_FALSE(renderer.collectFrame().has_value()) << "collected with room for the next frame";

            // The one behind it, which closed beside frame nought.
            const std::optional<FrameResult> behind = renderer.finishFrame();
            ASSERT_TRUE(behind.has_value());
            EXPECT_EQ(behind->mFrame, 1u);
            EXPECT_EQ(behind->mInFlight, 2u);
            EXPECT_FALSE(renderer.finishFrame().has_value()) << "a frame reported twice";

            // A skip with nothing placed closes no frame; frame two, placed and skipped, is numbered
            // and never reported.
            renderer.skipFrame();
            EXPECT_EQ(renderer.getFrameCount(), 2u) << "a skip numbered a frame nothing opened";
            renderer.placeScene(SceneSlot::world(), scene);
            renderer.skipFrame();
            EXPECT_EQ(renderer.getFrameCount(), 3u);
            EXPECT_FALSE(renderer.finishFrame().has_value()) << "a skipped frame reported";

            static_cast<void>(renderer.renderFrame(FrameRequest{}));
            const std::optional<FrameResult> after = renderer.finishFrame();
            ASSERT_TRUE(after.has_value());
            EXPECT_EQ(after->mFrame, 3u);
            EXPECT_EQ(renderer.getFrameCount(), 4u);
        }

        /// **A frame reports the reconstruction it was traced under**, as a device's does: one that
        /// asked for no denoiser reports none where the profile wants one, and one traced before a
        /// menu turned the upscaler on reports none, collected after it. And the profile holds what
        /// the upscaler the run asked for came to, never `auto`: FSR, with no device to offer DLSS.
        TEST(RtxNullRendererTest, aFrameReportsTheReconstructionItWasTracedUnder)
        {
            for (const UpscalerChoice asked : sUpscalerMenu)
            {
                RendererOptions options = optionsOf(8, 4);
                options.mRun.mProfile.mUpscaler = asked;
                EXPECT_EQ(NullRenderer(options).getProfile().mUpscaler, UpscalerChoice::Fsr)
                    << sUpscalerNames.name(asked);
            }

            NullRenderer renderer(optionsOf(8, 4));
            const SceneDesc scene;
            renderer.setScene(SceneSlot::world(), scene, {});

            FrameRequest raw;
            raw.mOptions.mReconstruction = ReconstructionRequest{ .mDenoise = false };
            static_cast<void>(renderer.renderFrame(raw));
            static_cast<void>(renderer.renderFrame(FrameRequest{}));
            renderer.setUpscale(Upscale::Performance);

            const std::optional<FrameResult> first = renderer.finishFrame();
            const std::optional<FrameResult> second = renderer.finishFrame();
            ASSERT_TRUE(first.has_value());
            ASSERT_TRUE(second.has_value());
            EXPECT_EQ(first->mReconstruction.mFilter, LightFilter::None) << "what the frame asked, not the profile";
            EXPECT_EQ(second->mReconstruction.mFilter, LightFilter::Renderer)
                << "the profile's, where the frame asked nothing";
            EXPECT_EQ(second->mReconstruction.mUpscale, Upscale::Off) << "the menu after the trace reached the frame";
        }

        /// What the null renderer says it holds is what an uploader decides by: a scene it was given
        /// is placed, not built again, and grown, not built again, as on a device.
        TEST(RtxNullRendererTest, anUploaderBuildsOncePlacesAndExtendsWhatTheRendererHolds)
        {
            NullRenderer renderer(optionsOf(8, 4));
            SceneDesc scene;
            SceneUploader uploader;
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));

            const auto hand = [&](const SceneSlot slot) {
                return uploader.hand(renderer, SceneUploader::Handing{ .mSlot = slot, .mScene = scene }).mKind;
            };
            EXPECT_EQ(hand(SceneSlot::world()), SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(hand(SceneSlot::world()), SceneUpload::Kind::Placed);

            Testing::addModel(scene, VFS::Path::NormalizedView("textures/two.dds"));
            EXPECT_EQ(hand(SceneSlot::world()), SceneUpload::Kind::Extended);
            EXPECT_EQ(renderer.describeHeld(SceneSlot::world()).mIdentity, scene.getIdentity());

            // A view scene holds nothing until it is handed one, and its slot goes back to be taken
            // again empty.
            const SceneSlot view = renderer.addViewScene();
            EXPECT_EQ(view, SceneSlot::view(0));
            EXPECT_EQ(renderer.describeHeld(view).mIdentity, 0u);
            SceneUploader viewUploader;
            static_cast<void>(
                viewUploader.hand(renderer, SceneUploader::Handing{ .mSlot = view, .mScene = scene }).mKind);
            EXPECT_EQ(renderer.describeHeld(view).mIdentity, scene.getIdentity());

            renderer.dropViewScene(view);
            EXPECT_EQ(renderer.addViewScene(), SceneSlot::view(0));
            EXPECT_EQ(renderer.describeHeld(SceneSlot::view(0)).mIdentity, 0u) << "a dropped scene held again";
            EXPECT_EQ(renderer.addViewScene(), SceneSlot::view(1));

            // The lowest free slot first, not the last one dropped, as a device hands them
            // (`SlotPool`): 0 is dropped before 1, and a list taken from the back would answer 1.
            renderer.dropViewScene(SceneSlot::view(0));
            renderer.dropViewScene(SceneSlot::view(1));
            EXPECT_EQ(renderer.addViewScene(), SceneSlot::view(0));
            EXPECT_EQ(renderer.addViewScene(), SceneSlot::view(1));
        }

        /// A picture's copy stands, as black, from the first trace that left one until its texture is
        /// dropped, as a device's does, and the texture's slot goes back to be taken again empty.
        TEST(RtxNullRendererTest, aPicturesCopyStandsFromTheTraceThatLeftItUntilItsTextureIsDropped)
        {
            NullRenderer renderer(optionsOf(8, 4));
            const GuiSlot texture = renderer.addGuiTexture(4, 2);
            EXPECT_EQ(texture, GuiSlot::at(0));

            // Four by two, at four bytes a pixel, into a buffer a byte longer: the byte past the
            // texture is not written.
            std::array<std::uint8_t, 4 * 2 * 4 + 1> into{};
            into.fill(0xFF);
            EXPECT_FALSE(renderer.takeGuiCopy(texture, into)) << "a copy before any trace";

            EXPECT_TRUE(renderer.traceGuiTexture(texture, SceneSlot::world(), viewOf(4, 2), GuiTraceOptions{}));
            EXPECT_FALSE(renderer.takeGuiCopy(texture, into)) << "a copy no trace asked for";

            const GuiTraceOptions copied{ .mReadBack = true };
            EXPECT_FALSE(renderer.traceGuiTexture(texture, SceneSlot::world(), viewOf(0, 2), copied))
                << "a view of no pixels recorded";
            EXPECT_FALSE(renderer.takeGuiCopy(texture, into)) << "a copy of a view of no pixels";

            EXPECT_TRUE(renderer.traceGuiTexture(texture, SceneSlot::world(), viewOf(4, 2), copied));
            ASSERT_TRUE(renderer.takeGuiCopy(texture, into));
            for (std::size_t at = 0; at + 1 < into.size(); ++at)
                EXPECT_EQ(into[at], 0u) << "byte " << at;
            EXPECT_EQ(into.back(), 0xFFu) << "a byte past the texture written";
            EXPECT_TRUE(renderer.traceGuiTexture(texture, SceneSlot::world(), viewOf(4, 2), GuiTraceOptions{}));
            EXPECT_TRUE(renderer.takeGuiCopy(texture, into)) << "a trace with no copy took the last one away";

            // Lent the rectangle's own bytes, and only the rectangle's.
            EXPECT_EQ(renderer.lendGuiTexture(texture, GuiRegion{ .mX = 1, .mY = 0, .mWidth = 3, .mHeight = 2 }).size(),
                3u * 2u * 4u);
            renderer.sendGuiTexture(texture);

            const GuiSlot second = renderer.addGuiTexture(2, 2);
            EXPECT_EQ(second, GuiSlot::at(1));
            renderer.dropGuiTexture(texture);
            const GuiSlot again = renderer.addGuiTexture(1, 1);
            EXPECT_EQ(again, GuiSlot::at(0)) << "a dropped slot not taken again";
            EXPECT_FALSE(renderer.takeGuiCopy(again, into)) << "a copy outlived its texture";

            // The lowest free slot first, as for a view scene: 0 dropped before 1.
            renderer.dropGuiTexture(again);
            renderer.dropGuiTexture(second);
            EXPECT_EQ(renderer.addGuiTexture(1, 1), GuiSlot::at(0));
            EXPECT_EQ(renderer.addGuiTexture(1, 1), GuiSlot::at(1));
        }

        /// The extents follow the size and the upscaler as a device's do, and the picture read back
        /// is black at the output extent.
        TEST(RtxNullRendererTest, theExtentsAndThePictureFollowTheSizeAndTheUpscale)
        {
            NullRenderer renderer(optionsOf(8, 4));
            EXPECT_EQ(renderer.getExtents().mRenderWidth, 8u);
            EXPECT_EQ(renderer.getExtents().mRenderHeight, 4u);

            std::vector<std::uint8_t> pixels(3, 0xFF);
            renderer.readPixels(pixels);
            EXPECT_EQ(pixels, std::vector<std::uint8_t>(8 * 4 * 4, 0));

            // Twelve by six under performance, whose ratio is two: traced at six by three.
            renderer.resize(12, 6);
            renderer.setUpscale(Upscale::Performance);
            EXPECT_EQ(renderer.getProfile().mUpscale, Upscale::Performance);
            EXPECT_EQ(renderer.getExtents().mRenderWidth, 6u);
            EXPECT_EQ(renderer.getExtents().mRenderHeight, 3u);
            EXPECT_EQ(renderer.getExtents().mOutputWidth, 12u);
            EXPECT_EQ(renderer.getExtents().mOutputHeight, 6u);

            renderer.readPixels(pixels);
            EXPECT_EQ(pixels.size(), std::size_t{ 12 * 6 * 4 });

            renderer.setBounceRays(3);
            EXPECT_EQ(renderer.getProfile().mReconstruction.mSampling.mBounceRays, 3u);

            EXPECT_TRUE(renderer.getProfile().mReconstruction.mSampling.mIndirect);
            renderer.setIndirectLighting(false);
            EXPECT_FALSE(renderer.getProfile().mReconstruction.mSampling.mIndirect);
            EXPECT_EQ(renderer.getProfile().mReconstruction.mSampling.mBounceRays, 3u)
                << "off keeps the slider's count for when it is back on";

            // The setters' contracts hold here as on a device: a gamma of nought and a count past
            // the most a kernel traces are a reader's to refuse.
            renderer.setGamma(2.2f);
            EXPECT_EQ(renderer.getProfile().mGamma, 2.2f);
            Testing::expectAssertDies([&] { renderer.setGamma(0.0f); }, "a gamma its reader should have refused");
            Testing::expectAssertDies([&] { renderer.setBounceRays(Shaders::MAX_BOUNCE_RAYS + 1); },
                "a count its reader should have refused");
        }
    }
}
