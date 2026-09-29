#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

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
