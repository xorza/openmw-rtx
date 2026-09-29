#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        constexpr std::uint32_t sSize = 64;

        /// A grey diffuse texel, and a specular map's: a metal at a roughness of `roughness / 255`,
        /// so the surface returns its lobe and nothing else.
        constexpr std::array<std::uint8_t, 4> sBaseTexel{ 128, 128, 128, 255 };

        /// A metal floor under four lamps of four colours and a black sky: the lamps' one draw a
        /// pixel, weighed by its luminance, is the whole of the noise, and all of it is in the lobe.
        struct MetalFloor
        {
            std::array<std::uint8_t, 4> mMetalTexel;
            std::array<TextureData, 2> mTextures;
            SceneDesc mScene;

            explicit MetalFloor(std::uint8_t roughness)
                : mMetalTexel{ 255, roughness, 0, 255 }
                , mTextures{ describeTexel(sBaseTexel, 0), describeTexel(mMetalTexel, 1) }
            {
                const std::array positions = sheetAt(4000.0f, 0.0f);
                const Index mesh = mScene.addMesh(
                    MeshArrays{ .mPositions = positions, .mTexCoords = sQuadUv, .mIndices = sQuadIndices });
                const Index diffuse = mScene.textures().add(VFS::Path::NormalizedView("base.dds"));
                const Index map = mScene.textures().add(
                    VFS::Path::NormalizedView("base_spec.dds"), TextureWrap::Repeat, TextureEncoding::Data);
                mScene.addInstance(MeshInstance{ .mMesh = mesh,
                    .mMaterial = mScene.addMaterial(Material{ .mDiffuse = diffuse, .mSpecular = map }) });

                const std::array<std::pair<osg::Vec2f, osg::Vec3f>, 4> lamps{ {
                    { osg::Vec2f(-100.0f, -100.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f) },
                    { osg::Vec2f(100.0f, -100.0f), osg::Vec3f(0.0f, 4000.0f, 0.0f) },
                    { osg::Vec2f(-100.0f, 100.0f), osg::Vec3f(0.0f, 0.0f, 4000.0f) },
                    { osg::Vec2f(100.0f, 100.0f), osg::Vec3f(4000.0f, 4000.0f, 0.0f) },
                } };
                for (const auto& [place, intensity] : lamps)
                    mScene.addLight(Light{
                        .mPosition = osg::Vec3f(place.x(), place.y(), 60.0f),
                        .mIntensity = intensity,
                        .mReach = 500.0f,
                    });
            }
        };

        /// A camera over the floor from `eye`, at the middle of it, under a black sky with no sun.
        Shaders::VisibilityConstants darkCameraAt(const osg::Vec3f& eye)
        {
            Shaders::VisibilityConstants camera
                = Testing::makeCamera(eye, osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, sSize, sSize, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();
            return camera;
        }

        /// **Over a still eye the glossy filter is the mean of its frames.** The floor at half
        /// roughness: sixteen frames filtered from an empty history, against the same sixteen
        /// averaged unfiltered. Under sixteen frames the blend's weight is one over the count, so the
        /// history is the running mean exactly, to the rounding of its floats: measured at 2e-5 to
        /// 6e-5 of the raw frame's error, and 8e-7 of the light. The metal has no diffuse half, so the
        /// wavelet filters nought and the whole of the difference from the last raw frame is the
        /// glossy filter's.
        TEST_F(RtxVisibilityTest, overAStillEyeTheGlossyFilterIsTheMeanOfItsFrames)
        {
            const MetalFloor floor(128);
            Shaders::VisibilityConstants camera = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));

            const Frame averaged
                = shoot(floor.mScene, floor.mTextures, camera, sSize, { .mFrames = 16, .mFirstFrame = 2000 });
            const Frame filtered = shoot(floor.mScene, floor.mTextures, camera, sSize, filteredRun(16, 2000));

            camera.mFrame = 2015;
            const Frame raw = shoot(floor.mScene, floor.mTextures, camera, sSize);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float rawError = raw.errorFrom(averaged, channel);
                const float filteredError = filtered.errorFrom(averaged, channel);
                ASSERT_GT(rawError, averaged.mean(channel) * 0.05f)
                    << "channel " << channel << ": four lamps drawn one a pixel are noisy";
                EXPECT_LT(filteredError, rawError * 1e-4f)
                    << "channel " << channel << ": raw " << rawError << ", filtered " << filteredError;
                EXPECT_NEAR(filtered.mean(channel), averaged.mean(channel), averaged.mean(channel) * 1e-5f)
                    << "channel " << channel << " keeps its light";
            }
        }

        /// **A lobe keeps its history over a turn of the view as wide as the lobe, and no wider.**
        /// Sixteen filtered frames from one eye, then one from an eye 150 units to the side, which
        /// turns the direction the middle of the floor is seen from by
        /// `acos(130000 / (360.56 · 390.51)) = 0.395` radians, at a cosine to the new eye of
        /// `300 / 390.51 = 0.768`. What the lobe allows is `atan(3 r²)` times that cosine:
        ///
        /// - at a roughness of 77/255 = 0.302, `atan(0.273) · 0.768 = 0.205` — the turn is past it,
        ///   the history is dropped and the frame is the raw frame, but for the half its channels are
        ///   read back in;
        /// - at one, `atan(3) · 0.768 = 0.959` — the history is kept at `1 - 0.395 / 0.959`, before
        ///   the two smoothsteps, and the frame stands apart from the raw one.
        TEST_F(RtxVisibilityTest, aLobeKeepsItsHistoryOverATurnOfTheViewAsWideAsTheLobe)
        {
            const auto turnedRawAndReference = [&](std::uint8_t roughness) {
                const MetalFloor floor(roughness);
                const Shaders::VisibilityConstants before = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));
                Shaders::VisibilityConstants after = darkCameraAt(osg::Vec3f(150.0f, -200.0f, 300.0f));
                after.mFrame = 5000;

                shoot(floor.mScene, floor.mTextures, before, sSize, filteredRun(16, 3000));
                Frame turned
                    = shoot(floor.mScene, floor.mTextures, after, sSize, { .mFilter = true, .mSetScene = false });
                Frame raw = shoot(floor.mScene, floor.mTextures, after, sSize, { .mSetScene = false });
                Frame reference = shoot(floor.mScene, floor.mTextures, after, sSize, { .mFrames = 256 });
                return std::tuple{ std::move(turned), std::move(raw), std::move(reference) };
            };

            // How many of the values stand further from the raw frame's than a half rounds by, 2^-11 of
            // a value, with room for one more rounding: a filtered frame reads its channels back at the
            // width a shown frame keeps radiance in, where the raw frame was composed before storing.
            const auto apart = [](const Frame& turned, const Frame& raw) {
                std::size_t count = 0;
                for (std::size_t value = 0; value < raw.mRadiance.size(); ++value)
                    if (value % 4 != 3
                        && std::abs(turned.at(value) - raw.at(value)) > std::abs(raw.at(value)) * 0x1p-10f + 1e-7f)
                        ++count;
                return count;
            };

            const auto [smoothTurned, smoothRaw, smoothReference] = turnedRawAndReference(77);
            const auto [roughTurned, roughRaw, roughReference] = turnedRawAndReference(255);
            ASSERT_GT(smoothRaw.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";
            ASSERT_GT(roughRaw.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";

            EXPECT_EQ(apart(smoothTurned, smoothRaw), 0u) << "the sharper lobe drops its history";
            EXPECT_GT(apart(roughTurned, roughRaw), 0u) << "the rough lobe keeps its history";

            // Measured 11 and 12% nearer the reference than the raw frame in red and green, and level
            // in blue: a turn this wide in one frame moves the highlights the history holds, which is
            // why the rule lets a history go at all.
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_LE(roughTurned.errorFrom(roughReference, channel), roughRaw.errorFrom(roughReference, channel))
                    << "channel " << channel << ": what the rough lobe kept is no worse than a raw frame";
        }
    }
}
