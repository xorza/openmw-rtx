#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2d>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

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
        /// picture's brightness centroid, frame by frame over the last half of 64 frames, may move by a
        /// quarter of an output pixel at most, in every mode.
        ///
        /// Measured: 0.01 to 0.08 of a pixel. With the jitter taken off the motion vector a second
        /// time (`fsrcallbacks.glsl`), the history followed the jitter and the swing was 0.5 of a
        /// pixel at native and 2.6 at ultra performance, growing with the ratio. At 96 pixels ultra
        /// performance traces 32, and its luma pyramid has five levels where the pass declares six:
        /// what `Upscaler::record` binds in place of the sixth.
        TEST_F(RtxVisibilityTest, aStillPictureHoldsStillThroughEveryUpscale)
        {
            constexpr std::uint32_t size = 96;
            constexpr std::uint32_t frames = 64;
            SceneDesc scene;
            addQuad(scene, sheetAt(100.0f, 0.0f));

            for (const Upscale mode : { Upscale::Native, Upscale::Quality, Upscale::Balanced, Upscale::Performance,
                     Upscale::UltraPerformance })
            {
                SCOPED_TRACE(sUpscaleNames.name(mode));
                mRenderer.resize(size, size);
                const UpscaleFor upscale(mRenderer, mode);
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});
                mRenderer.resetHistory();

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
                    mRenderer.renderFrame(sampled, FrameOptions{ .mExposure = ExposureRule{ .mFixed = 1.0f } });
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
            shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mResetHistory = true });
            mRenderer.readPixels(plain);

            std::vector<std::uint8_t> upscaled;
            {
                const UpscaleFor native(mRenderer, Upscale::Native);
                shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mResetHistory = true });
                mRenderer.readPixels(upscaled);
            }

            ASSERT_EQ(upscaled.size(), plain.size());
            int worst = 0;
            for (std::size_t at = 0; at < plain.size(); ++at)
                if (at % 4 != 3)
                    worst = std::max(worst, std::abs(int{ upscaled[at] } - int{ plain[at] }));
            EXPECT_LE(worst, 1) << "an even floor came out of the upscaler uneven";
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
                shoot(scene, {}, camera, size, { .mFrames = 24, .mAverage = false, .mResetHistory = true });
                std::vector<std::uint8_t> pixels;
                mRenderer.readPixels(pixels);
                return pixels;
            };

            const std::vector<std::uint8_t> first = run();
            EXPECT_EQ(run(), first);
        }
    }
}
