#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/visibility.h>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// A puff's texture is read across the disc the ray sees, so its cover ends where the texture's does
        /// and not at the disc's rim. The disc is cut square to the ray and the texture is read along the
        /// screen's axes, which agree only on the frame's axis: a ray forty degrees off it sees the offset
        /// across the disc shortened to `cos 40° = 0.77` of itself, so a blob's edge at `13 / 16 = 0.81` of the
        /// texture would lie beyond the silhouette and the silhouette would cut the blob's alpha as a hard rim.
        ///
        /// A blob transparent beyond thirteen of its sixteen texels, on a puff thirty degrees off the axis of a
        /// frame ninety degrees wide, against the same puff wearing an opaque texel: walking out from the
        /// centre toward the frame's edge, the opaque one covers the wall to the rim and the blob to `13 / 16`
        /// of the way there.
        TEST_F(RtxVisibilityTest, aPuffOffTheAxisIsReadAcrossTheDiscTheRaySees)
        {
            constexpr std::uint32_t size = 129;
            constexpr std::size_t row = size / 2;

            std::vector<std::uint8_t> texels(32 * 32 * 4);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    const float r = std::sqrt((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f));
                    std::uint8_t* texel = &texels[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4];
                    texel[0] = texel[1] = texel[2] = 255;
                    texel[3] = r > 13.0f ? 0 : 255;
                }
            TestTexture blob;
            paintFlat(blob, 32, texels, "blob.dds");

            constexpr std::array<std::uint8_t, 4> opaque{ 255, 255, 255, 255 };

            // Thirty degrees off the axis, three hundred units out, at the height of the eye:
            // `300 sin 30° = 150` across and `300 cos 30° = 259.8` along. On a frame ninety degrees
            // wide the centre lands `tan 30° / tan 45°` of the half-frame from the middle.
            const float centre = static_cast<float>(row) * (1.0f + std::tan(osg::DegreesToRadians(30.0f)));

            // How far the puff's cover reaches from its centre toward the frame's edge, in pixels:
            // the last pixel of the row darker than the open wall.
            const auto coverReaches = [&](const TextureData& texture) {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("blob.dds"), TextureWrap::Clamp);
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(150.0f, 259.81f, 200.0f),
                    .mRadius = 50.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 200.0f), osg::Vec3f(0.0f, 300.0f, 200.0f), 90.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f(0.6f, 0.6f, 0.6f);
                camera.mSkyZenith = camera.mSkyHorizon;
                camera.mAmbientFromSky = 1.0f;
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();

                const std::array<TextureData, 1> worn{ texture };
                const Frame frame = shoot(scene, std::span<const TextureData>(worn), camera, size);

                const float open = frame.at((row * size + 0) * 4);
                std::size_t last = 0;
                for (std::size_t x = static_cast<std::size_t>(centre); x < size; ++x)
                    if (frame.at((row * size + x) * 4) < open - 0.01f)
                        last = x;
                return static_cast<float>(last) + 0.5f - centre;
            };

            const float disc = coverReaches(describeTexel(opaque));
            const float painted = coverReaches(blob.mData);
            ASSERT_GT(disc, 10.0f) << "a disc wide enough to resolve the difference";

            // To the pixel the rim's own radius resolves, and the binary blob's edge is within one.
            EXPECT_NEAR(painted / disc, 13.0f / 16.0f, 1.5f / disc)
                << "the blob covers " << painted << " of a disc of " << disc;
        }

        /// A sprite is shadowed like anything else, by whatever stands over it. A particle has no normal and
        /// still has an up: what a point sees of the sky is a question about the point, so rain under a bridge
        /// carries no open sky and smoke in a canyon not the full sun.
        ///
        /// Two rays for the layer and not two for a puff: `spritesAlong` asks at the first sprite that is lit,
        /// because a rainstorm puts dozens over a pixel and a ray apiece is too many. The lamps take the same
        /// treatment, and what a lamp delivers runs as one over the square of a distance that changes from
        /// sprite to sprite, so the sum stays each puff's own and only the seeing is asked once for the layer.
        ///
        /// A lid four hundred units over the sprite and nothing else, each source in turn: what lights the
        /// sprite in the open stops lighting it under the lid. The two that arrive from a direction, and not
        /// the fill: a puff has no side, so what it sees of an ambient is drawn over the whole sphere, and a
        /// lid over a sprite with nothing under it leaves the lower half open, so the honest answer is half and
        /// not nought. It is one draw a froxel a frame, so a single frame of it is a coin; what the fill owes to
        /// what stands near is measured exactly by the test below.
        TEST_F(RtxVisibilityTest, aSpriteIsShadowedByWhatStandsOverIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ describeTexel(white) };

            // One source at a time, so what the lid takes is never ambiguous.
            enum class Source
            {
                Sun,
                Lamp,
            };

            const auto sprited = [&](bool lidded, Source source) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over);

                if (lidded)
                    addQuad(scene, sheetAt(4000.0f, 600.0f));

                // Over the lid, so the same sheet that takes the sun takes this too.
                if (source == Source::Lamp)
                    scene.addLight(Light{ .mPosition = osg::Vec3f(0.0f, 0.0f, 800.0f),
                        .mIntensity = osg::Vec3f(4.0e5f, 4.0e5f, 4.0e5f),
                        .mReach = 4000.0f });

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -1.0f, 400.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);

                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbientFromSky = 1.0f;
                camera.mSun = Shaders::sunSource(
                    osg::Vec3f(0.0f, 0.0f, 1.0f), source == Source::Sun ? osg::Vec3f(4.0f, 4.0f, 4.0f) : osg::Vec3f());
                camera.mAmbient = osg::Vec3f();

                const Frame frame = shoot(scene, puff, camera, size);

                return frame.at(centre);
            };

            // **Nought and not merely less**, which each of the two can be held to: a shadow ray at
            // the sun is one ray with no draw in it, and the lamp stands over the lid so every point
            // of it a ray is aimed at is behind the same sheet.
            for (const Source source : { Source::Sun, Source::Lamp })
            {
                const float open = sprited(false, source);
                ASSERT_GT(open, 0.01f) << "the source did not reach the sprite at all";

                EXPECT_EQ(sprited(true, source), 0.0f) << "a lid did not stop it";
            }
        }

        /// A puff under a shade that takes half the sun holds still from one frame to the next. A puff reads
        /// one ray a froxel a frame and is lit hard enough to show it: the history averages ten frames of it,
        /// which leaves a coin a column wide, where the air is lit by a fraction of the sun and takes the tent
        /// across its neighbours besides. So a puff reads the seeing the tent averaged (`fogSeeing`), measured
        /// as `theVolumeSettlesTheAirUnderALampRatherThanFlickeringBlockByBlock` measures the air: the mean
        /// step of a settled pixel between frames, against its mean.
        ///
        /// Slats eight units wide and eight apart, six hundred units over the puff and under the sun, so every
        /// froxel's jittered shadow ray is a fair coin: thirty-three pixels over sixty degrees at four hundred
        /// units are `2 * 400 * tan 30° / 33 = 14` units each, so a column of eight is 112 units across and
        /// holds seven slats and seven gaps. The mean is the half the slats leave, so a filter that dimmed the
        /// puff would not hold as still. The froxel's own seeing steps by 3.5% of the pixel and the averaged
        /// seeing by 2.1%; the bound stands between them.
        TEST_F(RtxVisibilityTest, aPuffUnderASlattedShadeHoldsStillFromFrameToFrame)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr std::uint32_t frames = 192;
            constexpr std::uint32_t settled = 64;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ describeTexel(white) };

            const auto lit = [&](bool slatted) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over);

                if (slatted)
                {
                    constexpr float width = 8.0f;
                    constexpr float extent = 200.0f;
                    constexpr float height = 600.0f;
                    for (float x = -extent; x < extent; x += 2.0f * width)
                        addQuad(scene,
                            std::array{ osg::Vec3f(x, -extent, height), osg::Vec3f(x + width, -extent, height),
                                osg::Vec3f(x + width, extent, height), osg::Vec3f(x, extent, height) });
                }

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -1.0f, 400.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbientFromSky = 0.0f;
                camera.mAmbient = osg::Vec3f();
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

                std::vector<float> radiance;
                shoot(scene, puff, camera, size,
                    Shot{ .mFrames = slatted ? frames : 1u,
                        .mAverage = false,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame& each) { radiance.push_back(each.at(centre)); } });
                return radiance;
            };

            const auto settledMean = [&](const std::vector<float>& radiance) {
                double total = 0.0;
                for (std::size_t frame = frames - settled; frame < frames; ++frame)
                    total += double{ radiance[frame] };
                return total / double{ settled };
            };

            // One frame of the open puff, because with nothing over it every ray finds the sun.
            const std::vector<float> shaded = lit(true);
            const double open = double{ lit(false).front() };
            const double mean = settledMean(shaded);
            ASSERT_GT(open, 0.01) << "the sun did not reach the puff at all";

            double stepped = 0.0;
            for (std::size_t frame = frames - settled; frame < frames; ++frame)
                stepped += std::abs(double{ shaded[frame] } - double{ shaded[frame - 1] });
            const double step = stepped / double{ settled };

            EXPECT_NEAR(mean / open, 0.5, 0.1) << "the slats take half the sun and the filter none of it";
            EXPECT_LT(step / mean, 0.028) << "how far a settled pixel of a shaded puff moves between frames";
        }

        /// A first-person billboard behind a palm has no volume to stand in front of it, and in the open
        /// is the ball it is drawn as elsewhere, under either blend. A sprite of radius twenty stands
        /// at 100 behind a wall at 90, whose ball reaches 80, and at 60, whose ball ends at 80 short of
        /// the wall: a chord uncut, so `(until - from) / (2 * halfChord) = 1` and its middle is the centre.
        TEST_F(RtxVisibilityTest, aHandSpellBehindAnOpaqueSurfaceDoesNotLeakItsVolumeThroughIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            Shaders::VisibilityConstants camera
                = Testing::makeCameraAlong(osg::Vec3f(), osg::Vec3f(0, 1, 0), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f(0.25f, 0.25f, 0.25f);
            camera.mSun = Shaders::sunSource(osg::Vec3f(0, 0, 1), osg::Vec3f());

            const auto seen = [&](const BlendKind blend, const bool firstPerson, const float depth) {
                SceneDesc scene;
                const Index texture = scene.textures().add(VFS::Path::NormalizedView("hand_spell.dds"));
                const std::array<TextureData, 1> textures{ describeTexel(white) };
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0, depth, 0),
                    .mRadius = 20.0f,
                    .mColour = osg::Vec3f(1, 1, 1),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, texture, blend, 0.0f, sNoIndex, false, firstPerson);
                addQuad(scene, uprightQuadAt(200.0f, 90.0f));
                return shoot(scene, textures, camera, size).at(centreValueOf(size));
            };

            const auto wall = [&] {
                SceneDesc scene;
                addQuad(scene, uprightQuadAt(200.0f, 90.0f));
                return shoot(scene, std::span<const TextureData>(), camera, size).at(centreValueOf(size));
            }();

            for (const BlendKind blend : { BlendKind::Add, BlendKind::Over })
            {
                EXPECT_NE(seen(blend, false, 100.0f), wall) << "a world puff's near half stands before the wall";
                EXPECT_NEAR(seen(blend, true, 100.0f), wall, 1e-4f) << "the hand billboard is wholly behind it";
                EXPECT_NE(seen(blend, true, 60.0f), wall) << "a billboard in front is seen";
                EXPECT_NEAR(seen(blend, true, 60.0f), seen(blend, false, 60.0f), 1e-4f)
                    << "and is what a ball there draws";
            }
        }

        /// A puff behind a post's edge, and a cloud's shell, are hidden where the pixel's own ray meets the
        /// post, and not where that frame's sample happened to land. The traced grid is jittered for the
        /// upscaler and the picture the composite lays the puffs over is not: hidden by the traced pixel's
        /// depth, a puff behind a silhouette is there on the frames the sample missed the post and gone on the
        /// frames it hit it, and a shell gathered by the trace stops where the sample stops. So the composite
        /// finds the depth along the ray through the traced pixel's centre (the pixel's own on this fixture's
        /// unscaled grid) and gathers the shells there;
        /// `anUpscaledPuffBehindAPostIsHiddenWhereTheTracedCentreMeetsIt` takes the upscaled grid.
        ///
        /// The centre pixel's ray passes a quarter of a pixel right of the post's edge and a sample moved four
        /// tenths of a pixel left lands on the post: thirty-three pixels over sixty degrees are
        /// `2 * 200 * tan 30° / 33 = 7.0` units across at the post's two hundred, so the edge stands at
        /// `-0.25 * 7.0 = -1.75` and the moved sample at `-2.8`. The post faces the eye and the sun is
        /// overhead, so with no sky and no ambient it is black and the pixel holds what stands behind it, the
        /// same under both samples, and lit as it is beside the post: with the sample on the post the traced
        /// pixel under this one holds no puff to take a colour from, and the composite reads the ring around
        /// it, where an ambient-only fallback would light it by nothing. The shell is lit by its own walk.
        TEST_F(RtxVisibilityTest, whatStandsBehindAPostIsHiddenWhereThePixelsRayMeetsThePost)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };

            enum class Behind
            {
                Puff,
                Shell,
            };

            Shaders::VisibilityConstants camera = Testing::makeCameraAlong(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
            // An upright shell under an overhead sun takes its light from the cone's tilt alone, a
            // draw the two samples make at different points of it, so the cone is this test's own:
            // two degrees, which the look's `SUN_SHADOW_RADIUS` may leave.
            camera.mSun.mLimb = twoDegreeLimb();

            const auto sampledAt = [&](Behind behind, float across) {
                SceneDesc scene;
                std::array<TextureData, 1> textures{ describeTexel(white) };
                if (behind == Behind::Puff)
                {
                    const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                    const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 400.0f, 0.0f),
                        .mRadius = 60.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f } };
                    scene.addEmitter(sprites, cut, BlendKind::Over);
                }
                else
                {
                    const Index texture = scene.textures().add(VFS::Path::NormalizedView("shell.dds"));
                    const Index shell = scene.addMaterial(Material{ .mDiffuse = texture,
                        .mOpacity = 0.5f,
                        .mAlphaMode = AlphaMode::Blend,
                        .mDiffuseNeverSolid = true });
                    addQuad(scene, uprightQuadAt(60.0f, 400.0f), shell);
                }

                constexpr float edge = -1.75f;
                addQuad(scene,
                    std::array{ osg::Vec3f(-500.0f, 200.0f, -500.0f), osg::Vec3f(edge, 200.0f, -500.0f),
                        osg::Vec3f(edge, 200.0f, 500.0f), osg::Vec3f(-500.0f, 200.0f, 500.0f) });

                return shoot(scene, textures, camera, size,
                    Shot{ .mLoss = HistoryLoss::Cut, .mOffset = osg::Vec2f(across, 0.0f) })
                    .at(centre);
            };

            for (const Behind behind : { Behind::Puff, Behind::Shell })
            {
                const char* const what = behind == Behind::Puff ? "a puff" : "a shell";
                const float beside = sampledAt(behind, 0.0f);
                const float onThePost = sampledAt(behind, -0.4f);

                ASSERT_GT(beside, 0.01f) << what << " did not reach the pixel beside the post at all";
                EXPECT_NEAR(onThePost / beside, 1.0f, 0.05f) << what << " behind the edge went with the sample";
            }
        }

        /// Sprites and shells on one pixel are weighed as one walk weighs its own puffs, by the sum of their
        /// alphas, so a pixel's colour does not depend on which walk drew which puff.
        ///
        /// Four red sprites on one spot and one green shell behind them, each over a black sky: each layer
        /// alone gives its straight colour times its coverage and its transmittance in the fourth value. The
        /// sprites' alpha is `1 - T^(1/4)` apiece, so they weigh `4α`, and the shell `1 - T`. Merged, the red
        /// is the sprites' straight red times `4α / (4α + 1 - T_shell)` over the layers' joint coverage
        /// `1 - T_sprites T_shell`. At an alpha of a half each: four sprites weigh 2 and the shell 0.5, so the
        /// red keeps `0.8 * 0.96875 / 0.9375 = 0.8267` of what the sprites gave alone and the green
        /// `0.2 * 0.96875 / 0.5 = 0.3875` of the shell's; weighing by coverage, 0.9375 to 0.5, keeps 0.6739 of each.
        TEST_F(RtxVisibilityTest, spritesAndShellsOnOnePixelAreWeighedByTheirAlphas)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };

            Shaders::VisibilityConstants camera = Testing::makeCameraAlong(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

            struct Seen
            {
                float mRed = 0.0f;
                float mGreen = 0.0f;
                float mThrough = 0.0f;
            };

            const auto shot = [&](bool sprites, bool shell) {
                SceneDesc scene;
                std::array<TextureData, 1> textures{ describeTexel(white) };
                if (sprites)
                {
                    const Index cut = scene.textures().add(VFS::Path::NormalizedView("white.dds"));
                    std::array<Sprite, 4> four{};
                    for (Sprite& one : four)
                        one = Sprite{ .mPosition = osg::Vec3f(0.0f, 300.0f, 0.0f),
                            .mRadius = 60.0f,
                            .mColour = osg::Vec3f(1.0f, 0.0f, 0.0f),
                            .mAlpha = 0.5f };
                    scene.addEmitter(four, cut, BlendKind::Over);
                }
                if (shell)
                {
                    const Index texture = scene.textures().add(VFS::Path::NormalizedView("white.dds"));
                    addQuad(scene, uprightQuadAt(80.0f, 400.0f),
                        scene.addMaterial(Material{ .mDiffuse = texture,
                            .mDiffuseColour = osg::Vec3f(0.0f, 1.0f, 0.0f),
                            .mOpacity = 0.5f,
                            .mAlphaMode = AlphaMode::Blend,
                            .mDiffuseNeverSolid = true }));
                }

                const Frame frame = shoot(scene, textures, camera, size, Shot{ .mLoss = HistoryLoss::Cut });
                return Seen{
                    .mRed = frame.at(centre), .mGreen = frame.at(centre + 1), .mThrough = frame.at(centre + 3)
                };
            };

            const Seen sprites = shot(true, false);
            const Seen shell = shot(false, true);
            const Seen both = shot(true, true);
            ASSERT_GT(sprites.mRed, 0.0f);
            ASSERT_GT(shell.mGreen, 0.0f);
            ASSERT_GT(sprites.mThrough, 0.0f);
            ASSERT_LT(sprites.mThrough, 1.0f);
            ASSERT_LT(shell.mThrough, 1.0f);

            const float spriteWeight = 4.0f * (1.0f - std::pow(sprites.mThrough, 0.25f));
            const float shellWeight = 1.0f - shell.mThrough;
            const float joint = 1.0f - sprites.mThrough * shell.mThrough;
            EXPECT_NEAR(both.mThrough, sprites.mThrough * shell.mThrough, 1e-4f);

            const float red = spriteWeight / (spriteWeight + shellWeight) * joint / (1.0f - sprites.mThrough);
            const float green = shellWeight / (spriteWeight + shellWeight) * joint / (1.0f - shell.mThrough);
            EXPECT_NEAR(both.mRed / sprites.mRed, red, 1e-3f) << "the sprites were weighed by their coverage";
            EXPECT_NEAR(both.mGreen / shell.mGreen, green, 1e-3f) << "the shell was weighed by its coverage";
        }

        /// An unbinned walk that steps past a missed emitter's run draws the emitter after it whole. One
        /// frame, which has no report to size its list by, so every tile walks every sprite. Two emitters off
        /// to the side of the centre's ray with three sprites each (the first and third in the table) and two
        /// on it with one sprite each (the second and fourth): a red puff and a green one behind it. The centre
        /// is the same to the bit with the side emitters as without; a step one sprite too far would drop each
        /// puff on the ray, each being its run's only sprite.
        TEST_F(RtxVisibilityTest, anUnbinnedWalkSkipsAMissedEmittersRunAndNothingAfterIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };

            Shaders::VisibilityConstants camera = Testing::makeCameraAlong(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

            const auto shot = [&](bool aside) {
                SceneDesc scene;
                std::array<TextureData, 1> textures{ describeTexel(white) };
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("white.dds"));
                const auto addAside = [&] {
                    std::array<Sprite, 3> three{};
                    for (std::size_t at = 0; at < three.size(); ++at)
                        three[at] = Sprite{ .mPosition = osg::Vec3f(120.0f, 300.0f, 20.0f * static_cast<float>(at)),
                            .mRadius = 10.0f,
                            .mColour = osg::Vec3f(0.0f, 0.0f, 1.0f),
                            .mAlpha = 0.5f };
                    scene.addEmitter(three, cut, BlendKind::Over);
                };
                const auto addOnRay = [&](float distance, const osg::Vec3f& colour) {
                    const std::array<Sprite, 1> one{ Sprite{ .mPosition = osg::Vec3f(0.0f, distance, 0.0f),
                        .mRadius = 40.0f,
                        .mColour = colour,
                        .mAlpha = 0.5f } };
                    scene.addEmitter(one, cut, BlendKind::Over);
                };

                if (aside)
                    addAside();
                addOnRay(300.0f, osg::Vec3f(1.0f, 0.0f, 0.0f));
                if (aside)
                    addAside();
                addOnRay(500.0f, osg::Vec3f(0.0f, 1.0f, 0.0f));

                const Frame frame = shoot(scene, textures, camera, size, Shot{ .mLoss = HistoryLoss::Cut });
                return std::array<float, 4>{ frame.at(centre), frame.at(centre + 1), frame.at(centre + 2),
                    frame.at(centre + 3) };
            };

            const std::array<float, 4> onRay = shot(false);
            ASSERT_GT(onRay[0], 0.0f) << "the red puff on the ray is drawn";
            ASSERT_GT(onRay[1], 0.0f) << "the green puff behind it is drawn";
            EXPECT_EQ(shot(true), onRay) << "the emitters aside moved the centre";
        }

        /// Puffs on one pixel are drawn in order of depth, whatever order their emitter holds them in: a thin
        /// red puff in front of a dense green one shows its own red as it does alone and the green as the green
        /// alone times what the red lets through. The green is first in the emitter's array; weighed by alpha
        /// alone the dense far puff would win, at alphas of 0.3 and 0.9 the red keeping
        /// `(1 - 0.7 * 0.1) / 1.2 = 0.775` of itself.
        TEST_F(RtxVisibilityTest, puffsOnOnePixelAreDrawnInOrderOfDepth)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };

            Shaders::VisibilityConstants camera = Testing::makeCameraAlong(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;
            camera.mAmbient = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

            const Sprite farGreen{ .mPosition = osg::Vec3f(0.0f, 600.0f, 0.0f),
                .mRadius = 60.0f,
                .mColour = osg::Vec3f(0.0f, 1.0f, 0.0f),
                .mAlpha = 0.9f };
            const Sprite nearRed{ .mPosition = osg::Vec3f(0.0f, 300.0f, 0.0f),
                .mRadius = 60.0f,
                .mColour = osg::Vec3f(1.0f, 0.0f, 0.0f),
                .mAlpha = 0.3f };

            struct Seen
            {
                float mRed = 0.0f;
                float mGreen = 0.0f;
                float mThrough = 0.0f;
            };

            const auto shot = [&](std::span<const Sprite> sprites) {
                SceneDesc scene;
                std::array<TextureData, 1> textures{ describeTexel(white) };
                scene.addEmitter(
                    sprites, scene.textures().add(VFS::Path::NormalizedView("white.dds")), BlendKind::Over);
                const Frame frame = shoot(scene, textures, camera, size, Shot{ .mLoss = HistoryLoss::Cut });
                return Seen{
                    .mRed = frame.at(centre), .mGreen = frame.at(centre + 1), .mThrough = frame.at(centre + 3)
                };
            };

            const Seen red = shot(std::array{ nearRed });
            const Seen green = shot(std::array{ farGreen });
            const Seen both = shot(std::array{ farGreen, nearRed });
            ASSERT_GT(red.mRed, 0.0f);
            ASSERT_GT(green.mGreen, 0.0f);
            ASSERT_LT(red.mThrough, 1.0f);

            EXPECT_NEAR(both.mThrough, red.mThrough * green.mThrough, 1e-4f);
            EXPECT_NEAR(both.mRed / red.mRed, 1.0f, 2e-3f) << "the near puff was dimmed by the far one";
            EXPECT_NEAR(both.mGreen / green.mGreen, red.mThrough, 2e-3f)
                << "the far puff was not seen through the near";

            // **The trace's own answer holds no puff**: the composed frame under them, which a read
            // back and the digest take as the trace's whether or not anything upscales, is the black
            // sky where the shown frame holds both puffs (`readComposite`).
            std::vector<float> composed;
            mProbe.readComposite(composed);
            ASSERT_EQ(composed.size(), std::size_t{ size } * size * 4);
            EXPECT_EQ(composed[centre], 0.0f) << "the near puff was laid over the trace's own frame";
            EXPECT_EQ(composed[centre + 1], 0.0f) << "the far puff was laid over the trace's own frame";

            // **And past the layers a walk keeps apart**, where the farthest are merged: five green
            // puffs at a half behind the red, held in no order, are the five alone seen through it.
            std::array<Sprite, 5> greens{};
            for (std::size_t at = 0; at < greens.size(); ++at)
            {
                greens[at] = farGreen;
                greens[at].mPosition.y() = 500.0f + 100.0f * static_cast<float>((at * 3) % greens.size());
                greens[at].mAlpha = 0.5f;
            }
            std::array<Sprite, 6> stack{};
            std::copy(greens.begin(), greens.end(), stack.begin());
            stack.back() = nearRed;

            const Seen alone = shot(greens);
            const Seen stacked = shot(stack);
            EXPECT_NEAR(stacked.mRed / red.mRed, 1.0f, 2e-3f) << "the merge dimmed the near puff";
            EXPECT_NEAR(stacked.mGreen / alone.mGreen, red.mThrough, 2e-3f) << "the merge lost the order";
        }

        /// A shell is dimmed by the air the volume says stands in front of it, the estimator the geometry
        /// behind it and the haze laid over it both read, and not by a closed form of its own. The world's edge
        /// is the air here, which the volume's answer holds: no weather and a black sky, so the edge scatters
        /// nothing in and the frame is the shell alone times what the edge leaves of it. Level, so the edge's
        /// rise is one; the shell at 400 against an edge at 800 has crossed
        ///
        ///   (e^(0.5 / 0.125) - 1) / (e^(1 / 0.125) - 1) = 53.598 / 2979.96 = 0.017986
        ///
        /// of the ramp, which leaves `256^-0.017986 = 0.90506` of it.
        TEST_F(RtxVisibilityTest, aShellIsDimmedByTheAirTheVolumeSaysIsInFrontOfIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };

            const auto shot = [&](float edge) {
                Shaders::VisibilityConstants camera = Testing::makeCameraAlong(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbientFromSky = 0.0f;
                camera.mAmbient = osg::Vec3f();
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
                camera.mFogEdge = edge;

                SceneDesc scene;
                const std::array<TextureData, 1> textures{ describeTexel(white) };
                const Index texture = scene.textures().add(VFS::Path::NormalizedView("white.dds"));
                addQuad(scene, uprightQuadAt(80.0f, 400.0f),
                    scene.addMaterial(Material{ .mDiffuse = texture,
                        .mOpacity = 0.5f,
                        .mAlphaMode = AlphaMode::Blend,
                        .mDiffuseNeverSolid = true }));

                return shoot(scene, textures, camera, size, Shot{ .mLoss = HistoryLoss::Cut }).at(centre);
            };

            const float clear = shot(0.0f);
            const float edged = shot(800.0f);
            ASSERT_GT(clear, 0.01f) << "the shell is not lit, so this proves nothing";
            EXPECT_NEAR(edged / clear, 0.90506f, 2e-3f);
        }

        /// A sprite in front of the player's hand is looked for where the arms' ray crosses the world's
        /// picture, and not in the tile of the pixel the ray was cast for. The bin is the world camera's and
        /// the arms have an eye of their own: sixty-five pixels, the world at sixty degrees and the arms at
        /// thirty. Column 60's arms ray leaves at `u = 60.5 / 65 * 2 - 1 = 0.8615`, a slope of
        /// `0.8615 * tan 15° = 0.2309`, which the world's eye sees at `0.2309 / tan 30° = 0.400`: world pixel
        /// `1.4 / 2 * 65 = 45.5`, in tile 2, where column 60 is in tile 3. A ball of two a hundred ahead on
        /// that ray spans `2 / (100 tan 30°) * 32.5 = 1.13` pixels either side, 44.4 to 46.6, still inside tile
        /// 2 with the bin's pixel of slack. In front of a red first-person pane two hundred ahead the ball is
        /// what the hand's pixel shows (its green is the ball's, the pane having none), and without the ball
        /// the pixel is the pane.
        ///
        /// The composite also has to know the pixel is the hand's: a flag riding in a word the channel's format
        /// drops leaves it marching the world's ray, looking in the right tile along the wrong line.
        TEST_F(RtxVisibilityTest, aSpriteInFrontOfTheArmsIsLookedUpWhereTheirRayCrossesTheWorldsPicture)
        {
            constexpr std::uint32_t size = 65;
            constexpr std::size_t column = 60;
            constexpr std::size_t pixel = std::size_t{ size / 2 } * size + column;

            constexpr std::array<std::uint8_t, 4> red{ 255, 0, 0, 255 };
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 2> textures{ describeTexel(red, 0), describeTexel(white, 1) };

            const float slope
                = ((static_cast<float>(column) + 0.5f) / size * 2.0f - 1.0f) * std::tan(osg::DegreesToRadians(15.0f));

            const auto seenAt = [&](bool sprited) {
                SceneDesc scene;
                const Index pane = scene.textures().add(VFS::Path::NormalizedView("red.dds"));
                const Index puff = scene.textures().add(VFS::Path::NormalizedView("white.dds"));

                const float across = 200.0f * slope;
                const std::array corners = uprightQuadAt(10.0f, 100.0f, osg::Vec2f(across, 0.0f));
                scene.addInstance(
                    MeshInstance{ .mMesh = scene.addMesh(MeshArrays{
                                      .mPositions = corners, .mTexCoords = sQuadUv, .mIndices = sQuadIndices }),
                        .mMaterial = scene.addMaterial(Material{ .mDiffuse = pane }),
                        .mClass = InstanceClass::FirstPerson });

                if (sprited)
                {
                    const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(100.0f * slope, 0.0f, 0.0f),
                        .mRadius = 2.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f } };
                    scene.addEmitter(sprites, puff, BlendKind::Over);
                }

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
                camera.mEyes.mArms = cameraAtFieldOfView(camera.mEyes.mWorld, 30.0f).value();
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mAmbient = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mAmbientFromSky = 0.0f;

                // The surfaces as their albedo, so the pane is red and nothing else; the ball is lit
                // by the fill, which the composite puts over the albedo as it puts it over a frame.
                // The last of three frames: the first bin has no report to size its list by, walks
                // every sprite unbinned, and would find the ball from any tile.
                const Frame frame = shoot(scene, textures, camera, size,
                    Shot{ .mFrames = 3, .mAverage = false, .mShow = SurfaceView::Albedo });

                return std::array<float, 2>{ frame.at(pixel * 4), frame.at(pixel * 4 + 1) };
            };

            const std::array<float, 2> bare = seenAt(false);
            EXPECT_EQ(bare[0], 1.0f) << "the hand's pixel is not the pane";
            EXPECT_EQ(bare[1], 0.0f);
            EXPECT_GT(seenAt(true)[1], 0.25f) << "the sprite in front of the hand was not found";
        }

        /// A puff behind a see-through arm is laid down by what the arm lets through. The rasterizer draws the
        /// arms after every particle, so the smoke behind a faded hand is seen through the hand as the room is.
        /// A white ball a hundred units ahead over a black sky and a black first-person pane twenty ahead
        /// across the picture, faded to a quarter: the pane lets `0.75` through, held in a byte as `191 / 255`,
        /// and over a world with no light the puff's own light `P` is all there is, so the pixel is
        /// `191 / 255 * P`, the pane adding nothing. The composite's weighing is exact there, since the arm is
        /// the colour of what it stands over; laid over the frame whole, the puff would come through at `P`.
        TEST_F(RtxVisibilityTest, aPuffBehindASeeThroughArmIsLaidDownByWhatTheArmLetsThrough)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> textures{ describeTexel(white, 0) };

            const auto seenWith = [&](bool sprited, bool armed) {
                SceneDesc scene;
                const Index puff = scene.textures().add(VFS::Path::NormalizedView("white.dds"));

                // Behind the eye, so the scene holds something and the picture is the sky.
                addQuad(scene, uprightQuadAt(50.0f, -1000.0f));

                if (armed)
                    scene.addInstance(MeshInstance{ .mMesh = addQuadMesh(scene, uprightQuadAt(50.0f, -80.0f)),
                        .mMaterial = scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f() }),
                        .mOpacity = 0.25f,
                        .mClass = InstanceClass::FirstPerson });

                if (sprited)
                {
                    const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(),
                        .mRadius = 10.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f } };
                    scene.addEmitter(sprites, puff, BlendKind::Over);
                }

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mAmbient = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mAmbientFromSky = 0.0f;

                // The last of three frames, for the reason the arms' test above gives.
                const Frame frame = shoot(scene, textures, camera, size, Shot{ .mFrames = 3, .mAverage = false });
                return frame.at(centre + 1);
            };

            const float lit = seenWith(true, false);
            ASSERT_GT(lit, 0.01f) << "the puff was not lit";
            ASSERT_EQ(seenWith(false, true), 0.0f) << "the pane added light of its own";
            EXPECT_NEAR(seenWith(true, true), 191.0f / 255.0f * lit, 1e-4f)
                << "the puff behind the arm was not laid down by what the arm let through";
        }

        /// A room's fill reaches a puff from every side, and what stands near takes it away. A puff is a point
        /// in a medium with no face to turn away from, so what it sees of an `AMBI` fill is a question about
        /// the whole sphere and not a hemisphere; answered by a puff's own thickness alone, smoke under a table
        /// would be as bright as smoke in the middle of the floor.
        ///
        /// The law is exactly linear, which makes this an assertion and not a comparison: a sheet `h` above
        /// and another `h` below block every direction that reaches them inside `ROOM_FILL_REACH`, that is
        /// `|d.z| >= h / reach`, and a uniform sphere draw puts `d.z` evenly on `[-1, 1]`, so the share left is
        /// `h / reach` and nothing else. At 70 and 105 units of a 140-unit reach that is a half and three
        /// quarters of the open fill. Drawing the cosine about the up instead would leave `(h / reach)^2`, a
        /// quarter and nine sixteenths, 0.25 and 0.19 from the law, over twice the tolerance below.
        ///
        /// Not nearer, because the puff is lit out of the fog's columns: a column is eight pixels square, forty
        /// units at the puff, and a sheet 35 units off cuts through the columns the middle row reads, so the
        /// field holds the column's mean over its block and not the point's (0.35 measured over 1024 frames
        /// for a law of 0.25). The tolerance is the field's own noise at 128 frames: four runs from four
        /// starting frames came within 0.05 of the law at both heights.
        ///
        /// The sprite is opaque with no lighting bake, so a pixel shows the fill alone. Only the middle row is
        /// read: those rays are level, so they meet neither sheet and cut the same chord in all three scenes.
        /// A cloud's shell in a frame with no sprite in it follows the same law, since the air asks what a
        /// point sees of the fill wherever a puff of either kind can read it (`mPuffsInFrame`).
        TEST_F(RtxVisibilityTest, aRoomsFillReachesAPuffFromEverySideAndWhatIsNearTakesItAway)
        {
            constexpr std::uint32_t size = 33;
            constexpr float reach = 140.0f;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ describeTexel(white) };

            const auto boxedAt = [&](float half, bool shell, std::uint32_t frames) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                if (shell)
                {
                    addQuad(scene, uprightQuadAt(40.0f, 0.0f),
                        scene.addMaterial(Material{ .mDiffuse = cut,
                            .mOpacity = 0.5f,
                            .mAlphaMode = AlphaMode::Blend,
                            .mDiffuseNeverSolid = true }));
                }
                else
                {
                    const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                        .mRadius = 40.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f } };
                    scene.addEmitter(sprites, cut, BlendKind::Over);
                }

                // Nothing at all where the fill is whole, rather than sheets moved out of reach: a
                // scene with no geometry is the one case where the answer cannot be the geometry's.
                if (half > 0.0f)
                    for (const float z : { half, -half })
                        addQuad(scene, sheetAt(4000.0f, z));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -reach, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);

                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mAmbient = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mAmbientFromSky = 0.0f;

                const Frame frame = shoot(scene, puff, camera, size, { .mFrames = frames });

                // The middle row, whose rays leave the eye level and stay level.
                float sum = 0.0f;
                for (std::uint32_t x = 0; x < size; ++x)
                    sum += frame.at((std::size_t{ size / 2 } * size + x) * 4);

                return sum;
            };

            // One frame: with nothing to stand in the way, every draw of the fill reaches the puff,
            // and the first frame's open fill is the 128th's within a part in ten million.
            const float open = boxedAt(0.0f, false, 1);
            ASSERT_GT(open, 0.01f) << "the fill did not light the puff at all";

            EXPECT_NEAR(boxedAt(70.0f, false, 128) / open, 70.0f / reach, 0.08f) << "half of the sphere is left";
            EXPECT_NEAR(boxedAt(105.0f, false, 128) / open, 105.0f / reach, 0.08f)
                << "and three quarters in a taller room";

            // Sixteen frames, which hold the shell to the law within a hundredth: the open fill it read
            // without the flag is a whole half away.
            const float shellOpen = boxedAt(0.0f, true, 16);
            ASSERT_GT(shellOpen, 0.01f) << "the fill did not light the shell at all";
            EXPECT_NEAR(boxedAt(70.0f, true, 16) / shellOpen, 70.0f / reach, 0.08f)
                << "half of the sphere is left for a shell with no sprite in the frame";
        }

        /// A lamp lights a puff by the card's convention, as the sun and the fill do: a white puff under a
        /// lamp that brings irradiance `E` reads as it does under a fill of `E / pi`. The lamps' field holds
        /// the air's share, `E / 4 pi`, which the air's isotropic phase asks for; read unchanged, it would
        /// light a puff four times too dimly beside the card a surface is.
        ///
        /// By hand: the lamp stands 200 units over the puff with a reach of 1000 and the eye's ray passes
        /// under it at its closest approach, so the irradiance at the puff is
        /// `40000 * (1 - 0.2^4)^2 / (200^2 + 1) = 40000 * 0.996801 / 40001 = 0.996776` and the matching fill
        /// is `0.996776 / pi = 0.317285`. The froxel averages the falloff over its stretch of the ray, which a
        /// puff 40 across holds to within a few per cent.
        TEST_F(RtxVisibilityTest, aLampLightsAPuffAsItLightsACardAndAsTheFillDoes)
        {
            constexpr std::uint32_t size = 33;
            constexpr float irradiance = 40000.0f * 0.996801f / 40001.0f;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ describeTexel(white) };

            const auto render = [&](bool lamp, float fill) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 40.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over);
                if (lamp)
                    scene.addLight(Light{
                        .mPosition = osg::Vec3f(0.0f, 0.0f, 200.0f),
                        .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                        .mReach = 1000.0f,
                    });

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -140.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mAmbient = osg::Vec3f(fill, fill, fill);
                camera.mAmbientFromSky = 0.0f;

                const Frame frame = shoot(scene, puff, camera, size, { .mFrames = 128 });

                float sum = 0.0f;
                for (std::uint32_t x = 0; x < size; ++x)
                    sum += frame.at((std::size_t{ size / 2 } * size + x) * 4);
                return sum;
            };

            const float filled = render(false, irradiance * Shaders::INV_PI);
            const float lit = render(true, 0.0f);
            ASSERT_GT(filled, 0.01f) << "the fill did not light the puff at all";
            EXPECT_NEAR(lit / filled, 1.0f, 0.05f) << "a lamp's puff against the fill of its irradiance over pi";
        }

        /// The alpha every sprite test below cuts its sprite from: half, so that what it hides and
        /// what it lets through are the same size and neither can pass by being nought or one.
        constexpr float sHalfAlpha = 128.0f / 255.0f;

        /// A sprite is a ball the ray crosses, so a floor cuts its chord rather than clipping its disc. The eye
        /// looks along a line through the sprite's centre. Hanging in the open the whole chord is seen and the
        /// sprite hides exactly what was painted, `128/255`, leaving `0.49804` of the floor behind it. Centred
        /// on the floor, the ray meets the floor at the centre and sees half the chord, leaving
        /// `0.49804^0.5 = 0.70572`. The puff is unlit (no ambient, sun or lamp), so the pixel shows the floor
        /// through it and nothing else, and the floor alone is measured in the same pose to divide by.
        TEST_F(RtxVisibilityTest, aSpriteIsAChordAndAFloorCutsItInHalf)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> half{ 255, 255, 255, 128 };
            const std::array<TextureData, 1> puff{ describeTexel(half) };

            const auto through = [&](float height, bool sprited) {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));

                if (sprited)
                {
                    const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                    const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, height),
                        .mRadius = 60.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f } };
                    scene.addEmitter(sprites, cut, BlendKind::Over);
                }

                // Forty up over four hundred along, so the centre ray runs down through the
                // sprite's centre and on to the floor, near or far.
                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -400.0f, height + 40.0f),
                    osg::Vec3f(0.0f, 0.0f, height), 60.0f, size, size, 100000.0f);

                // An even sky lights the floor exactly and the puff not at all: a puff is lit by the
                // ambient, the sun and the lamps, and none of those is here.
                camera.mSkyHorizon = osg::Vec3f(0.6f, 0.6f, 0.6f);
                camera.mSkyZenith = camera.mSkyHorizon;
                camera.mAmbientFromSky = 1.0f;
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();

                const Frame frame = shoot(
                    scene, sprited ? std::span<const TextureData>(puff) : std::span<const TextureData>(), camera, size);

                return frame.at(centre);
            };

            const float openFloor = through(200.0f, false);
            const float buriedFloor = through(0.0f, false);
            ASSERT_GT(openFloor, 0.01f) << "the floor is not lit";
            ASSERT_GT(buriedFloor, 0.01f) << "the floor is not lit";

            EXPECT_NEAR(through(200.0f, true) / openFloor, 1.0f - sHalfAlpha, 0.005f)
                << "a whole chord hides exactly what was painted";
            EXPECT_NEAR(through(0.0f, true) / buriedFloor, std::sqrt(1.0f - sHalfAlpha), 0.005f)
                << "half a chord lets through the square root";
        }

        /// Two flames in one place add less than twice one, because a flame absorbs what it emits. A texel of
        /// alpha `128/255 = 0.50196` adds that share of `SUNLIT_WHITE` on its own; two of them screen,
        /// `1 - 0.49804^2 = 0.75196` of it, where a sum would reach `1.00392`: the smooth form of the limit the
        /// original's framebuffer clamped at one. Under `ONE, ONE` the same texel adds whole, its alpha unread,
        /// as a surface that adds whole does: all of `SUNLIT_WHITE` the walk lets one sprite hide,
        /// `SPRITE_ALPHA_LIMIT` of it, where `SRC_ALPHA, ONE` adds half.
        TEST_F(RtxVisibilityTest, flamesSaturateWhereTheOriginalClamped)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> half{ 255, 255, 255, 128 };
            const std::array<TextureData, 1> flame{ describeTexel(half) };

            const auto glowing = [&](std::size_t count, BlendKind blend = BlendKind::Add) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::vector<Sprite> flames(count,
                    Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                        .mRadius = 60.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f });
                scene.addEmitter(flames, cut, blend);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();

                const Frame frame = shoot(scene, flame, camera, size);

                return frame.at(centre);
            };

            EXPECT_NEAR(glowing(1), Shaders::SUNLIT_WHITE * sHalfAlpha, 0.01f) << "one adds what it painted";
            EXPECT_NEAR(glowing(2), Shaders::SUNLIT_WHITE * (1.0f - (1.0f - sHalfAlpha) * (1.0f - sHalfAlpha)), 0.01f)
                << "two screen rather than sum";
            EXPECT_NEAR(glowing(1, BlendKind::AddWhole), Shaders::SUNLIT_WHITE * Shaders::SPRITE_ALPHA_LIMIT, 1e-4f)
                << "a flame that adds whole read its texture's alpha";
        }

        /// A streak hangs on the axis its own particle carries, and the trace draws it leaning. The axis is the
        /// sprite's and not the emitter's: `osgParticle` turns a quad by the angle the particle holds and
        /// `Weather::RainShooter` leans every drop it fires into the wind that way, so a march that read the
        /// emitter's authored axis would draw a storm the rasterizer leans as falling straight down.
        ///
        /// A streak 120 long and a quarter of that wide, seen face-on from 400 units off through sixty degrees,
        /// so half the frame is `400 * tan 30 = 230.94` units at the sprite's plane. Upright it covers
        /// `|x| < 30` about a fall of `|z| < 120`; leant forty-five degrees it covers that rectangle turned, its
        /// length running up and to the left. A point eighty above the middle is inside the first and
        /// `80 * sin 45 = 56.6` from the second's axis, well outside its thirty, and a point eighty up the
        /// leant axis is the other way about. Additive, so a pixel holds the coverage alone and nothing has to
        /// light it.
        TEST_F(RtxVisibilityTest, aStreakLeansWithTheAxisItsOwnParticleCarries)
        {
            constexpr std::uint32_t size = 65;
            constexpr float radius = 120.0f;
            constexpr float width = 0.25f;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> drop{ describeTexel(white) };

            const auto drawn = [&](const osg::Vec3f& axis) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = radius,
                    .mAxis = axis,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Add, width);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();

                const Frame frame = shoot(scene, drop, camera, size);

                return frame.mRadiance;
            };

            // Where a point of the sprite's own plane lands, as the first value of its pixel. The
            // middle of pixel `n` sits at `(n + 0.5) / size * 2 - 1` of half the frame, and the
            // frame's up is the screen's down.
            constexpr float half = 230.9401f;
            const auto pixelAt = [](float x, float z) {
                const auto column = static_cast<std::uint32_t>(std::lround((x / half + 1.0f) * 0.5f * size - 0.5f));
                const auto row = static_cast<std::uint32_t>(std::lround((-z / half + 1.0f) * 0.5f * size - 0.5f));

                return (std::size_t{ row } * size + column) * 4;
            };

            constexpr float lean = 0.70710678f;
            const std::size_t aboveTheMiddle = pixelAt(0.0f, 80.0f);
            const std::size_t upTheLean = pixelAt(-80.0f * lean, 80.0f * lean);

            const std::vector<float> upright = drawn(osg::Vec3f(0.0f, 0.0f, -1.0f));
            const std::vector<float> leaning = drawn(osg::Vec3f(lean, 0.0f, -lean));

            ASSERT_EQ(upright.size(), std::size_t{ size } * size * 4);

            EXPECT_GT(upright[aboveTheMiddle], 0.01f) << "a streak standing up did not cover its own fall";
            EXPECT_EQ(upright[upTheLean], 0.0f) << "a streak standing up covered the diagonal";

            EXPECT_EQ(leaning[aboveTheMiddle], 0.0f) << "a leaning streak still covered the upright fall";
            EXPECT_GT(leaning[upTheLean], 0.01f) << "the lean the particle carries never reached the picture";

            // A turn cannot change the shape it turned, so the leaning streak is measured along its new axis and
            // across it: 120 long and 30 wide, exactly as it stood. A hundred up the lean is inside its fall and
            // a hundred and forty is past the end; twenty across, sixty up, is inside its width and forty-five is
            // outside, each several pixels clear of the edge it names.
            const auto alongLean
                = [&](float along, float across) { return pixelAt(lean * (across - along), lean * (along + across)); };

            EXPECT_GT(leaning[alongLean(100.0f, 0.0f)], 0.01f) << "the streak fell short of its own length";
            EXPECT_EQ(leaning[alongLean(140.0f, 0.0f)], 0.0f) << "the streak ran past its own length";
            EXPECT_GT(leaning[alongLean(60.0f, 20.0f)], 0.01f) << "the streak was narrower than its width";
            EXPECT_EQ(leaning[alongLean(60.0f, 45.0f)], 0.0f) << "the streak was wider than its width";
        }

        /// A streak takes the sun broadside and takes nothing along its own axis. The march draws an oriented
        /// quad as a cylinder, so it is lit as one: its width is swung about its axis to meet the ray, a
        /// cylinder's silhouette, and such a body presents `sin` of the angle off its axis to whatever lights
        /// it. A streak that took a full card's worth of the sun whichever way it hung would light rain at noon
        /// as brightly as rain at dawn. One sun, straight up, and the streak turned under it, which keeps the
        /// coverage, level and air the same in the three frames: lying across the sun it takes the whole of
        /// the broadside; thirty degrees off the vertical `sin 30`, half of it; straight up and down none.
        TEST_F(RtxVisibilityTest, aStreakTakesTheSunBroadsideAndNothingAlongItsOwnAxis)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> drop{ describeTexel(white) };

            const auto lit = [&](const osg::Vec3f& axis) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mAxis = axis,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over, 0.25f);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // The sun and nothing else, so what a pixel holds is the shape's own share of it.
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

                const Frame frame = shoot(scene, drop, camera, size);

                return frame.at(centre);
            };

            constexpr float lean = 0.8660254f;
            const float across = lit(osg::Vec3f(1.0f, 0.0f, 0.0f));
            const float leaning = lit(osg::Vec3f(0.5f, 0.0f, -lean));
            const float upright = lit(osg::Vec3f(0.0f, 0.0f, -1.0f));

            ASSERT_GT(across, 0.01f) << "a streak lying across the sun was not lit at all";

            EXPECT_NEAR(leaning / across, 0.5f, 0.02f) << "thirty degrees off the sun is sin 30 of it";
            EXPECT_NEAR(upright, 0.0f, across * 0.02f) << "a streak pointing at the sun has no side to turn to it";
        }

        /// The level a streak is read at comes from the axis its texels are densest along. A rain streak
        /// carries its texture at two densities, the quad being a fraction as wide as it is long with the
        /// texture stretched over the whole of it; a level chosen from the length alone reads the width
        /// sharper than the ray can carry, the drop aliasing into a hard mark rather than fading.
        ///
        /// Two levels painted different colours, so which one was read is in the picture. The texture is four
        /// square and the sprite sixty across. A quad as wide as it is long spreads four texels over 120 units
        /// either way, one every thirty; a quad a quarter as wide spreads the same four over thirty, one every
        /// seven and a half. The pixel's cone at eight hundred units is `atan(2 tan 30 / 33) * 800 = 27.98`
        /// units, under one texel of the first and 3.73 of the second: the first reads level nought and the
        /// second the level below it.
        ///
        /// And the length as the ray sees it, foreshortened: a cone across the ray covers `1 / sin θ` of an
        /// axis θ off the ray, which is rain seen from above. The square quad with its axis leaned 15° off the
        /// ray covers `27.98 / 30 / sin 15° = 3.60` texels along it and reads the level below; taken face-on,
        /// 0.93 would read level nought and shimmer.
        TEST_F(RtxVisibilityTest, theLevelAStreakIsReadAtComesFromTheAxisItsTexelsAreDensestAlong)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            // Level nought is white and level one is red, so the green channel says which was read.
            TestTexture layered;
            for (std::uint32_t texel = 0; texel < 4 * 4; ++texel)
                for (const std::uint8_t byte : { 255, 255, 255, 255 })
                    layered.mBytes.push_back(byte);

            for (std::uint32_t texel = 0; texel < 2 * 2; ++texel)
                for (const std::uint8_t byte : { 255, 0, 0, 255 })
                    layered.mBytes.push_back(byte);

            layered.mLevels.push_back(MipLevel{ .mOffset = 0, .mWidth = 4, .mHeight = 4 });
            layered.mLevels.push_back(MipLevel{ .mOffset = 4 * 4 * 4, .mWidth = 2, .mHeight = 2 });
            layered.describe(4, 4, "layered.dds");

            const auto green = [&](float width, const osg::Vec3f& axis) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mAxis = axis,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };

                // Additive, so what a pixel holds is the texel it read and nothing has to light it.
                scene.addEmitter(sprites, cut, BlendKind::Add, width);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -800.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();

                const Frame frame = shoot(scene, std::span(&layered.mData, 1), camera, size);

                return frame.at(centre + 1) / std::max(frame.at(centre), 1.0e-6f);
            };

            const osg::Vec3f upright(0.0f, 0.0f, -1.0f);
            EXPECT_NEAR(green(1.0f, upright), 1.0f, 0.05f) << "a square quad read the level below its own";
            EXPECT_NEAR(green(0.25f, upright), 0.0f, 0.05f) << "a streak read its length rather than its width";

            const float leaned = osg::DegreesToRadians(15.0f);
            EXPECT_NEAR(green(1.0f, osg::Vec3f(0.0f, std::cos(leaned), -std::sin(leaned))), 0.0f, 0.05f)
                << "a streak along the ray read its length as though it stood across it";
        }

        /// A puff is lit from the side the light is on, and by what its own texture lets through. A sprite
        /// facing an eye that looks along +Y, lit by a sun of four and nothing else, so its centre pixel is
        /// `4 / pi` per unit of albedo, times what the puff lets through, times its half alpha: `0.6391` for a
        /// puff nothing shadows. At the centre the ball's normal is toward the eye, so a sun to the side is at
        /// the mean and the bake alone decides: a sun from the screen's right is `+u`, from above `+v`, and a
        /// channel of nought there puts the sun out while the mirror channel is left alone.
        ///
        /// The ball has a side: a sun behind the eye lights the near side at `1 + SPRITE_WRAP` through a front
        /// nothing shadows; one behind the sprite lights it at `1 - SPRITE_WRAP` through the whole of the
        /// texel's thickness, `1 - alpha`.
        ///
        /// The sun is thrown forward: Henyey-Greenstein at `g = 0.6` against the even share is
        /// `(1 - g^2) / (1 + g^2 - 2 g cos)^1.5 = 0.64 / (1.36 - 1.2 cos)^1.5`: from the side
        /// `0.64 / 1.36^1.5 = 0.40353`, from behind the eye `0.64 / 2.56^1.5 = 0.15625`, from behind the sprite
        /// `0.64 / 0.16^1.5 = 10`. The side cases carry the first and the two sides of the ball the other two,
        /// which is what makes them differ by sixty-four and not by six. A bake that stands in is no bake: the
        /// puff is lit as a card, where the stand-in's grey would shut the sun to a quarter.
        TEST_F(RtxVisibilityTest, aPuffIsLitByItsSideAndByWhatItsTextureLetsThrough)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> half{ 255, 255, 255, 128 };

            const auto lit = [&](const osg::Vec3f& sun, std::array<std::uint8_t, 4> through, bool standsIn = false) {
                std::array<TextureData, 2> textures{ describeTexel(half), describeTexel(through) };
                if (standsIn)
                    textures[1].mSource = TextureSource::StandIn;

                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const Index bake = scene.textures().addSpriteLight(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Over, 0.0f, bake);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mAmbientFromSky = 1.0f;
                camera.mSun = Shaders::sunSource(sun, osg::Vec3f(4.0f, 4.0f, 4.0f));

                const Frame frame = shoot(scene, textures, camera, size);

                return frame.at(centre);
            };

            const float card = 4.0f * Shaders::INV_PI * sHalfAlpha;
            constexpr float sideways = 0.40353f;
            constexpr float backward = 0.15625f;
            constexpr float forward = 10.0f;
            constexpr std::array<std::uint8_t, 4> clear{ 255, 255, 255, 255 };

            EXPECT_NEAR(lit(osg::Vec3f(1.0f, 0.0f, 0.0f), clear), card * sideways, 0.005f)
                << "a card's worth from the side, thrown";
            EXPECT_NEAR(lit(osg::Vec3f(1.0f, 0.0f, 0.0f), { 0, 255, 255, 255 }), 0.0f, 0.005f)
                << "the sun from +u, and +u shut";
            EXPECT_NEAR(lit(osg::Vec3f(1.0f, 0.0f, 0.0f), { 0, 255, 255, 255 }, true), card * sideways, 0.005f)
                << "a bake that stands in, read as the card it is without one";
            EXPECT_NEAR(lit(osg::Vec3f(-1.0f, 0.0f, 0.0f), { 0, 255, 255, 255 }), card * sideways, 0.005f)
                << "the sun from -u, which +u does not shut";
            EXPECT_NEAR(lit(osg::Vec3f(0.0f, 0.0f, 1.0f), { 255, 255, 0, 255 }), 0.0f, 0.005f)
                << "the sun from above is +v, and +v shut";

            EXPECT_NEAR(
                lit(osg::Vec3f(0.0f, -1.0f, 0.0f), clear), card * (1.0f + Shaders::SPRITE_WRAP) * backward, 0.005f)
                << "the near side, through the front, thrown away from the eye";
            EXPECT_NEAR(lit(osg::Vec3f(0.0f, 1.0f, 0.0f), clear),
                card * (1.0f - Shaders::SPRITE_WRAP) * (1.0f - sHalfAlpha) * forward, 0.02f)
                << "the far side, through the thickness, thrown toward the eye";
        }

        /// A puff in the shade of its own emitter is thinned by what one layer of its texture hides. Two puffs
        /// of one emitter, the second a hundred units toward a sun from the side and sixty in radius, so the
        /// first's path to the sun runs through it and the eye's ray to the first does not. `spriteshade.h`
        /// counts one whole layer and the shader thins the sun by the texture's mean alpha, its one texel
        /// `128/255`, to `0.49804` of the card's worth from the side. Nothing stands over either, so the sky is
        /// untouched, and the ambient is nought.
        ///
        /// The mean, and not the middle of the coarsest level: a puff of sixteen texels square opaque over its
        /// middle four by four, with its file's chain stopping at eight by eight as Morrowind's do, has a mean
        /// alpha of a sixteenth, so one layer leaves `15/16` of the sun; read at the coarsest level's middle it
        /// is all alpha and leaves almost nothing.
        TEST_F(RtxVisibilityTest, aPuffInTheShadeOfItsOwnEmitterIsThinnedByOneLayer)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            constexpr std::array<std::uint8_t, 4> half{ 255, 255, 255, 128 };
            const std::array<TextureData, 1> halfPuff{ describeTexel(half) };

            Testing::TestTexture radial;
            for (const std::uint32_t side : { 16u, 8u })
            {
                radial.mLevels.push_back(MipLevel{ static_cast<std::uint32_t>(radial.mBytes.size()), side, side });
                for (std::uint32_t y = 0; y < side; ++y)
                    for (std::uint32_t x = 0; x < side; ++x)
                    {
                        const bool middle
                            = x >= side * 3 / 8 && x < side * 5 / 8 && y >= side * 3 / 8 && y < side * 5 / 8;
                        radial.mBytes.insert(radial.mBytes.end(), 3, std::uint8_t{ 255 });
                        radial.mBytes.push_back(middle ? 255 : 0);
                    }
            }
            radial.describe(16, 16, "radial");
            const std::array<TextureData, 1> radialPuff{ radial.mData };

            const auto lit = [&](bool shaded, std::span<const TextureData> puff) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                std::vector<Sprite> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 60.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                if (shaded)
                    sprites.push_back(Sprite{ .mPosition = osg::Vec3f(100.0f, 0.0f, 0.0f),
                        .mRadius = 60.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f });
                scene.addEmitter(sprites, cut, BlendKind::Over);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mAmbientFromSky = 1.0f;
                camera.mSun = Shaders::sunSource(osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));

                const Frame frame = shoot(scene, puff, camera, size);

                return frame.at(centre);
            };

            const float alone = lit(false, halfPuff);
            ASSERT_GT(alone, 0.1f) << "the sun did not reach the puff at all";
            EXPECT_NEAR(lit(true, halfPuff) / alone, 1.0f - sHalfAlpha, 0.01f) << "one layer of a half-alpha texture";

            const float radialAlone = lit(false, radialPuff);
            ASSERT_GT(radialAlone, 0.1f) << "the sun did not reach the radial puff at all";
            EXPECT_NEAR(lit(true, radialPuff) / radialAlone, 15.0f / 16.0f, 0.01f)
                << "one layer of a puff whose mean alpha is a sixteenth";
        }

        /// A drop under a roof is not drawn, and a hearth's smoke under the same roof is. The rasterizer's
        /// occluder, as a ray: `PrecipitationOccluder` discards every drop with a static over it inside its
        /// box, and `spriteshelter.rgen` traces every falling sprite straight up to the top of that box and
        /// zeroes the one that meets a surface. Two drops of one falling emitter, a lid over the left one
        /// alone, and a sun from behind the eye so the lid shadows neither: with shelter in the frame the left
        /// pixel shows nothing and the right one is what it was, and with none in the frame both are drawn. The
        /// same two under a lid, from an emitter that does not fall, stay where they are.
        ///
        /// A frame with no shelter height in it and not a flag: the game says nought where what falls is ash,
        /// and a picture inside the interface says nought because it has no world over it. Nought keeps the
        /// launch off nearly every frame.
        TEST_F(RtxVisibilityTest, aRoofKeepsTheRainOffAndLeavesTheSmoke)
        {
            constexpr std::uint32_t size = 33;

            // At thirty degrees a thousand units out the frame is 536 units wide, so a drop a
            // hundred units off the axis lands six pixels from the middle, and its radius of forty
            // covers two and a half. The middle row, since the drops stand at the eye's height.
            constexpr std::size_t row = size / 2;
            constexpr std::size_t left = (row * size + 10) * 4;
            constexpr std::size_t right = (row * size + 22) * 4;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ describeTexel(white) };

            struct Read
            {
                float mLeft;
                float mRight;
            };

            const auto shown = [&](bool falls, float shelter) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 2> drops{
                    Sprite{ .mPosition = osg::Vec3f(-100.0f, 0.0f, 0.0f),
                        .mRadius = 40.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f },
                    Sprite{ .mPosition = osg::Vec3f(100.0f, 0.0f, 0.0f),
                        .mRadius = 40.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 1.0f },
                };
                scene.addEmitter(drops, cut, BlendKind::Over, 0.0f, sNoIndex, falls);

                // A lid two hundred wide over the left drop alone, three hundred up.
                addQuad(scene, sheetAt(100.0f, 300.0f), std::nullopt, osg::Matrixf::translate(-100.0f, 0.0f, 0.0f));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -1000.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 30.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mAmbientFromSky = 1.0f;
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, -1.0f, 0.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
                camera.mShelterHeight = shelter;

                const Frame frame = shoot(scene, puff, camera, size);

                return Read{ frame.at(left), frame.at(right) };
            };

            const Read open = shown(true, 0.0f);
            ASSERT_GT(open.mLeft, 0.1f) << "the left drop was not drawn at all";
            ASSERT_GT(open.mRight, 0.1f) << "the right drop was not drawn at all";

            const Read sheltered = shown(true, 2000.0f);
            EXPECT_EQ(sheltered.mLeft, 0.0f) << "the roof did not keep the rain off";
            EXPECT_EQ(sheltered.mRight, open.mRight) << "the shelter reached a drop with nothing over it";

            const Read smoke = shown(false, 2000.0f);
            EXPECT_EQ(smoke.mLeft, open.mLeft) << "the roof took a hearth's smoke";
            EXPECT_EQ(smoke.mRight, open.mRight);
        }
    }
}
