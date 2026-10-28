#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2d>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/shaders/shared/fsr.h>
#include <components/rtxvulkan/vulkanrenderer.hpp>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// Puts the shared renderer back to no upscaling however the test ends, since every other
        /// test traces through it at `off`.
        struct UpscaleFor
        {
            UpscaleFor(VulkanRenderer& renderer, Upscale mode)
                : mRenderer(renderer)
            {
                mRenderer.setUpscale(mode);
            }

            ~UpscaleFor() { mRenderer.setUpscale(Upscale::Off); }

            UpscaleFor(const UpscaleFor&) = delete;
            UpscaleFor& operator=(const UpscaleFor&) = delete;

            VulkanRenderer& mRenderer;
        };

        /// A floor under an overhead sun and a black sky: every pixel the same radiance, whatever
        /// the jitter samples.
        SceneDesc evenFloor()
        {
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            return scene;
        }

        Shaders::VisibilityConstants overTheFloor(std::uint32_t size)
        {
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
            return camera;
        }

        /// **A still picture holds still through every upscale.** A square floor under the sun against
        /// a black sky, from a still eye: its edges are the hardest thing a jittered frame aliases, so
        /// a sample the upscaler places even a fraction of a render pixel out moves them. The shown
        /// picture's brightness centroid, frame by frame over the last half of 32 frames, may move by a
        /// quarter of an output pixel at most, in every mode.
        ///
        /// Measured: 0.01 to 0.08 of a pixel over the last 32 of 64 frames, and on a Radeon 680M 0.01
        /// to 0.05 over the last 16 of 32, where `FFX_HALF` moved it 0.15 to 0.55, as over 64. With
        /// the jitter taken off the motion vector a second time (`fsrcallbacks.glsl`), the history
        /// followed the jitter and the swing was 0.5 of a pixel at native and 2.6 at ultra
        /// performance, growing with the ratio. At 96 pixels ultra performance traces 32, and its luma
        /// pyramid has five levels where the pass declares six: what `Upscaler::record` binds in place
        /// of the sixth.
        TEST_F(RtxVisibilityTest, aStillPictureHoldsStillThroughEveryUpscale)
        {
            constexpr std::uint32_t size = 96;
            constexpr std::uint32_t frames = 32;
            SceneDesc scene;
            addQuad(scene, sheetAt(100.0f, 0.0f));

            for (const Upscale mode : { Upscale::Native, Upscale::Quality, Upscale::Balanced, Upscale::Performance,
                     Upscale::UltraPerformance })
            {
                SCOPED_TRACE(sUpscaleNames.name(mode));
                mRenderer.resize(size, size);
                const UpscaleFor upscale(mRenderer, mode);
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

                // The camera the trace is handed is built for the render extent, which is the mode's:
                // square, as the output is.
                const Shaders::VisibilityConstants camera = overTheFloor(mRenderer.getExtents().mRenderWidth);

                osg::Vec2d lowest(1e9, 1e9);
                osg::Vec2d highest(-1e9, -1e9);
                std::vector<std::uint8_t> pixels;
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    Shaders::VisibilityConstants sampled = camera;
                    sampled.mFrame = at;
                    mProbe.traceFrame(sampled, FrameOptions{ .mExposure = FixedExposure{ 1.0f } });
                    ASSERT_TRUE(mRenderer.finishFrame().has_value());
                    if (at < frames / 2)
                        continue;

                    mRenderer.readPixels(pixels);
                    double sum = 0.0;
                    osg::Vec2d weighted;
                    for (std::uint32_t y = 0; y < size; ++y)
                        for (std::uint32_t x = 0; x < size; ++x)
                        {
                            const double lit = pixels[(y * size + x) * 4 + 1];
                            sum += lit;
                            weighted += osg::Vec2d(x, y) * lit;
                        }
                    ASSERT_GT(sum, 0.0) << "a floor that shows nothing proves nothing";

                    const osg::Vec2d centroid = weighted / sum;
                    lowest = osg::Vec2d(std::min(lowest.x(), centroid.x()), std::min(lowest.y(), centroid.y()));
                    highest = osg::Vec2d(std::max(highest.x(), centroid.x()), std::max(highest.y(), centroid.y()));
                }

                EXPECT_LE(highest.x() - lowest.x(), 0.25) << "the picture moved across";
                EXPECT_LE(highest.y() - lowest.y(), 0.25) << "the picture moved down";
            }
        }

        /// **The smallest frame draws in every mode**: the presentation holds a frame to
        /// `sSmallestFrameSide`, which ultra performance traces at one pixel, and nothing between the
        /// trace and the upscaler may come to an extent of nought there.
        TEST_F(RtxVisibilityTest, theSmallestFrameDrawsInEveryUpscale)
        {
            constexpr std::uint32_t side = sSmallestFrameSide;
            SceneDesc scene;
            addQuad(scene, sheetAt(100.0f, 0.0f));

            for (const Upscale mode : sUpscaleMenu)
            {
                SCOPED_TRACE(sUpscaleNames.name(mode));
                mRenderer.resize(side, side);
                const UpscaleFor upscale(mRenderer, mode);
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

                const Shaders::VisibilityConstants camera = overTheFloor(mRenderer.getExtents().mRenderWidth);
                for (std::uint32_t at = 0; at < 4; ++at)
                {
                    Shaders::VisibilityConstants sampled = camera;
                    sampled.mFrame = at;
                    mProbe.traceFrame(sampled, FrameOptions{ .mExposure = FixedExposure{ 1.0f } });
                    ASSERT_TRUE(mRenderer.finishFrame().has_value());
                }

                std::vector<std::uint8_t> pixels;
                mRenderer.readPixels(pixels);
                EXPECT_EQ(pixels.size(), std::size_t{ side } * side * 4);
            }
        }

        /// **Behind a post, upscaled, a puff is hidden where the traced pixel's centre meets the post**,
        /// and so holds still under the jitter: the composite finds the depth once along the ray
        /// through that centre, which does not move with the jitter, and each shown pixel over it
        /// reads it. The edge is the traced grid's, and still.
        ///
        /// **At quality a 48-pixel picture traces 32.** Over sixty degrees, at the post's two
        /// hundred, a traced pixel is `2 * 200 * tan 30° / 32 = 7.217` units across and a shown one
        /// `4.811`. Shown pixel 24's centre is at `0.5 * 4.811 = 2.406`, and the centre of the traced
        /// pixel under it, 16, at `0.5 * 7.217 = 3.608`, so the post's edge at 3 stands between
        /// them: that pixel's own ray meets the post and its traced centre passes beside it, and the
        /// puff is drawn there as at pixel 25, where neither meets it. Pixel 23 and its traced pixel
        /// 15 both meet the post, which is black, as is the sky, and what the shown picture holds
        /// there is the bloom of the puff beside it: a tenth of the open pixel, measured, where a
        /// puff drawn there would be all of it, so a quarter tells the two apart. Every one of the
        /// last 32 of 64 jittered frames holds, where a depth that moved with the sample — the
        /// traced pixel's jittered one, which the composite once read — would hide the puff on the
        /// frames whose sample landed left of the edge.
        TEST_F(RtxVisibilityTest, anUpscaledPuffBehindAPostIsHiddenWhereTheTracedCentreMeetsIt)
        {
            constexpr std::uint32_t size = 48;
            constexpr std::uint32_t frames = 64;
            constexpr std::uint32_t row = size / 2;
            constexpr float edge = 3.0f;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> textures{ describeTexel(white) };

            SceneDesc scene;
            const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
            const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 400.0f, 0.0f),
                .mRadius = 60.0f,
                .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                .mAlpha = 1.0f } };
            scene.addEmitter(sprites, cut, BlendKind::Over);
            addQuad(scene,
                std::array{ osg::Vec3f(-500.0f, 200.0f, -500.0f), osg::Vec3f(edge, 200.0f, -500.0f),
                    osg::Vec3f(edge, 200.0f, 500.0f), osg::Vec3f(-500.0f, 200.0f, 500.0f) });

            mRenderer.resize(size, size);
            const UpscaleFor upscale(mRenderer, Upscale::Quality);
            setWorld(scene, textures);
            ASSERT_EQ(mRenderer.getExtents().mRenderWidth, 32u) << "the figures above are for 32 traced pixels";

            Shaders::VisibilityConstants camera
                = Testing::makeCameraAlong(osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f,
                    mRenderer.getExtents().mRenderWidth, mRenderer.getExtents().mRenderHeight, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

            std::vector<std::uint8_t> pixels;
            const auto green = [&](std::uint32_t x) { return static_cast<float>(pixels[(row * size + x) * 4 + 1]); };
            for (std::uint32_t at = 0; at < frames; ++at)
            {
                Shaders::VisibilityConstants sampled = camera;
                sampled.mFrame = at;
                mProbe.traceFrame(sampled, FrameOptions{ .mExposure = FixedExposure{ 1.0f } });
                ASSERT_TRUE(mRenderer.finishFrame().has_value());
                if (at < frames / 2)
                    continue;

                mRenderer.readPixels(pixels);
                const float open = green(25);
                ASSERT_GT(open, 2.0f) << "the puff did not reach the pixel clear of the post, frame " << at;
                EXPECT_NEAR(green(24) / open, 1.0f, 0.05f)
                    << "the puff over the post's edge was not drawn where its traced centre passes the post, frame "
                    << at;
                EXPECT_LT(green(23), 0.25f * open) << "the post did not hide the puff behind it, frame " << at;
            }
        }

        /// **An even frame reconstructs to itself.** FSR at native size, over 24 jittered frames — past
        /// its eight phases twice — of a floor every pixel of which is one colour: whatever the
        /// history, the resampling and the locks do, a field of one value has nothing to move, and
        /// the picture is the unupscaled one to within a byte of the display's rounding.
        TEST_F(RtxVisibilityTest, anEvenFrameReconstructsToItself)
        {
            constexpr std::uint32_t size = 64;
            const SceneDesc scene = evenFloor();
            const Shaders::VisibilityConstants camera = overTheFloor(size);

            std::vector<std::uint8_t> plain;
            shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mLoss = HistoryLoss::Cut });
            mRenderer.readPixels(plain);

            std::vector<std::uint8_t> upscaled;
            {
                const UpscaleFor native(mRenderer, Upscale::Native);
                shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mLoss = HistoryLoss::Cut });
                mRenderer.readPixels(upscaled);
            }

            ASSERT_EQ(upscaled.size(), plain.size());
            int worst = 0;
            for (std::size_t at = 0; at < plain.size(); ++at)
                if (at % 4 != 3)
                    worst = std::max(worst, std::abs(int{ upscaled[at] } - int{ plain[at] }));
            EXPECT_LE(worst, 1) << "an even floor came out of the upscaler uneven";
        }

        /// **What enters at the left edge leaves the right edge's history alone.** A 60-pixel frame at
        /// native size runs four threads a row past its edge, the last workgroup's. Each loads the
        /// depth and the motion at a pixel off the frame, which the surface channel's fields would
        /// place at the left edge of the row. Such a thread then scattered the left edge's depth
        /// along the left edge's motion onto the right edge, and the band there lost its history.
        /// The eye moves left beside a far comb of strips, whose aliased edges only an accumulated
        /// history smooths. Over the last six frames a near post sweeps in from the left. Its motion
        /// is `51.96 * 5.8 / 50 = 6` pixels a frame, enough to carry a scatter from past the edge
        /// back onto the frame. The right band, ten columns that the post never reaches, must come
        /// out as it does with no post at all.
        TEST_F(RtxVisibilityTest, whatEntersAtTheLeftEdgeLeavesTheRightEdgesHistoryAlone)
        {
            constexpr std::uint32_t size = 60;
            constexpr std::uint32_t frames = 24;
            constexpr std::uint32_t band = 10;
            constexpr float step = 5.8f;
            static_assert(size % Shaders::FSR_WORKGROUP != 0, "a frame of whole workgroups has no thread past it");

            const auto sceneOf = [&](bool withPost) {
                SceneDesc scene;
                for (float left = -2000.0f; left < 2000.0f; left += 100.0f)
                    addQuad(scene,
                        std::array{ osg::Vec3f(left, 2000.0f, -2000.0f), osg::Vec3f(left + 50.0f, 2000.0f, -2000.0f),
                            osg::Vec3f(left + 50.0f, 2000.0f, 2000.0f), osg::Vec3f(left, 2000.0f, 2000.0f) });
                const float postEdge = -step * static_cast<float>(frames - 1);
                if (withPost)
                    addQuad(scene,
                        std::array{ osg::Vec3f(-3000.0f, 50.0f, -500.0f), osg::Vec3f(postEdge, 50.0f, -500.0f),
                            osg::Vec3f(postEdge, 50.0f, 500.0f), osg::Vec3f(-3000.0f, 50.0f, 500.0f) });
                return scene;
            };

            mRenderer.resize(size, size);
            const UpscaleFor native(mRenderer, Upscale::Native);
            const auto run = [&](const SceneDesc& scene) {
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    Shaders::VisibilityConstants camera
                        = Testing::makeCameraAlong(osg::Vec3f(-step * static_cast<float>(at), 0.0f, 0.0f),
                            osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
                    camera.mSkyHorizon = osg::Vec3f();
                    camera.mSkyZenith = osg::Vec3f();
                    camera.mAmbientFromSky = 0.0f;
                    camera.mAmbient = osg::Vec3f();
                    camera.mSun = Shaders::sunSource(
                        osg::Vec3f(0.0f, -1.0f, 1.0f) / std::sqrt(2.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
                    camera.mFrame = at;
                    mProbe.traceFrame(camera,
                        FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mExposure = FixedExposure{ 1.0f } });
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                }
                std::vector<std::uint8_t> pixels;
                mRenderer.readPixels(pixels);
                return pixels;
            };

            const std::vector<std::uint8_t> bare = run(sceneOf(false));
            const std::vector<std::uint8_t> withPost = run(sceneOf(true));
            ASSERT_EQ(withPost.size(), bare.size());
            int worst = 0;
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = size - band; x < size; ++x)
                    for (std::uint32_t c = 0; c < 3; ++c)
                    {
                        const std::size_t at = (y * size + x) * 4 + c;
                        worst = std::max(worst, std::abs(int{ withPost[at] } - int{ bare[at] }));
                    }
            EXPECT_LE(worst, 1) << "the post at the left edge moved the right edge's picture";
        }

        /// **The same frames reconstruct to the same picture.** Two runs of 24 upscaled frames from a
        /// reset, byte for byte: the upscaler's history, its atomics and its pyramid leave nothing
        /// to the order its lanes ran in, which is what `repeat` holds the rest of the frame to.
        TEST_F(RtxVisibilityTest, theSameFramesUpscaleToTheSamePicture)
        {
            constexpr std::uint32_t size = 64;
            SceneDesc scene = evenFloor();
            addQuad(scene, sheetAt(40.0f, 120.0f));
            const Shaders::VisibilityConstants camera = overTheFloor(size);

            const UpscaleFor native(mRenderer, Upscale::Native);
            const auto run = [&] {
                shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mLoss = HistoryLoss::Cut });
                std::vector<std::uint8_t> pixels;
                mRenderer.readPixels(pixels);
                return pixels;
            };

            const std::vector<std::uint8_t> first = run();
            EXPECT_EQ(run(), first);
        }
    }
}
