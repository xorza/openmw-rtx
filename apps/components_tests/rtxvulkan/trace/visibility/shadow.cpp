#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/environment/moonbuilder.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/visibility.h>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// The floor the sun lights: level at nought, or a hundred units under water whose surface is
        /// at nought, which `camera` is told — so the sun's bit is what the water's refraction found.
        SceneDesc floorOf(bool flooded, Shaders::VisibilityConstants& camera)
        {
            if (flooded)
            {
                camera.mWaterLevel = 0.0f;
                return makeFlooded(4000.0f, 100.0f);
            }

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            return scene;
        }

        /// **A floor every ray reaches, and a floor no ray reaches, come back exact.** Every tile of
        /// either is cleared — each receiver and its neighbours alike — so nothing is filtered, and
        /// the composite multiplies the sun by exactly one or exactly nought. The frame the shadow
        /// denoiser composes is then the frame the trace composes with the rays' own bits, value for
        /// value. The bits themselves are read off `CHANNEL_SHADOWED`: all one under the open sky, and
        /// all nought under the roof, with the sun's light standing behind both. **And so for a
        /// flooded floor**, whose bit is what the water's refraction found: the reflection finds
        /// the black sky and adds no sun, so the bed's bit is the only one to keep.
        TEST_F(RtxVisibilityTest, theShadowDenoiserLeavesAFloorWhollyLitOrWhollyShadowedExact)
        {
            constexpr std::uint32_t size = 64;

            for (const bool flooded : { false, true })
                for (const bool roofed : { false, true })
                {
                    SCOPED_TRACE(flooded ? "flooded" : "dry");
                    SCOPED_TRACE(roofed ? "roofed" : "open");
                    Shaders::VisibilityConstants camera = overheadSun(size);
                    SceneDesc scene = floorOf(flooded, camera);
                    if (roofed)
                        addQuad(scene, roofOver(-4000.0f, 4000.0f, 2000.0f));

                    const Frame raw
                        = shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });

                    std::vector<float> sunlit;
                    mRenderer.readChannel(Channel::Shadowed, sunlit);
                    ASSERT_EQ(sunlit.size(), std::size_t{ size } * size * 4);
                    for (std::size_t value = 0; value < sunlit.size(); value += 4)
                    {
                        ASSERT_GT(sunlit[value], 0.0f) << "the sun lights every pixel of the floor, roofed or not";
                        ASSERT_EQ(sunlit[value + 3], roofed ? 0.0f : 1.0f) << "pixel " << value / 4;
                    }

                    const Frame filtered = shoot(scene, {}, camera, size,
                        { .mSea = SeaState{ .mSignificantHeight = 0.0f }, .mFilter = true, .mLoss = HistoryLoss::Cut });
                    EXPECT_EQ(filtered.mRadiance, raw.mRadiance);
                }

            // **And a picture with shadows off is open to the sky under the water as over it.** A
            // slab in the water fifty units over the flooded floor, over half of it, under a sun 45
            // degrees off the zenith from that side: bent to 32 degrees under the surface, its shadow
            // runs `50 tan 32°` = 31 units past the slab's edge onto the bed the eye sees. The leg
            // under the surface asked the occluders itself, where the leg over it asked the flag.
            {
                SCOPED_TRACE("a slab in the water");
                Shaders::VisibilityConstants camera = overheadSun(size);
                const float zenith = osg::DegreesToRadians(45.0f);
                camera.mSun = Shaders::sunSource(
                    osg::Vec3f(-std::sin(zenith), 0.0f, std::cos(zenith)), osg::Vec3f(2.0f, 2.0f, 2.0f));
                SceneDesc scene = floorOf(true, camera);
                addQuad(scene, roofOver(-4000.0f, 0.0f, -50.0f));
                for (const std::uint32_t noShadows : { 0u, 1u })
                {
                    camera.mNoSkyShadows = noShadows;
                    shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });
                    std::vector<float> sunlit;
                    mRenderer.readChannel(Channel::Shadowed, sunlit);
                    std::size_t shadowed = 0;
                    for (std::size_t value = 0; value < sunlit.size(); value += 4)
                        shadowed += sunlit[value + 3] == 0.0f ? 1 : 0;
                    if (noShadows == 0u)
                        EXPECT_GT(shadowed, std::size_t{ 2 * size })
                            << "the slab cast no shadow, or this proves nothing";
                    else
                        EXPECT_EQ(shadowed, 0u) << "a picture with shadows off kept a shadow under the water";
                }
            }

            // **And the roofed floor beside a wall the sun does not light stays dark.** The wall
            // faces the eye square to the overhead sun, so the sun adds nothing to it and its bit is
            // one, since nothing was split off it. Counted in the temporal pass's local mean, those
            // bits held the floor at the wall's foot up to a seam of sunlight the roof hides. The
            // roof's penumbra is `2000 * 0.0349` = 70 units, fourteen pixels of the floor, so the
            // temporal pass blends rather than handing each bit on. The roof reaches past the floor
            // on every side, so no sun lands where a bounce could carry it back, and every value of
            // the frame is nought.
            // The eye looks 20 degrees down at the wall's foot, so the frame's top edge meets the
            // wall 150 + 300 tan 10° = 203 units up, under its top, and its bottom edge the floor.
            SCOPED_TRACE("beside a wall");
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -300.0f, 150.0f), osg::Vec3f(0.0f, 0.0f, 40.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene,
                std::array<osg::Vec3f, 4>{ osg::Vec3f(-8000.0f, -8000.0f, 2000.0f),
                    osg::Vec3f(8000.0f, -8000.0f, 2000.0f), osg::Vec3f(8000.0f, 8000.0f, 2000.0f),
                    osg::Vec3f(-8000.0f, 8000.0f, 2000.0f) });
            addQuad(scene,
                std::array<osg::Vec3f, 4>{ osg::Vec3f(-4000.0f, 0.0f, 0.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f),
                    osg::Vec3f(4000.0f, 0.0f, 400.0f), osg::Vec3f(-4000.0f, 0.0f, 400.0f) });

            const Frame raw = shoot(scene, {}, camera, size, {});

            std::vector<float> sunlit;
            mRenderer.readChannel(Channel::Shadowed, sunlit);
            std::size_t floor = 0;
            std::size_t wall = 0;
            for (std::size_t value = 0; value < sunlit.size(); value += 4)
            {
                const bool lit = sunlit[value] > 0.0f;
                ASSERT_EQ(sunlit[value + 3], lit ? 0.0f : 1.0f) << "pixel " << value / 4;
                ++(lit ? floor : wall);
            }
            ASSERT_GT(floor, std::size_t{ 8 * size }) << "no floor under the wall, or this proves nothing";
            ASSERT_GT(wall, std::size_t{ 8 * size }) << "no wall over the floor, or this proves nothing";

            const Frame filtered = shoot(scene, {}, camera, size,
                { .mFrames = 16, .mAverage = false, .mFilter = true, .mLoss = HistoryLoss::Cut });
            EXPECT_EQ(filtered.mRadiance, raw.mRadiance);
        }

        /// **The floor at a wall's foot is shadowed by the wall, however near it stands.** A sun 14
        /// degrees up behind a wall, and the eye ten units off the wall's foot, where a pixel is a
        /// fifth of a unit of floor. A shadow ray from the floor `d` short of the wall meets it after
        /// `d / cos 14°` = `1.031 d`, so every floor point within a unit of the wall asks about an
        /// occluder nearer than a unit — which a ray that skipped its first unit never met, and lit.
        /// The wall is two-sided, so it casts toward a ray that meets it from either face.
        ///
        /// Every pixel the sun adds light to is floor in front of the wall, and its bit is nought.
        TEST_F(RtxVisibilityTest, theFloorAtAWallsFootIsShadowedByTheWallNoMatterHowNear)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -8.0f, 8.0f), osg::Vec3f(0.0f, -1.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            const float elevation = osg::DegreesToRadians(14.0f);
            camera.mSun = Shaders::sunSource(
                osg::Vec3f(0.0f, std::cos(elevation), std::sin(elevation)), osg::Vec3f(2.0f, 2.0f, 2.0f));

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            const Index wall
                = scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f(0.5f, 0.5f, 0.5f), .mTwoSided = true });
            addQuad(scene,
                std::array<osg::Vec3f, 4>{ osg::Vec3f(-4000.0f, 0.0f, 0.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f),
                    osg::Vec3f(4000.0f, 0.0f, 400.0f), osg::Vec3f(-4000.0f, 0.0f, 400.0f) },
                wall);

            shoot(scene, {}, camera, size, {});

            std::vector<float> sunlit;
            mRenderer.readChannel(Channel::Shadowed, sunlit);
            ASSERT_EQ(sunlit.size(), std::size_t{ size } * size * 4);
            std::size_t floor = 0;
            for (std::size_t value = 0; value < sunlit.size(); value += 4)
            {
                if (!(sunlit[value] > 0.0f))
                    continue;
                ++floor;
                EXPECT_EQ(sunlit[value + 3], 0.0f) << "pixel " << value / 4 << " lit through the wall";
            }
            ASSERT_GT(floor, std::size_t{ 8 * size }) << "no floor before the wall, or this proves nothing";
        }

        /// **The sky's shadowed light is every source's, and a source under a floor draws no bit.**
        /// An open floor under the sun overhead, with Masser up at 60 degrees from the zenith: by day
        /// a thousandth of the sun, at dusk a fifth of the light and blue where the sun is red.
        ///
        /// `CHANNEL_SHADOWED.rgb` is the sum of what each source delivers, `E cos / pi` times the
        /// grey's albedo of a half, in every pixel of one frame: the sun's `(2, 1, 0.5) / 2 pi`, and
        /// Masser's `E * cos 60° / 2 pi` beside it. A pick of one source over its chance put each pixel's hue on
        /// the pick, the sun's or the moon's. Nothing stands on the floor, so every bit is one; the
        /// penumbra says whether it was drawn. With no floor, the default, a bit is drawn wherever a
        /// second source lights the pixel at all, the daylight moon too: `SHADOW_PENUMBRA_DRAWN`.
        /// Under a floor of 1/64 the daylight moon is under it and the sun holds all but it,
        /// `SHADOW_PENUMBRA_CLEAR`, and the dusk moon, a fifth of the light, is over it and drawn.
        TEST_F(RtxVisibilityTest, theSkysShadowedLightIsEverySourcesAndAMinorOneDrawsNoBit)
        {
            constexpr std::uint32_t size = 32;
            const osg::Vec3f sunlight(2.0f, 1.0f, 0.5f);
            const float tilt = osg::DegreesToRadians(60.0f);

            const auto underMoon = [&](const osg::Vec3f& moonlight, float floor) {
                Shaders::VisibilityConstants camera = overheadSun(size);
                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), sunlight);
                Shaders::MoonDisc masser{};
                masser.mSource = Shaders::skySource(
                    osg::Vec3f(0.0f, std::sin(tilt), std::cos(tilt)), moonlight, moonAngularRadius(94.0f));
                masser.mRight = osg::Vec3f(1.0f, 0.0f, 0.0f);
                masser.mUp = osg::Vec3f(0.0f, -std::cos(tilt), std::sin(tilt));
                masser.mColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
                masser.mAlpha = 1.0f;
                masser.mFace = Shaders::NO_TEXTURE;
                camera.mMoons[0] = masser;
                camera.mMoons[1] = Shaders::MoonDisc{};

                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                shoot(scene, {}, camera, size, { .mShadowFloor = floor });

                std::vector<float> shadowed;
                std::vector<float> penumbra;
                mRenderer.readChannel(Channel::Shadowed, shadowed);
                mRenderer.readChannel(Channel::Penumbra, penumbra);
                EXPECT_EQ(shadowed.size(), std::size_t{ size } * size * 4);

                const osg::Vec3f sum = (sunlight + moonlight * std::cos(tilt)) * (0.5f * Shaders::INV_PI);
                for (std::size_t pixel = 0; pixel < shadowed.size() / 4; ++pixel)
                {
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        EXPECT_NEAR(shadowed[pixel * 4 + channel], sum[channel], sum[channel] * 1e-5f)
                            << "pixel " << pixel << ", channel " << channel;
                    EXPECT_EQ(shadowed[pixel * 4 + 3], 1.0f) << "pixel " << pixel;
                }
                return penumbra;
            };

            const osg::Vec3f daylightMoon = sunlight * 0.002f;
            for (const float width : underMoon(daylightMoon, Shaders::SHADOW_DRAW_FLOOR))
                ASSERT_EQ(width, Shaders::SHADOW_PENUMBRA_DRAWN) << "a second source with no floor drew no bit";
            for (const float width : underMoon(daylightMoon, 1.0f / 64.0f))
                ASSERT_EQ(width, Shaders::SHADOW_PENUMBRA_CLEAR) << "a moon under the floor drew the sun's bit";

            // Masser's weight is `(0.5, 0.5, 2) * cos 60°` against the sun's `(2, 1, 0.5)`, in
            // luminance 0.3042 against 1.1765: a fifth of the sky, so over the floor and drawn.
            for (const float width : underMoon(osg::Vec3f(0.5f, 0.5f, 2.0f), 1.0f / 64.0f))
                ASSERT_EQ(width, Shaders::SHADOW_PENUMBRA_DRAWN) << "a dusk moon drew no bit";
        }

        /// **A thin hard shadow keeps its depth through the denoiser, and a bit's penumbra is its
        /// occluder's distance times the source's half angle.** A bar 40 units wide, 100 units over
        /// the floor under the overhead sun, seen from 300 units up at 96 pixels square, with no
        /// indirect light, so the sun is the frame: its shadow is 7 to 9 pixels wide.
        ///
        /// By hand: the sun's shadow cone is `SUN_SHADOW_RADIUS` = 0.0349 as a sine, a tangent of
        /// 0.03493, so a ray stopped by the bar 100 units up has a penumbra 3.493 units in radius. A
        /// pixel there is `mSpreadAngle` × its distance wide, and the floor under the bar stands 300
        /// to 360 units from the eye: the penumbra is 3.493 over 300 and over 360 pixel widths of
        /// the frame's spread, under one pixel, and a shadow that hard is noiseless. Every open ray
        /// holds `SHADOW_PENUMBRA_CLEAR`.
        ///
        /// So the denoised frame is the raw one, value for value, after 96 frames of history whose
        /// last draws what the raw frame draws: the temporal pass hands a hard bit on as it is, and
        /// no filter level is narrower than nothing. Before, the clamp's box and the levels held the
        /// umbra at 31 of 255 where the raw frame's stood at 17.
        TEST_F(RtxVisibilityTest, aThinHardShadowKeepsItsDepth)
        {
            constexpr std::uint32_t size = 96;
            constexpr std::uint32_t frames = 96;
            const Shaders::VisibilityConstants camera = overheadSun(size);
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, roofOver(-100.0f, -60.0f, 100.0f));

            const Frame raw = shoot(scene, {}, camera, size, { .mFrames = 1, .mFirstFrame = 300 + frames - 1 });

            std::vector<float> bits;
            std::vector<float> widths;
            mRenderer.readChannel(Channel::Shadowed, bits);
            mRenderer.readChannel(Channel::Penumbra, widths);
            ASSERT_EQ(widths.size() * 4, bits.size()) << "a channel of one half a pixel";

            const auto sine = static_cast<double>(Shaders::SUN_SHADOW_RADIUS);
            const double radius = 100.0 * sine / std::sqrt(1.0 - sine * sine);
            const auto spread = static_cast<double>(camera.mEyes.mWorld.mSpreadAngle);
            const float widest = static_cast<float>(radius / (spread * 300.0));
            const float narrowest = static_cast<float>(radius / (spread * 360.0));
            ASSERT_LT(widest, 1.0f) << "a penumbra the denoiser does not count as hard proves nothing";

            std::size_t stopped = 0;
            for (std::size_t pixel = 0; pixel < bits.size() / 4; ++pixel)
            {
                const float width = widths[pixel];
                if (bits[pixel * 4 + 3] > 0.5f)
                {
                    ASSERT_EQ(width, Shaders::SHADOW_PENUMBRA_CLEAR) << "an open ray at pixel " << pixel;
                    continue;
                }
                ++stopped;
                EXPECT_GE(width, narrowest * 0.999f) << "pixel " << pixel;
                EXPECT_LE(width, widest * 1.001f) << "pixel " << pixel;
            }
            EXPECT_GT(stopped, std::size_t{ 7 * size }) << "the bar cast less than the shadow it casts";

            const Frame denoised = shoot(scene, {}, camera, size,
                { .mFrames = frames,
                    .mAverage = false,
                    .mFirstFrame = 300,
                    .mFilter = true,
                    .mLoss = HistoryLoss::Cut });
            EXPECT_EQ(denoised.mRadiance, raw.mRadiance);
        }

        /// **A penumbra is its nearest occluder's, laid on the receiver, and a stopped ray lets
        /// everything through.** The bar of `aThinHardShadowKeepsItsDepth` over a floor, seen from
        /// 300 units up, where a pixel is 3.61 units across, and 3.61 to 4.66 over the frame.
        ///
        /// A sun 60 degrees from the zenith across the bar, from -x, the bar 20 units up and a roof
        /// 500 up over the whole floor: every ray is stopped, and beside the bar, where its shadow
        /// falls `20 tan 60°` = 34.6 units off it toward +x, the nearest solid is the bar. Its penumbra is
        /// `20 / cos 60° * 0.03493 / cos 60°` = 2.79 units on the floor, under a pixel; the roof's is
        /// `500 / cos 60° * 0.03493 / cos 60°` = 69.9 units, over fifteen pixels. A ray that ended on
        /// the first solid traversal found took the roof's on whichever pixels it found the roof
        /// first.
        ///
        /// The same sun and the bar 100 up with no roof, its shadow 173 units off it: the bar stands
        /// `100 / cos 60°` = 200 along the ray, and the penumbra square to the light,
        /// `200 * 0.03493`, lies up to `1 / cos 60°` = twice as long on the floor, along the light's
        /// azimuth: 13.97 units, the reach the shadow denoiser is handed, where the light's own plane
        /// read half.
        ///
        /// And under a black pane half there at 40 units, over the whole floor, and a roof over half
        /// the floor at 100, the eye 30 units up under them both: every pixel's shadowed light is the
        /// sun's whole, `2 * 0.5 / pi` = 0.31831, and what the pane lets through is drawn into the bit.
        /// The roofed half's bit is shut, whatever the stopped ray met first, and its penumbra the
        /// roof's; the open half's is open on half its draws, the pane's opacity, and takes the drawn
        /// reach. Over 32 frames of at least 768 such pixels, n >= 24576 independent draws of a
        /// half: the mean's deviation is `0.5 / sqrt(n)` <= 0.0032, held at four of it.
        TEST_F(RtxVisibilityTest, aPenumbraIsItsNearestOccludersOnTheReceiverAndAStoppedRayLetsAllThrough)
        {
            constexpr std::uint32_t size = 96;
            const auto sine = static_cast<double>(Shaders::SUN_SHADOW_RADIUS);
            const double tangent = sine / std::sqrt(1.0 - sine * sine);

            const Shaders::VisibilityConstants overhead = overheadSun(size);
            const auto spread = static_cast<double>(overhead.mEyes.mWorld.mSpreadAngle);
            const float zenith = osg::DegreesToRadians(60.0f);
            Shaders::VisibilityConstants tilted = overhead;
            tilted.mSun = Shaders::sunSource(
                osg::Vec3f(-std::sin(zenith), 0.0f, std::cos(zenith)), osg::Vec3f(2.0f, 2.0f, 2.0f));

            const auto widthsUnder = [&](float barHeight, bool roofed) {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                if (roofed)
                    addQuad(scene, roofOver(-4000.0f, 4000.0f, 500.0f));
                addQuad(scene, roofOver(-100.0f, -60.0f, barHeight));
                shoot(scene, {}, tilted, size, {});

                std::vector<float> widths;
                mRenderer.readChannel(Channel::Penumbra, widths);
                return widths;
            };

            // Within the frame a pixel is `spread * 300` to `spread * 387` units across.
            const auto pixels
                = [&](double units, double distance) { return static_cast<float>(units / (spread * distance)); };
            const double barOnFloor = 20.0 / 0.5 * tangent / 0.5;
            const double roofOnFloor = 500.0 / 0.5 * tangent / 0.5;
            ASSERT_LT(pixels(barOnFloor, 300.0), 1.0f);
            ASSERT_GT(pixels(roofOnFloor, 387.0), 10.0f);
            std::size_t barred = 0;
            for (const float width : widthsUnder(20.0f, true))
            {
                ASSERT_LT(width, Shaders::SHADOW_PENUMBRA_CLEAR) << "the roof stops every ray";
                ASSERT_TRUE(width < 1.0f || width > 10.0f) << "a penumbra of neither occluder: " << width;
                barred += width < 1.0f ? 1 : 0;
            }
            EXPECT_GT(barred, std::size_t{ 5 * size }) << "the bar's shadow took a farther occluder's penumbra";

            // The ray is aimed across the disc, whose half angle is `asin 0.0349` = 2.0 degrees, so it
            // meets the bar `100 / cos 62°` to `100 / cos 58°` along it.
            const double half = std::asin(sine);
            const double nearest = 100.0 / std::cos(double{ zenith } - half);
            const double farthest = 100.0 / std::cos(double{ zenith } + half);
            std::size_t stopped = 0;
            for (const float width : widthsUnder(100.0f, false))
            {
                if (width == Shaders::SHADOW_PENUMBRA_CLEAR)
                    continue;
                ++stopped;
                EXPECT_GE(width, pixels(nearest * tangent / 0.5, 387.0) * 0.999f) << "the penumbra square to the light";
                EXPECT_LE(width, pixels(farthest * tangent / 0.5, 300.0) * 1.001f);
            }
            EXPECT_GT(stopped, std::size_t{ 7 * size }) << "the bar cast less than the shadow it casts";

            Shaders::VisibilityConstants between = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 30.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            between.mSkyHorizon = osg::Vec3f();
            between.mSkyZenith = osg::Vec3f();
            between.mSun = overhead.mSun;
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addPane(scene, sheetAt(4000.0f, 40.0f), osg::Vec4f(0.0f, 0.0f, 0.0f, 0.5f), 1.0f, true);
            addQuad(scene, roofOver(-4000.0f, 0.0f, 100.0f));
            constexpr std::uint32_t frames = 32;
            const float sunlit = Shaders::INV_PI;
            std::vector<float> shadowed;
            std::vector<float> penumbrae;
            std::size_t drawn = 0;
            std::size_t opened = 0;
            std::size_t roofed = 0;
            shoot(scene, {}, between, size, { .mFrames = frames, .mAverage = false, .mEachFrame = [&](const Frame&) {
                                                 mRenderer.readChannel(Channel::Shadowed, shadowed);
                                                 mRenderer.readChannel(Channel::Penumbra, penumbrae);
                                                 for (std::size_t pixel = 0; pixel < penumbrae.size(); ++pixel)
                                                 {
                                                     EXPECT_NEAR(shadowed[pixel * 4], sunlit, 2e-3f)
                                                         << "pixel " << pixel;
                                                     const float bit = shadowed[pixel * 4 + 3];
                                                     if (penumbrae[pixel] == Shaders::SHADOW_PENUMBRA_DRAWN)
                                                     {
                                                         ++drawn;
                                                         opened += bit == 1.0f ? 1 : 0;
                                                         continue;
                                                     }
                                                     EXPECT_EQ(bit, 0.0f) << "pixel " << pixel << " under the roof";
                                                     ++roofed;
                                                 }
                                             } });
            EXPECT_GE(drawn, std::size_t{ frames * 8 * size });
            EXPECT_GE(roofed, std::size_t{ frames * 8 * size });
            const double open = static_cast<double>(opened) / static_cast<double>(drawn);
            EXPECT_NEAR(open, 0.5, 4.0 * 0.5 / std::sqrt(static_cast<double>(drawn)))
                << "the bit's mean is what the pane lets through";
        }

        /// **A penumbra's bits are drawn blue, so a little blur takes most of their noise.** The roof
        /// of the penumbra test below over half the floor, 2000 units up, where the penumbra runs 140
        /// units across; one frame's bits, each blurred over the 5 by 5 pixels around it, against the
        /// mean of 256 frames' bits. Drawn from the tile (`STREAM_SUN_DISC`), a pixel's error is
        /// arranged against its neighbours' and the box averages it away; hashed, it is white, and
        /// the box keeps a fifth of it. Asserted as the blue frame's error under three quarters of the
        /// white one's.
        TEST_F(RtxVisibilityTest, aPenumbrasBitsAreDrawnBlueSoABlurTakesTheirNoise)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = overheadSun(size);
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, roofOver(-4000.0f, 0.0f, 2000.0f));

            std::vector<double> mean(std::size_t{ size } * size, 0.0);
            std::vector<float> bits;
            shoot(scene, {}, camera, size, { .mFrames = 256, .mAverage = false, .mEachFrame = [&](const Frame&) {
                                                mRenderer.readChannel(Channel::Shadowed, bits);
                                                for (std::size_t pixel = 0; pixel < mean.size(); ++pixel)
                                                    mean[pixel] += static_cast<double>(bits[pixel * 4 + 3]) / 256.0;
                                            } });

            const auto blurredError = [&](NoiseSource noise) {
                camera.mFrame = 1000;
                shoot(scene, {}, camera, size, { .mNoise = noise });
                mRenderer.readChannel(Channel::Shadowed, bits);
                double squares = 0.0;
                std::size_t counted = 0;
                for (int y = 2; y < static_cast<int>(size) - 2; ++y)
                    for (int x = 2; x < static_cast<int>(size) - 2; ++x)
                    {
                        double box = 0.0;
                        double truth = 0.0;
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dx = -2; dx <= 2; ++dx)
                            {
                                const std::size_t at
                                    = static_cast<std::size_t>((y + dy) * static_cast<int>(size) + x + dx);
                                box += static_cast<double>(bits[at * 4 + 3]) / 25.0;
                                truth += mean[at] / 25.0;
                            }
                        squares += (box - truth) * (box - truth);
                        ++counted;
                    }
                return std::sqrt(squares / static_cast<double>(counted));
            };

            const double white = blurredError(NoiseSource::WhiteHash);
            const double blue = blurredError(NoiseSource::BlueNoiseTile);
            ASSERT_GT(white, 0.01) << "a penumbra one ray a pixel draws is noisy, or this proves nothing";
            EXPECT_LT(blue, 0.75 * white) << "blue " << blue << ", white " << white;
        }

        /// **The noise of a penumbra comes off and its light stays where it was.** A roof over half
        /// the floor, two thousand units up, under a sun whose shadow cone is `SUN_SHADOW_RADIUS`
        /// either way: the penumbra is `2 * 2000 * 0.0349` = 140 units across, a quarter of the
        /// 346 the frame spans, and one ray a pixel speckles it. Measured against a reference of 256
        /// unfiltered frames, one frame's error and the denoised frame's after sixty-four frames of
        /// history — on a dry floor, and on a flooded one seen through the water, whose sun is what
        /// the refraction found. Two tests on one body, since each is 321 frames.
        class RtxPenumbraDenoiseTest : public RtxVisibilityTest
        {
        protected:
            void penumbraOn(bool flooded)
            {
                constexpr std::uint32_t size = 64;
                Shaders::VisibilityConstants camera = overheadSun(size);
                SceneDesc scene = floorOf(flooded, camera);
                addQuad(scene, roofOver(-4000.0f, 0.0f, 2000.0f));

                const Frame reference = shoot(
                    scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f }, .mFrames = 256 });

                camera.mFrame = 1000;
                const Frame raw = shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });

                const Frame denoised = shoot(scene, {}, camera, size,
                    { .mSea = SeaState{ .mSignificantHeight = 0.0f },
                        .mFrames = 64,
                        .mAverage = false,
                        .mFirstFrame = 2000,
                        .mFilter = true,
                        .mLoss = HistoryLoss::Cut });

                const float rawError = raw.errorFrom(reference);
                const float denoisedError = denoised.errorFrom(reference);
                ASSERT_GT(rawError, 0.01f) << "a penumbra one ray a pixel draws is noisy, or this proves nothing";
                EXPECT_LT(denoisedError, rawError * 0.1f) << "raw " << rawError << ", denoised " << denoisedError;

                // **The denoiser keeps the penumbra's light to a quarter of a per cent**: measured at
                // +0.03% dry and +0.10% flooded. Its history is read back into its own blend, so it is
                // kept in full floats (`DenoiseHistory`): kept in halves, whose store rounds toward
                // nought here, it stood at -0.57% and -0.41% after these frames and -0.68% and -0.47%
                // after 256. The SDK's contrast step, which darkened the penumbra by 3.4% on purpose,
                // is not ported, and the frame's edge no longer counts as shadow; either would fail
                // this.
                EXPECT_NEAR(denoised.mean(), reference.mean(), reference.mean() * 0.0025f)
                    << "the denoiser moved the light it was smoothing";
            }
        };

        /// Measured at a thirty-second: 0.0762 raw against 0.0024 denoised.
        TEST_F(RtxPenumbraDenoiseTest, theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas)
        {
            penumbraOn(false);
        }

        /// Measured at a thirteenth: 0.0318 raw against 0.0025 denoised. Before the water split the sun
        /// off what its rays found, nothing filtered it, and the denoised frame stood at 0.0322.
        TEST_F(RtxPenumbraDenoiseTest, theShadowDenoiserTakesTheNoiseOffAPenumbraSeenThroughTheWater)
        {
            penumbraOn(true);
        }

        /// **A shadow's history takes nothing from a pixel that receives nothing.** A floor of two
        /// halves on one plane, the near one lit by a lamp and the far one a material the lamps do not
        /// light, so every pixel of it is a non-receiver, whose history holds nought. Bars ten units
        /// wide every thirty, thirty units over the floor, cross the seam under a lamp ten units
        /// across sixty units up, so every tile along the seam holds lit receivers, receivers in a
        /// soft penumbra, and non-receivers. The eye moves half a pixel a frame across the seam, so
        /// each history fetch falls between two texels, one of them across the seam for the row
        /// beside it. Held against the same run over a floor the lamp lights whole: the near half's
        /// rays and bits are the same in both, so the row beside the seam filters to the same light.
        /// A tap taken from a non-receiver pulls its history toward nought, and the row reads darker:
        /// measured at +0.7% against the whole floor, the spatial filter's own difference across the
        /// seam, and at -7.5% with every tap taken.
        TEST_F(RtxVisibilityTest, aShadowsHistoryTakesNothingFromAPixelThatReceivesNothing)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t frames = 32;

            // Half of the 346 units the frame spans over its 64 pixels.
            constexpr float step = 2.7f;

            const auto run = [&](bool farLit, std::vector<float>* unshadowed) {
                SceneDesc scene;
                addQuad(scene,
                    std::array<osg::Vec3f, 4>{ osg::Vec3f(-4000.0f, -4000.0f, 0.0f),
                        osg::Vec3f(4000.0f, -4000.0f, 0.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f),
                        osg::Vec3f(-4000.0f, 0.0f, 0.0f) });
                addQuad(scene,
                    std::array<osg::Vec3f, 4>{ osg::Vec3f(-4000.0f, 0.0f, 0.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f),
                        osg::Vec3f(4000.0f, 4000.0f, 0.0f), osg::Vec3f(-4000.0f, 4000.0f, 0.0f) },
                    scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f(0.5f, 0.5f, 0.5f), .mLampLit = farLit }));
                for (int bar = -6; bar <= 6; ++bar)
                {
                    const float middle = 30.0f * static_cast<float>(bar);
                    addQuad(scene,
                        std::array<osg::Vec3f, 4>{ osg::Vec3f(middle - 5.0f, -400.0f, 30.0f),
                            osg::Vec3f(middle + 5.0f, -400.0f, 30.0f), osg::Vec3f(middle + 5.0f, 400.0f, 30.0f),
                            osg::Vec3f(middle - 5.0f, 400.0f, 30.0f) });
                }
                scene.addLight(Light{ .mPosition = osg::Vec3f(0.0f, 0.0f, 60.0f),
                    .mIntensity = osg::Vec3f(4000.0f, 4000.0f, 4000.0f),
                    .mReach = 2000.0f,
                    .mSourceRadius = 10.0f });

                Frame last;
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    const float along = step * static_cast<float>(at);
                    Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, along - 1.0f, 300.0f),
                        osg::Vec3f(0.0f, along, 0.0f), 60.0f, size, size, 100000.0f);
                    camera.mSkyHorizon = osg::Vec3f();
                    camera.mSkyZenith = osg::Vec3f();
                    camera.mSun.mIrradiance = osg::Vec3f();
                    camera.mFrame = 3000 + at;
                    last = shoot(scene, {}, camera, size,
                        { .mFilter = true,
                            .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mSetScene = at == 0 });
                }
                if (unshadowed != nullptr)
                    mRenderer.readChannel(Channel::Shadowed, *unshadowed);
                return last;
            };

            std::vector<float> unshadowed;
            const Frame seam = run(false, &unshadowed);
            const Frame whole = run(true, nullptr);

            // The near half's row beside the seam: a receiver whose neighbour across it receives
            // nothing, which the bars' own pixels never are.
            const auto receives = [&](std::size_t pixel) {
                return unshadowed[pixel * 4] + unshadowed[pixel * 4 + 1] + unshadowed[pixel * 4 + 2] > 0.0f;
            };
            double seamSum = 0.0;
            double wholeSum = 0.0;
            std::size_t counted = 0;
            for (std::size_t y = 1; y + 1 < size; ++y)
                for (std::size_t x = 0; x < size; ++x)
                {
                    const std::size_t pixel = y * size + x;
                    if (!receives(pixel) || (receives(pixel - size) && receives(pixel + size)))
                        continue;
                    seamSum += static_cast<double>(seam.mRadiance[pixel * 4 + 1]);
                    wholeSum += static_cast<double>(whole.mRadiance[pixel * 4 + 1]);
                    ++counted;
                }

            ASSERT_GE(counted, size / 2) << "the seam is in the frame";
            EXPECT_NEAR(seamSum / wholeSum, 1.0, 0.025) << "the row beside the seam took history from across it";
        }

        /// **The lamps' light is the shadow denoiser's, exact where nothing stands in its way and quieter
        /// where something does.** Four lamps of four colours over a floor under a black sky, so the
        /// lamps are the frame's whole light: each pixel's reservoir holds one, weighed against the
        /// other three, and its ray decides the pixel's bit. The light the bit multiplies is every
        /// lamp's summed, so on the open floor, where every bit is one, one frame is the reference.
        /// A square sixty units across, thirty over the floor, casts four overlapping shadows of four
        /// colours, and there the bits are noisy. Measured against 256 unfiltered frames, a raw frame
        /// and a denoised one after sixteen of history.
        TEST_F(RtxVisibilityTest, theLampsLightIsExactOnAnOpenFloorAndTheirShadowsComeOutQuieter)
        {
            constexpr std::uint32_t size = 64;

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();

            for (const bool blocked : { false, true })
            {
                SCOPED_TRACE(blocked ? "blocked" : "open");
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                if (blocked)
                    addQuad(scene,
                        std::array<osg::Vec3f, 4>{ osg::Vec3f(-30.0f, -30.0f, 30.0f), osg::Vec3f(30.0f, -30.0f, 30.0f),
                            osg::Vec3f(30.0f, 30.0f, 30.0f), osg::Vec3f(-30.0f, 30.0f, 30.0f) });

                const std::array<std::pair<osg::Vec2f, osg::Vec3f>, 4> lamps{ {
                    { osg::Vec2f(-100.0f, -100.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f) },
                    { osg::Vec2f(100.0f, -100.0f), osg::Vec3f(0.0f, 4000.0f, 0.0f) },
                    { osg::Vec2f(-100.0f, 100.0f), osg::Vec3f(0.0f, 0.0f, 4000.0f) },
                    { osg::Vec2f(100.0f, 100.0f), osg::Vec3f(4000.0f, 4000.0f, 0.0f) },
                } };
                for (const auto& [place, intensity] : lamps)
                    scene.addLight(Light{
                        .mPosition = osg::Vec3f(place.x(), place.y(), 60.0f),
                        .mIntensity = intensity,
                        .mReach = 500.0f,
                    });

                // The open floor's one frame is the reference, so a reference of four is as good as
                // one of 256 and a quarter of a second shorter.
                camera.mFrame = 0;
                const Frame reference = shoot(scene, {}, camera, size, { .mFrames = blocked ? 256u : 4u });

                camera.mFrame = 1000;
                const Frame raw = shoot(scene, {}, camera, size);
                const Frame filtered = shoot(scene, {}, camera, size,
                    { .mFrames = 16,
                        .mAverage = false,
                        .mFirstFrame = 2000,
                        .mFilter = true,
                        .mLoss = HistoryLoss::Cut });

                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    SCOPED_TRACE(channel);
                    const float rawError = raw.errorFrom(reference, channel);
                    const float filteredError = filtered.errorFrom(reference, channel);
                    if (!blocked)
                    {
                        // Measured at 1.0e-7, the rounding of a sum of 256 frames.
                        EXPECT_LT(rawError, 1e-6f) << "one frame of the open floor is not the reference";
                        EXPECT_LT(filteredError, 1e-6f) << "the denoised open floor is not the reference";
                        continue;
                    }

                    ASSERT_GT(rawError, 0.001f)
                        << "four shadows drawn one ray a pixel are noisy, or this proves nothing";
                    // Measured at 0.125 to 0.142 of the raw error, channel by channel.
                    EXPECT_LT(filteredError, rawError * 0.2f) << "raw " << rawError << ", filtered " << filteredError;

                    // Measured within 0.13 to 1.0 per cent. **The ratio estimator's cost**: the bit a
                    // neighbour averages in is another lamp's, so a filtered pixel's visibility is
                    // the lamps' in the shares its neighbours drew, under its own sum's colour.
                    EXPECT_NEAR(filtered.mean(channel), reference.mean(channel), reference.mean(channel) * 0.015f)
                        << "the denoiser moved the light";
                }

                // A floor with no specular map has no lobe, and the lobe's channel holds nought light.
                std::vector<float> lobe;
                mRenderer.readChannel(Channel::Specular, lobe);
                ASSERT_EQ(lobe.size(), std::size_t{ size } * size * 4);
                for (std::size_t value = 0; value < lobe.size(); value += 4)
                    ASSERT_EQ(osg::Vec3f(lobe[value], lobe[value + 1], lobe[value + 2]), osg::Vec3f())
                        << "pixel " << value / 4 << " of a floor with no lobe";
            }
        }

        /// What two shadowed sources one pixel shows come to: the share of the two lights the second
        /// adds, in luminance and over every pixel, and the share of frames the pixel kept its bit.
        struct BitShares
        {
            double mShare;
            double mDrawn;
        };

        /// **Two shadowed sources one pixel shows, and one bit between them.** Each alone first,
        /// read off `CHANNEL_SHADOWED`: the first's ray stopped at every pixel, and the second's
        /// through at every one. Then both, where the channel holds the sum of the two lights, over
        /// `sFrames` frames of every pixel: the bit is one — the second's — in the share the
        /// second's luminance is of the two, and a pixel that kept the brighter source's bit would
        /// hold the same one every frame. Two tests on one body.
        class RtxBitShareTest : public RtxVisibilityTest
        {
        protected:
            static constexpr std::uint32_t sSize = 32;
            static constexpr std::uint32_t sFrames = 64;

            /// @param shootWith shoots the scene with the first source, the second, or both, as its
            ///        two flags say, under the shot it is handed.
            BitShares sharesOf(const std::function<void(bool first, bool second, const Shot& shot)>& shootWith)
            {
                constexpr std::size_t pixels = std::size_t{ sSize } * sSize;

                const auto alone = [&](bool first, bool second) {
                    shootWith(first, second, {});
                    std::vector<float> read;
                    mRenderer.readChannel(Channel::Shadowed, read);
                    return read;
                };

                const std::vector<float> stopped = alone(true, false);
                const std::vector<float> open = alone(false, true);
                for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                {
                    EXPECT_EQ(stopped[pixel * 4 + 3], 0.0f) << "the first source's ray got through at " << pixel;
                    EXPECT_EQ(open[pixel * 4 + 3], 1.0f) << "the second source's ray was stopped at " << pixel;
                }

                std::vector<float> both;
                std::vector<double> kept(pixels, 0.0);
                shootWith(true, true, { .mFrames = sFrames, .mAverage = false, .mEachFrame = [&](const Frame&) {
                                           mRenderer.readChannel(Channel::Shadowed, both);
                                           for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                                               kept[pixel] += static_cast<double>(both[pixel * 4 + 3]);
                                       } });

                BitShares shares{ .mShare = 0.0, .mDrawn = 0.0 };
                for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                {
                    const osg::Vec3f first(stopped[pixel * 4], stopped[pixel * 4 + 1], stopped[pixel * 4 + 2]);
                    const osg::Vec3f second(open[pixel * 4], open[pixel * 4 + 1], open[pixel * 4 + 2]);
                    const osg::Vec3f sum(both[pixel * 4], both[pixel * 4 + 1], both[pixel * 4 + 2]);
                    for (int channel = 0; channel < 3; ++channel)
                        EXPECT_NEAR(sum[channel], first[channel] + second[channel], 1e-5f * sum[channel])
                            << "the channel holds both sources' light at pixel " << pixel;

                    const float fromSecond = second * Shaders::LUMINANCE_WEIGHTS;
                    shares.mShare
                        += static_cast<double>(fromSecond / (fromSecond + first * Shaders::LUMINANCE_WEIGHTS));
                    shares.mDrawn += kept[pixel] / sFrames;
                }
                shares.mShare /= pixels;
                shares.mDrawn /= pixels;

                EXPECT_GT(shares.mShare, 0.2) << "the first source outshines the second, or this proves nothing";
                EXPECT_LT(shares.mShare, 0.8) << "the second source outshines the first, or this proves nothing";
                return shares;
            }
        };

        /// **A surface lit by the sun and by a lamp keeps the bit of either by the light it adds.** A
        /// floor under `overheadSun`, roofed two thousand units up, so the sun's ray is stopped at
        /// every pixel, and a lamp sixty units over the floor under the roof, which every pixel sees.
        ///
        /// A bit drawn independently at every pixel of every frame: a deviation of
        /// `sqrt(p (1 - p) / 65536)`, 0.002 at most. Measured: a share of 0.323, and 0.321 drawn.
        TEST_F(RtxBitShareTest, aSurfaceLitByTheSunAndALampKeepsTheBitOfEitherByTheLightItAdds)
        {
            const BitShares shares = sharesOf([&](bool sun, bool lamp, const Shot& shot) {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                addQuad(scene, roofOver(-4000.0f, 4000.0f, 2000.0f));
                if (lamp)
                    scene.addLight(Light{
                        .mPosition = osg::Vec3f(0.0f, 0.0f, 60.0f),
                        .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                        .mReach = 500.0f,
                    });

                Shaders::VisibilityConstants camera = overheadSun(sSize);
                if (!sun)
                    camera.mSun.mIrradiance = osg::Vec3f();
                shoot(scene, {}, camera, sSize, shot);
            });

            EXPECT_NEAR(shares.mDrawn, shares.mShare, 0.01) << "the bit kept is not the lamp's in the lamp's share";
        }

        /// **A pixel of water keeps the bit of what it reflects or of what it shows, drawn by the light
        /// each adds.** Water under `grazingTheWater`'s eye, so every pixel's two rays are parallel
        /// to every other's. The reflection meets `sGrazedWall`, under an overhang 150 deep that
        /// shades all of it from a sun 45 degrees up behind the eye. The refraction meets a bed fifty
        /// units down, where the sun reaches: at most `-100 + 1.09 * 50` = -45 along, where the sun's
        /// path back rises 50 units in the water at 32 degrees off the vertical and 100 more in air,
        /// 131 units toward the sun, to -176 — short of the overhang, which starts at -150.
        ///
        /// Measured: a share of 0.559, and 0.557 drawn.
        TEST_F(RtxBitShareTest, aPixelOfWaterKeepsTheShadowOfEitherRayByTheLightItAdds)
        {
            Shaders::VisibilityConstants camera = grazingTheWater(sSize);
            litThroughWater(camera, osg::DegreesToRadians(45.0f));
            camera.mAmbient = osg::Vec3f();

            const BitShares shares = sharesOf([&](bool wall, bool bed, Shot shot) {
                SceneDesc scene = makeOpenWater(4000.0f);
                if (wall)
                {
                    addQuad(scene, sGrazedWall);
                    addQuad(scene,
                        std::array<osg::Vec3f, 4>{ osg::Vec3f(-50.0f, -150.0f, 100.0f),
                            osg::Vec3f(50.0f, -150.0f, 100.0f), osg::Vec3f(50.0f, 0.0f, 100.0f),
                            osg::Vec3f(-50.0f, 0.0f, 100.0f) });
                }
                if (bed)
                    addQuad(scene, sheetAt(4000.0f, -50.0f));

                shot.mSea = SeaState{ .mSignificantHeight = 0.0f };
                shoot(scene, {}, camera, sSize, shot);
            });

            EXPECT_NEAR(shares.mDrawn, shares.mShare, 0.01) << "the bit kept is not the bed's in the bed's share";
        }
    }
}
