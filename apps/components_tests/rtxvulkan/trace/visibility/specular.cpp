#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
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

        /// A grey diffuse texel.
        constexpr std::array<std::uint8_t, 4> sBaseTexel{ 128, 128, 128, 255 };

        /// A glossy floor under four lamps of four colours and a black sky: the lamps' one draw a
        /// pixel, weighed by its luminance, is the whole of the noise. Its specular map's texel is a
        /// metalness of `metal / 255` at a roughness of `roughness / 255`: a metal returns its lobe
        /// and nothing else, so all of the noise is in the lobe, and a dielectric has both halves.
        struct GlossyFloor
        {
            std::array<std::uint8_t, 4> mMapTexel;
            std::array<TextureData, 2> mTextures;
            SceneDesc mScene;

            explicit GlossyFloor(std::uint8_t roughness, std::uint8_t metal = 255)
                : mMapTexel{ metal, roughness, 0, 255 }
                , mTextures{ describeTexel(sBaseTexel, 0), describeTexel(mMapTexel, 1) }
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
            const GlossyFloor floor(128);
            Shaders::VisibilityConstants camera = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));

            const Frame averaged
                = shoot(floor.mScene, floor.mTextures, camera, sSize, { .mFrames = 16, .mFirstFrame = 2000 });
            const Frame filtered = shoot(floor.mScene, floor.mTextures, camera, sSize, filteredRun(16, 2000, false));

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

        /// **The lamps' split light on a glossy floor is the same in every frame.** A dielectric at
        /// half roughness, under the four lamps, where nothing stands between a lamp and the floor:
        /// the lamps' channel, `CHANNEL_LAMPED`, holds every lamp's light, each less the share its own lobe
        /// took, and the held lamp's bit, which is one. A lobe's share taken off the whole sum by the
        /// held lamp's Fresnel term, or the sum scaled by the held lamp's own estimate, would change
        /// with the lamp each pixel holds, in a light nothing filters.
        TEST_F(RtxVisibilityTest, theLampsSplitLightOnAGlossyFloorIsTheSameInEveryFrame)
        {
            const GlossyFloor floor(128, 0);
            Shaders::VisibilityConstants camera = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));

            const auto lampedAt = [&](std::uint32_t frame) {
                camera.mFrame = frame;
                shoot(floor.mScene, floor.mTextures, camera, sSize);
                std::vector<float> read;
                mRenderer.readChannel(Channel::Lamped, read);
                return read;
            };

            const std::vector<float> first = lampedAt(1000);
            const std::vector<float> second = lampedAt(1001);
            ASSERT_EQ(first.size(), std::size_t{ sSize } * sSize * 4);

            float brightest = 0.0f;
            for (std::size_t value = 0; value < first.size(); ++value)
            {
                if (value % 4 == 3)
                {
                    ASSERT_EQ(first[value], 1.0f) << "every lamp reaches pixel " << value / 4;
                    continue;
                }
                brightest = std::max(brightest, first[value]);
                ASSERT_EQ(first[value], second[value]) << "value " << value << " moved between two frames";
            }
            ASSERT_GT(brightest, 0.0f) << "a floor the lamps leave dark proves nothing";
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
            const GlossyFloor smooth(77);
            const GlossyFloor rough(255);
            const auto turnedAndRaw = [&](const GlossyFloor& floor) {
                const Shaders::VisibilityConstants before = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));
                Shaders::VisibilityConstants after = darkCameraAt(osg::Vec3f(150.0f, -200.0f, 300.0f));
                after.mFrame = 5000;

                shoot(floor.mScene, floor.mTextures, before, sSize, filteredRun(16, 3000, false));
                Frame turned = shoot(floor.mScene, floor.mTextures, after, sSize,
                    { .mFilter = true, .mAntilag = false, .mSetScene = false });
                Frame raw = shoot(floor.mScene, floor.mTextures, after, sSize, { .mSetScene = false });
                return std::pair{ std::move(turned), std::move(raw) };
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

            const auto [smoothTurned, smoothRaw] = turnedAndRaw(smooth);
            const auto [roughTurned, roughRaw] = turnedAndRaw(rough);
            ASSERT_GT(smoothRaw.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";
            ASSERT_GT(roughRaw.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";

            EXPECT_EQ(apart(smoothTurned, smoothRaw), 0u) << "the sharper lobe drops its history";
            EXPECT_GT(apart(roughTurned, roughRaw), 0u) << "the rough lobe keeps its history";

            // Measured at 35, 35 and 30% of the raw frame's error against 1 024 frames. **64 frames are
            // reference enough**: against them each error moves by 0.0007 at most, and in each channel
            // by under a tenth of the 0.004 to 0.021 between the two.
            const Shaders::VisibilityConstants after = darkCameraAt(osg::Vec3f(150.0f, -200.0f, 300.0f));
            const Frame roughReference = shoot(rough.mScene, rough.mTextures, after, sSize, { .mFrames = 64 });
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_LE(roughTurned.errorFrom(roughReference, channel), roughRaw.errorFrom(roughReference, channel))
                    << "channel " << channel << ": what the rough lobe kept is no worse than a raw frame";
        }

        /// **A sharp lobe's reflection follows a light that goes, before a still eye** (ReBLUR's
        /// responsive accumulation, `SPECULAR_RESPONSIVE_ROUGHNESS`). The metal floor under its four
        /// lamps for forty filtered frames, then the lamps go and nothing is left to light it: each
        /// frame keeps `1 - 1 / n` of the last, `n` the frames the history holds, and four frames on
        /// the floor holds `(1 - 1 / n)⁴` of its light. At a roughness of 5/255 = 0.0196 the cap is
        /// `32 · lerp(0.0194, 1, smoothstep(0.0784)) = 1.17`, held at three, so `(2/3)⁴ = 0.1975`
        /// is left; at one, thirty-two frames, `(31/32)⁴ = 0.8807`. Without the cap the sharp floor
        /// kept the rough one's.
        TEST_F(RtxVisibilityTest, aSharpLobesReflectionFollowsALightThatGoesBeforeAStillEye)
        {
            const auto left = [&](std::uint8_t roughness) {
                GlossyFloor floor(roughness);
                const Shaders::VisibilityConstants camera = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));
                const Frame lit = shoot(floor.mScene, floor.mTextures, camera, sSize, filteredRun(40, 6000, false));

                floor.mScene.clearPlacement();
                const Frame dark = shoot(floor.mScene, floor.mTextures, camera, sSize,
                    Shot{ .mFrames = 4,
                        .mAverage = false,
                        .mFirstFrame = 6040,
                        .mFilter = true,
                        .mAntilag = false,
                        .mSetScene = false });
                EXPECT_GT(lit.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";
                return dark.mean(1) / lit.mean(1);
            };

            EXPECT_NEAR(left(5), 16.0f / 81.0f, 1e-3f) << "the sharp floor dragged its lamps";
            EXPECT_NEAR(left(255), std::pow(31.0f / 32.0f, 4.0f), 1e-3f) << "the rough floor lost its history";
        }

        /// **A glossy floor follows a sky whose light halves** (`historyclamp.comp`). The metal floor
        /// at half roughness under a grey sky and no lamp, filtered for 64 still frames, then 30 more
        /// under the sky at half its light: the lobe reflects the sky alone, so the new level is half
        /// the old one exactly. Measured, the clamp's 30th frame stood at 1.046 of it, and without
        /// the clamp at 1.386, where the history fades by `1 / 32` a frame.
        TEST_F(RtxVisibilityTest, aGlossyFloorFollowsASkyWhoseLightHalves)
        {
            for (const bool antilag : { true, false })
            {
                GlossyFloor floor(128);
                floor.mScene.clearPlacement();
                Shaders::VisibilityConstants camera = darkCameraAt(osg::Vec3f(0.0f, -200.0f, 300.0f));
                camera.mSkyHorizon = osg::Vec3f(0.6f, 0.6f, 0.6f);
                camera.mSkyZenith = camera.mSkyHorizon;
                camera.mAmbientFromSky = 1.0f;
                const Frame before
                    = shoot(floor.mScene, floor.mTextures, camera, sSize, filteredRun(64, 7000, antilag));
                ASSERT_GT(before.mean(1), 0.0f) << "a floor that reflects nothing proves nothing";

                camera.mSkyHorizon *= 0.5f;
                camera.mSkyZenith *= 0.5f;
                const Frame after = shoot(floor.mScene, floor.mTextures, camera, sSize,
                    Shot{ .mFrames = 30,
                        .mAverage = false,
                        .mFirstFrame = 7064,
                        .mFilter = true,
                        .mAntilag = antilag,
                        .mSetScene = false });
                const float share = after.mean(1) / (0.5f * before.mean(1));
                if (antilag)
                    EXPECT_LT(share, 1.1f) << "the clamp did not follow the sky down";
                else
                    EXPECT_GT(share, 1.1f) << "the history followed the sky without the clamp, so this proves nothing";
            }
        }

        /// **A replacer's speckled reflectance stays sharp under the glossy filter's history**, which
        /// holds the lobe's light per unit of its specular albedo. A metal floor whose base
        /// colour, and so its F0, is a checker of single texels — 230 and 30, three pixels a square —
        /// at one roughness under a grey sky: the light its lobe reflects is the sky's, alike on both
        /// squares, and the checker is the albedo alone. Sixteen still frames filtered, then eight
        /// steps of 2.5 units sideways, three eighths of a pixel each, each one more filtered frame:
        /// every step resamples the history bilinearly. Against 64 unfiltered frames at the last
        /// place, the filtered frame's error was 0.0052, a tenth of the raw frame's 0.053; with the
        /// history kept whole, the checker blurred into it and the error was 0.025, nearly half.
        TEST_F(RtxVisibilityTest, aSpeckledReflectanceStaysSharpUnderTheGlossyHistory)
        {
            constexpr std::uint32_t extent = 256;
            std::vector<std::uint8_t> checker(std::size_t{ extent } * extent * 4, 255);
            for (std::uint32_t y = 0; y < extent; ++y)
                for (std::uint32_t x = 0; x < extent; ++x)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        checker[(std::size_t{ y } * extent + x) * 4 + channel] = (x + y) % 2 == 0 ? 230 : 30;
            TestTexture base;
            paintFlat(base, extent, checker, "checker");
            base.mData.mSlot = 0;
            constexpr std::array<std::uint8_t, 4> sMetalTexel{ 255, 128, 0, 255 };
            const std::array<TextureData, 2> textures{ base.mData, describeTexel(sMetalTexel, 1) };

            SceneDesc scene;
            const std::array positions = sheetAt(4000.0f, 0.0f);
            const Index mesh
                = scene.addMesh(MeshArrays{ .mPositions = positions, .mTexCoords = sQuadUv, .mIndices = sQuadIndices });
            const Index diffuse = scene.textures().add(VFS::Path::NormalizedView("checker.dds"));
            const Index map = scene.textures().add(
                VFS::Path::NormalizedView("checker_spec.dds"), TextureWrap::Repeat, TextureEncoding::Data);
            scene.addInstance(MeshInstance{
                .mMesh = mesh, .mMaterial = scene.addMaterial(Material{ .mDiffuse = diffuse, .mSpecular = map }) });

            const auto cameraAt = [](float across) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(across, -200.0f, 300.0f),
                    osg::Vec3f(across, 0.0f, 0.0f), 60.0f, sSize, sSize, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mSkyZenith = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mAmbientFromSky = 1.0f;
                return camera;
            };

            constexpr std::uint32_t steps = 8;
            constexpr float step = 2.5f;
            shoot(scene, textures, cameraAt(0.0f), sSize, filteredRun(16, 3000));
            Frame filtered;
            for (std::uint32_t at = 1; at <= steps; ++at)
            {
                Shaders::VisibilityConstants camera = cameraAt(step * static_cast<float>(at));
                camera.mFrame = 3015 + at;
                filtered = shoot(scene, textures, camera, sSize, { .mFilter = true, .mSetScene = false });
            }

            Shaders::VisibilityConstants last = cameraAt(step * static_cast<float>(steps));
            const Frame reference = shoot(scene, textures, last, sSize, { .mFrames = 64, .mSetScene = false });
            last.mFrame = 5000;
            const Frame raw = shoot(scene, textures, last, sSize, { .mSetScene = false });

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float rawError = raw.errorFrom(reference, channel);
                const float filteredError = filtered.errorFrom(reference, channel);
                ASSERT_GT(rawError, 0.0f) << "a raw frame with no noise proves nothing";
                EXPECT_LT(filteredError, rawError * 0.2f)
                    << "channel " << channel << ": raw " << rawError << ", filtered " << filteredError;
            }
        }
    }
}
