#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/displaycurve.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/halfstep.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/rendererprobe.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framesinflight.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/fognoise.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/moon.hpp>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// What colour the air is. Named for the reason `sFoggySky` is: each expectation computes
        /// with it as well as handing it to the shader. Deliberately not grey, so a fog scattering
        /// the wrong colour cannot pass by matching a total.
        const osg::Vec3f sHaze(0.1f, 0.2f, 0.4f);

        /// A coverage band faint enough that the air is still even enough to compute with. One path serves every
        /// kind of air, so this chooses only how much of the band the arithmetic carries: at a thousandth the
        /// density moves by at most two parts in a thousand, below anything asserted here. A test that wants
        /// the band exercised sets this and one that does not sets one; the banks have their own tests, at nought.
        constexpr float sVolumeOverEvenAir = 0.999f;

        /// A frame `columns` columns of the air volume across, and a pixel more, so a centre pixel
        /// stands on its axis. **For a test whose figures are the volume's**: what a column reads and
        /// how far its tent reaches stand still in the world at a frame of so many columns, whatever
        /// `FOG_VOLUME_SCALE` makes a column's pixels, where a frame of so many pixels moves them.
        constexpr std::uint32_t framedInColumns(std::uint32_t columns)
        {
            return columns * Shaders::FOG_VOLUME_SCALE + 1;
        }

        /// What one unit of a lamp's intensity delivers `span` units away, from the same windowed
        /// inverse square the shader uses: an inverse square that reaches exactly zero at the
        /// lamp's reach, because Morrowind's is a hard cutoff and clipping one leaves a ring.
        constexpr float lampDelivered(float span, float reach)
        {
            const float ratio = span / reach;
            const float window = 1.0f - ratio * ratio * ratio * ratio;
            return window * window / (span * span + 1.0f);
        }

        /// A level rectangle standing between a ray along the y axis and a lamp above it. Not `sheetAt`, because
        /// half a lid is the point: a square about the origin is over the whole march or none of it, and these
        /// tests need a shadow boundary where they put it rather than where the grid does.
        std::array<osg::Vec3f, 4> makeLid(float height, float from, float to, float halfWidth)
        {
            return {
                osg::Vec3f(-halfWidth, from, height),
                osg::Vec3f(halfWidth, from, height),
                osg::Vec3f(halfWidth, to, height),
                osg::Vec3f(-halfWidth, to, height),
            };
        }

        /// Puts `camera` under a sky of one radiance and nothing else, with air of `extinction` in it
        /// pooling at `level`.
        void litThroughFog(Shaders::VisibilityConstants& camera, float extinction,
            float level = -std::numeric_limits<float>::infinity())
        {
            camera.mSkyHorizon = osg::Vec3f(sFoggySky, sFoggySky, sFoggySky);
            camera.mSkyZenith = camera.mSkyHorizon;
            camera.mAmbientFromSky = 1.0f;
            camera.mFogColour = sHaze;
            camera.mFogExtinction = extinction;
            camera.mWaterLevel = level;

            // Even air, which is what every exact expectation here needs: a banked field varies
            // along the ray, and its answer then stops being one anyone can write down. The banks
            // have their own test.
            camera.mFogUniform = 1.0f;
        }

        /// Fog takes what Beer-Lambert says over the path and gives its own colour back in place.
        ///
        /// A horizontal ray makes the march exact: the layer's density varies only with height, so along such a
        /// ray the medium is uniform and the march telescopes, the per-step transmittances multiplying to
        /// `exp(-sigma * span)` whatever the step distribution and the scattered terms summing to
        /// `colour * (1 - T)`. So this asserts the analytic answer and not the march's approximation of it.
        TEST_F(RtxVisibilityTest, fogTakesWhatBeerLambertSaysOverThePathAndGivesItsOwnColourBack)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float distance = 2000.0f;
            constexpr float extinction = 3.5e-4f;

            const auto look = [&](float thickness) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness);

                const SceneDesc scene = makeWall();
                const Frame frame = shoot(scene, {}, camera, size);
                return std::array<int, 3>{ frame.byte(centre), frame.byte(centre + 1), frame.byte(centre + 2) };
            };

            // The wall is the tests' grey, an albedo of 0.5, and the cell's ambient is all that is on
            // it: 0.5 * 0.6 = 0.3. Over that sits `exp(-3.5e-4 * 2000)` = 0.4966 of transmittance,
            // with the rest of the path's worth of the fog's own colour in front of it.
            const float transmittance = std::exp(-extinction * distance);
            const std::array<int, 3> fogged = look(extinction);
            constexpr float wall = 0.5f * sFoggySky;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto expected
                    = static_cast<int>(encodeSrgb(wall * transmittance + sHaze[channel] * (1.0f - transmittance)));

                EXPECT_NEAR(fogged[channel], expected, 1) << "channel " << channel;
            }

            // The three channels also have to come apart the way the fog's colour does, or a grey
            // haze would pass the arithmetic above by accident.
            EXPECT_LT(fogged[0], fogged[1]) << "the fog's own colour, showing through";
            EXPECT_LT(fogged[1], fogged[2]);

            // And no fog is *exactly* no fog. A lit surface with air over it is a differently lit
            // one, which is why every test here that measures radiance leaves this at zero.
            const std::array<int, 3> clear = look(0.0f);
            for (const int level : clear)
                EXPECT_EQ(level, int{ encodeSrgb(wall) }) << "with no fog in the cell";
        }

        /// What the water shows fades through the air its ray crosses to get there, as that thing would fade
        /// seen across the same air directly.
        ///
        /// The froxel volume holds the eye's rays and no other, so a leg a surface sends on is charged no air:
        /// across a fogged bay the far shore showed in the water under a wall of fog that hid the shore itself.
        ///
        /// An emissive ceiling two hundred units over flat water, at two brightnesses, so that what it puts into
        /// the pixel is the difference of the two and the water's own column and Fresnel share cancel. Through
        /// even air of `sigma` that difference is the airless one times the transmittance over the whole path.
        /// The haze is the sky's own grey, so what the air scatters in cancels too, and the layer is lifted so
        /// high that its falloff over two hundred units is under a hundred-thousandth.
        ///
        /// Twice: a unit above the water looking down, the reflection, `exp(-sigma * 201)`; and a unit below
        /// looking up through Snell's window, the refraction, which crosses the two hundred and then water,
        /// `exp(-sigma * 200)`.
        TEST_F(RtxVisibilityTest, whatTheWaterShowsFadesThroughTheAirItsRayCrosses)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float sigma = 2.0e-3f;

            struct Row
            {
                const char* mWhat;
                float mEye;
                float mAir;
            };

            for (const Row row : { Row{ "the reflection", 1.0f, 201.0f }, Row{ "the refraction", -1.0f, 200.0f } })
            {
                const auto shown = [&](float extinction, float glow) {
                    SceneDesc scene = makeOpenWater(4000.0f);
                    addQuad(scene, sheetAt(4000.0f, 200.0f),
                        scene.addMaterial(
                            Material{ .mEmissiveColour = osg::Vec3f(glow, glow, glow), .mTwoSided = true }));

                    Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -0.01f, row.mEye),
                        osg::Vec3f(0.0f, 0.0f, row.mEye > 0.0f ? 0.0f : 200.0f), 60.0f, size, size, 100000.0f);
                    litThroughFog(camera, extinction, 0.0f);
                    camera.mFogColour = camera.mSkyHorizon;
                    camera.mFogLift = 1.0e4f;

                    return shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } })
                        .at(centre);
                };

                const float clear = shown(0.0f, 2.0f) - shown(0.0f, 1.0f);
                const float fogged = shown(sigma, 2.0f) - shown(sigma, 1.0f);
                ASSERT_GT(clear, 1.0e-3f) << row.mWhat << " shows nothing of the ceiling";

                EXPECT_NEAR(fogged / clear, std::exp(-sigma * row.mAir), 0.01f) << row.mWhat;
            }
        }

        /// The fog layer sits on the water, thins with height above it, and stops at the surface.
        ///
        /// Fog gathers over water and drains off high ground, so the level a cell records is where its layer
        /// sits, at it and not in it: a point under the surface already has the water's own absorption, and fog
        /// there would put grey between the eye and the seabed twice over.
        TEST_F(RtxVisibilityTest, theFogLayerSitsOnTheWaterThinsAboveItAndStopsAtIt)
        {
            constexpr std::uint32_t size = framedInColumns(4);
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float distance = 2000.0f;
            constexpr float extinction = 3.5e-4f;

            // The ray runs level at z = 0, so how much fog it crosses is decided by where the layer
            // is put under it and by nothing else.
            const auto look = [&](float level, float thickness) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness, level);

                const SceneDesc scene = makeWall();
                const Frame frame = shoot(scene, {}, camera, size);
                return std::array<int, 3>{ frame.byte(centre), frame.byte(centre + 1), frame.byte(centre + 2) };
            };

            // A dry cell is handed minus infinity and falls back to sea level, which is where a water level of
            // zero puts the layer anyway, so the two hold the same air above the base and must draw the same
            // frame through it.
            //
            // Above the base and not on it: below it a dry cell's layer is capped at its full strength and a
            // flooded one holds no air at all. A ray along the base samples both sides of that step (a column
            // has width and the tent reaches a column either side, some five hundred units of height at the far
            // end here), so the two read up to thirteen levels apart there and agree exactly six hundred units
            // over it.
            constexpr float clearOfTheBase = 600.0f;
            const auto lookAbove = [&](float level) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -distance, clearOfTheBase),
                    osg::Vec3f(0.0f, 0.0f, clearOfTheBase), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, extinction, level);

                // Stretched, because the ray runs six hundred units over the middle of the wall the
                // rest of this test is measured against.
                const SceneDesc scene = makeWall(20.0f);
                const Frame frame = shoot(scene, {}, camera, size);
                return std::array<int, 3>{ frame.byte(centre), frame.byte(centre + 1), frame.byte(centre + 2) };
            };

            const std::array<int, 3> dry = lookAbove(-std::numeric_limits<float>::infinity());
            const std::array<int, 3> atSeaLevel = lookAbove(0.0f);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(dry[channel], atSeaLevel[channel], 1) << "channel " << channel << ", the dry-cell fallback";

            // Three thousand units under the ray, so it runs `exp(-3000 / 2600)` = 0.3154 of the way
            // up the layer's own falloff and crosses less than a third of the fog.
            constexpr float below = 3000.0f;
            const float thinned = extinction * std::exp(-below / Shaders::FOG_HEIGHT);
            const float transmittance = std::exp(-thinned * distance);

            const std::array<int, 3> high = look(-below, extinction);
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto expected = static_cast<int>(
                    encodeSrgb(0.5f * sFoggySky * transmittance + sHaze[channel] * (1.0f - transmittance)));

                EXPECT_NEAR(high[channel], expected, 1) << "channel " << channel << ", high over the layer";
            }

            // And with the surface over the ray instead there is no air to fog at all: the frame is
            // whatever the water did to it and nothing else, whatever the cell's fog says.
            const std::array<int, 3> submerged = look(100.0f, extinction);
            const std::array<int, 3> withoutFog = look(100.0f, 0.0f);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_EQ(submerged[channel], withoutFog[channel]) << "channel " << channel << ", under the surface";

            // **The falloff itself, which no level ray crosses.** Every expectation above holds the
            // ray at one height, so the layer is one number along it; a ray that climbs out of the
            // layer crosses the whole curve, and what it is charged for is its own length times the
            // mean of `exp(-z / FOG_HEIGHT)` over the heights it passed.
            constexpr float climb = 2000.0f;
            const auto lookSloping = [&](const osg::Vec3f& eye, const osg::Vec3f& target) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(eye, target, 60.0f, size, size, 100000.0f);
                litThroughFog(camera, extinction);

                // Stretched, because a ray leaving at forty-five degrees meets the wall two thousand
                // units off its middle and the wall is four hundred across.
                const SceneDesc scene = makeWall(20.0f);
                const Frame frame = shoot(scene, {}, camera, size);
                return std::array<int, 3>{ frame.byte(centre), frame.byte(centre + 1), frame.byte(centre + 2) };
            };

            // Two thousand up over two thousand along is a path of 2828.43, and the layer's mean
            // over the first 2000/2600 of its falloff is (1 - exp(-0.76923)) / 0.76923 = 0.69762 —
            // so 1973.1 units' worth of air at the base's own density, and `exp(-3.5e-4 * 1973.1)`
            // is 0.50125 of transmittance.
            const float slope = std::sqrt(distance * distance + climb * climb);
            const float crossed = climb / Shaders::FOG_HEIGHT;
            const float climbing = std::exp(-extinction * slope * (1.0f - std::exp(-crossed)) / crossed);

            const std::array<int, 3> up = lookSloping(osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, climb));
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto expected
                    = static_cast<int>(encodeSrgb(0.5f * sFoggySky * climbing + sHaze[channel] * (1.0f - climbing)));

                EXPECT_NEAR(up[channel], expected, 1) << "channel " << channel << ", climbing out of the layer";
            }

            // And a ray descending the same two heights crosses the same air, which is the other
            // half of the integral and the one a sign error would have shown up in. Within a level:
            // the volume samples each slice at a jittered point rather than integrating the curve,
            // and the two rays cross the slices at different heights.
            const std::array<int, 3> down
                = lookSloping(osg::Vec3f(0.0f, -distance, climb), osg::Vec3f(0.0f, 0.0f, 0.0f));
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(down[channel], up[channel], 1) << "channel " << channel << ", the same heights descending";
        }

        /// An eye under the surface has no air in front of it, and the volume must say so too.
        ///
        /// `fogDensityAt` gives nothing under the surface and `fogColumn` integrates nothing there, but the
        /// volume accumulates along a column's ray and a froxel the surface stands inside draws its sample from
        /// the air the column found, which for a column aimed up out of the water is the air above it. A pixel
        /// under the water then paid for that air in a band along the waterline (135 pixels here, the worst 61
        /// of 255 out).
        ///
        /// Asserted as the whole frame against the same frame with no weather in the cell, the only exact form
        /// the claim has: a submerged eye owes the fog nothing and every byte must match.
        TEST_F(RtxVisibilityTest, theVolumesAirIsNotOverAnEyeUnderTheSurface)
        {
            constexpr std::uint32_t size = 65;

            // Level at fifty units down, so the frame holds the surface overhead, the bed below and
            // the waterline between them — which is where the band was.
            const SceneDesc scene = makeFlooded(8000.0f, 400.0f);

            const auto look = [&](float extinction) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -1000.0f, -50.0f), osg::Vec3f(0.0f, 0.0f, -50.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, extinction, 0.0f);
                camera.mFogUniform = sVolumeOverEvenAir;

                return shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } }).bytes();
            };

            const std::vector<std::uint8_t> foggy = look(3.5e-4f);
            const std::vector<std::uint8_t> clear = look(0.0f);

            ASSERT_EQ(foggy.size(), clear.size());
            for (std::uint32_t row = 0; row < size; ++row)
                for (std::uint32_t column = 0; column < size; ++column)
                {
                    const std::size_t at = (std::size_t{ row } * size + column) * 4;
                    ASSERT_EQ(foggy[at], clear[at]) << "row " << row << ", column " << column;
                }
        }

        /// A lamp lights the air it stands in, and by the isotropic share of what it delivers.
        ///
        /// `INV_FOUR_PI` is what this is about: a lamp reaches a point in the fog as irradiance, as it reaches a
        /// surface, and what comes back toward the eye is that spread over the whole sphere. Dropping the factor
        /// lights the air 4pi times over (the centre pixel goes from 121 of 255 to 211).
        ///
        /// The lamp is far enough away that its falloff is flat along the ray, so the march's answer is
        /// analytic: the scattered terms telescope to `E / 4pi * (1 - T)`. Twenty thousand units off a
        /// two-hundred-unit ray varies the irradiance by 0.02%.
        TEST_F(RtxVisibilityTest, aLampLightsTheAirItStandsInByTheIsotropicShareOfWhatItDelivers)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float distance = 200.0f;
            constexpr float reach = 30000.0f;

            // Half the ray's worth of fog, so the wall and the air contribute comparably.
            const float extinction = std::log(2.0f) / distance;

            // Behind the wall in y, so its cosine there is negative and the lamp lights the air
            // without also lighting what the air is in front of.
            const osg::Vec3f lamp(0.0f, 100.0f, 20000.0f);

            const osg::Vec3f middle(0.0f, -0.5f * distance, 0.0f);
            const float delivered = lampDelivered((lamp - middle).length(), reach);

            const auto look = [&](bool lit, bool shaded = false, std::uint32_t lampClass = Shaders::MASK_EVERY_CLASS) {
                SceneDesc scene = makeWall();

                // A lid between the ray and the lamp, high enough to be nowhere near what the eye
                // sees and squarely across every ray the march sends up at the light.
                if (shaded)
                    addQuad(scene, sheetAt(40000.0f, 1000.0f));

                if (lit)
                    scene.addLight(Light{
                        .mPosition = lamp,
                        // Scaled so the lamp delivers exactly one unit of irradiance to the ray,
                        // which is what lets the expectation below be written without it.
                        .mIntensity = osg::Vec3f(1.0f, 1.0f, 1.0f) / delivered,
                        .mReach = reach,
                        .mTraits = Shaders::lightTraits(false, lampClass),
                    });

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, extinction);
                // The statics alone, which the wall is: the classes a lamp may hang under besides.
                camera.mRayMask = Shaders::MASK_STATIC;

                // Black air, so the lamp is the only thing in the frame the fog scatters and the
                // expectation below needs no term for the haze `litThroughFog` would otherwise put
                // in it.
                camera.mFogColour = osg::Vec3f();

                const Frame frame = shoot(scene, {}, camera, size);
                return int{ frame.byte(centre) };
            };

            // With no lamp the air scatters nothing, because the fog's own colour is black here: the
            // wall is all there is, at half of it.
            constexpr float wall = 0.5f * sFoggySky;
            EXPECT_EQ(look(false), int{ encodeSrgb(0.5f * wall) }) << "black air over a lit wall";

            // And with it, one unit of irradiance through an isotropic sphere, over the half of the
            // ray's light the fog took: 0.15 + 1 / (4 pi) * 0.5 = 0.18979, which encodes to 121.
            const float scattered = 0.25f * Shaders::INV_PI * 0.5f;
            EXPECT_NEAR(look(true), int{ encodeSrgb(0.5f * wall + scattered) }, 1) << "the lamp in the air";

            // And nothing at all through a lid, the whole of what the march's one ray buys: every lamp at every
            // step is weighed into one reservoir and the one held is traced to, so a lantern behind something
            // stops lighting the air in front of it. Against the same lid with no lamp, because the lid also
            // stands over the wall: the wall's light is one bounce, which the lid stops, and the lamp lights
            // only the lid's upper face, so the frames differ only where the lid failed to stop the lamp.
            EXPECT_EQ(look(true, true), look(false, true)) << "a lamp behind a lid still lit the air";

            // **And nothing from a lamp whose class the view hides**, as the surfaces' walk drops it:
            // an actor's lamp under a view of the statics.
            EXPECT_EQ(look(true, false, Shaders::MASK_ACTOR), look(false)) << "a hidden lamp lit the air";
            EXPECT_NEAR(look(true, false, Shaders::MASK_STATIC), int{ encodeSrgb(0.5f * wall + scattered) }, 1)
                << "a shown lamp left the air";
        }

        /// Where the volume's own air stands, how thick it is, and what lights it.
        ///
        /// One fixture for the two tests below, which differ in the lid and nothing else. Both stand a lamp
        /// beside a two thousand unit ray with a reach that covers eight hundred units of it, less than the
        /// stretch a single probe of the ray would answer for; a lamp reaching the whole ray asks the volume no
        /// question the closed form has not answered.
        ///
        /// A narrow field of view, because a column is `FOG_VOLUME_SCALE` pixels wide: at sixty degrees a
        /// column of a thirty-three pixel frame spans twenty-two, so the ray the middle pixel reads its air
        /// along leaves the ray it was traced along by hundreds of units. Ten degrees puts that under forty,
        /// small against a reach of five hundred.
        struct LampInTheAir
        {
            /// How far the wall stands, and so how long the ray the middle pixel reads is.
            static constexpr float sDistance = 2000.0f;

            /// Half the ray's light, which leaves the wall and the air comparable.
            static constexpr float sExtinction = 0.693147f / sDistance;

            /// Where the lamp stands and how far it carries. Three hundred units up off the ray and
            /// a reach of five hundred, so it lights the stretch of it between 385 and 1215.
            static inline const osg::Vec3f sLamp{ 0.0f, -1200.0f, 300.0f };
            static constexpr float sReach = 512.0f;

            /// What it delivers where the ray passes closest, which the intensity is scaled to
            /// rather than named — so the expectations below need no falloff in them.
            static constexpr float sIrradiance = 12.0f;
            static inline const osg::Vec3f sIntensity
                = osg::Vec3f(1.0f, 1.0f, 1.0f) * (sIrradiance / lampDelivered(sLamp.z(), sReach));

            /// How high the lid hangs. Between the ray and the lamp, so a shadow ray from a point
            /// on the ray crosses it half way along.
            static constexpr float sLidHeight = 150.0f;
        };

        /// A lamp behind a lid lights none of the volume's air either.
        ///
        /// The closed form weighs every lamp of the whole ray into one reservoir, so a lid over the march takes
        /// the whole lamp with it. The volume answers a froxel at a time, and the question is whether the
        /// seeing it charges a froxel is the seeing at that froxel.
        ///
        /// A short reach makes it sharp: a probe drawn outside the lamp's reach finds no lamp, buys no ray, and
        /// `lampVisible` answers one for want of anything to trace, while every slice of the stretch still sums
        /// the lamp where it stands, so a lantern behind a lid would light the air in front of it. The lid
        /// stands in both frames, since it also covers part of the sky the wall gathers.
        TEST_F(RtxVisibilityTest, aLampBehindALidLightsNoneOfTheVolumesAirEither)
        {
            using Fixture = LampInTheAir;

            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            const auto look = [&](bool lit, bool lidded) {
                SceneDesc scene = makeWall();

                if (lidded)
                    addQuad(scene, makeLid(Fixture::sLidHeight, -4000.0f, 4000.0f, 4000.0f));

                if (lit)
                    scene.addLight(Light{
                        .mPosition = Fixture::sLamp,
                        .mIntensity = Fixture::sIntensity,
                        .mReach = Fixture::sReach,
                    });

                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -Fixture::sDistance, 0.0f),
                    osg::Vec3f(0.0f, 0.0f, 0.0f), 10.0f, size, size, 100000.0f);
                litThroughFog(camera, Fixture::sExtinction);
                camera.mFogUniform = sVolumeOverEvenAir;

                // Black air, so the lamp is the only thing the fog has to scatter and the two
                // frames below can be compared as they stand.
                camera.mFogColour = osg::Vec3f();

                // **Averaged, because one frame is one draw of the probe.** What is asserted is
                // where the leak sits on average; a single frame either leaked or did not, and
                // which is a coin.
                const Frame frame = shoot(scene, {}, camera, size, { .mFrames = 32 });
                return int{ frame.byte(centre) };
            };

            const int dark = look(false, true);
            const int lidded = look(true, true);
            const int open = look(true, false);

            EXPECT_EQ(lidded, dark) << "the lid takes the whole lamp out of the air, not most of it";
            EXPECT_GT(open, dark + 20) << "and there was a lamp in the air to take";
        }

        /// The air under a lamp settles instead of flickering block by block.
        ///
        /// A froxel stands for a column's pixels squared, so an estimator that decides one thing for a stretch
        /// of a column paints that decision across a block and redecides it next frame. The complaint is not a
        /// wrong mean but frames that do not stand still, so this measures how far a settled pixel moves from
        /// one frame to the next: the step and not the spread, because the volume averages nine tenths of its
        /// history in, so consecutive frames are strongly correlated and a standard deviation over a short run
        /// mostly estimates that correlation.
        ///
        /// Half the lit stretch is under a lid so the seeing genuinely changes along the ray: with none
        /// shadowed a probe has nothing to be wrong about, and with all shadowed the test above asks.
        ///
        /// The bound is a bound and not the figure: this settles at 0.4% of the pixel's value, where one probe
        /// answering for a stretch of eight slices swings by about half the lamp's whole term every frame, which
        /// the history takes to a tenth of the pixel. Three per cent stands between with room either side. The
        /// same estimator with the history off measures 5.9%, which is what the temporal half is worth here.
        TEST_F(RtxVisibilityTest, theVolumeSettlesTheAirUnderALampRatherThanFlickeringBlockByBlock)
        {
            using Fixture = LampInTheAir;

            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreOf(size);

            // Long enough that the history is settled well before the window, and that the window
            // holds many of the ten-frame spans the history's own weight correlates over.
            constexpr std::size_t frames = 192;
            constexpr std::size_t settled = 64;

            SceneDesc scene = makeWall();

            // Over the near half of the stretch the lamp reaches and no further. A shadow ray
            // crosses this height half way to the lamp, so what it covers is every point of the ray
            // short of the lamp's own y and nothing beyond it.
            addQuad(scene, makeLid(Fixture::sLidHeight, -Fixture::sDistance, Fixture::sLamp.y(), 2000.0f));

            scene.addLight(Light{
                .mPosition = Fixture::sLamp,
                .mIntensity = Fixture::sIntensity,
                .mReach = Fixture::sReach,
            });

            Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -Fixture::sDistance, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 0.0f), 10.0f, size, size, 100000.0f);
            litThroughFog(camera, Fixture::sExtinction);
            camera.mFogUniform = sVolumeOverEvenAir;

            // Nothing in the frame but the lit air, so the figure below is about the air: black fog, black sky
            // and no ambient leave the wall unlit and every missing ray black, where a lit wall's single
            // undenoised bounce would move further between frames than anything measured here.
            camera.mFogColour = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;

            std::vector<float> radiance;
            shoot(scene, {}, camera, size,
                Shot{ .mFrames = frames,
                    .mAverage = false,
                    .mLoss = HistoryLoss::Cut,
                    .mEachFrame = [&](const Frame& each) { radiance.push_back(each.at(centre * 4)); } });

            double total = 0.0;
            double stepped = 0.0;
            for (std::size_t frame = frames - settled; frame < frames; ++frame)
            {
                total += double{ radiance[frame] };
                stepped += std::abs(double{ radiance[frame] } - double{ radiance[frame - 1] });
            }

            const double mean = total / double{ settled };
            const double step = stepped / double{ settled };

            // The lamp has to be lighting the air, or a frame that stands perfectly still is a
            // frame with nothing in it.
            ASSERT_GT(mean, 0.02) << "the lit air the flicker is measured against";

            EXPECT_LT(step / mean, 0.03) << "how far a settled pixel of lit air moves between frames";
        }

        /// The air beside an edge holds its lamp light still, and as bright as open air. A froxel's lamps are
        /// integrated along its column's middle ray and their image keeps no history, so the stretch they are
        /// integrated over has to end where that ray ends, on every frame.
        ///
        /// Two posts 760 units out with a slit 44 units wide between them, and the centre pixel looking through
        /// it at a black wall. Both columns the pixel reads have their middle ray in the slit and their outer
        /// two or three pixels behind a post, so the jittered ray each draws meets a post on a quarter to
        /// three eighths of the frames. A lamp twenty units over the ray at 800, with a reach of a hundred,
        /// puts nearly all its light into the slice from 732 to 886, which a stretch cut at the post holds none of.
        ///
        /// Measured against the same air with no posts, which holds still to the halves it is read through: a
        /// value stored into a half may round either way (Vulkan leaves it to the implementation, AMD's rounds
        /// toward nought), so a settled froxel may step by `2^-10` of itself at most (2.6e-5 measured on a
        /// low-end AMD integrated GPU). Cut where the jittered ray stopped, the pixel beside the posts stepped
        /// by 20% of itself and settled 12% darker than open air; cut where the middle ray stops, it is the open
        /// air's to 1.5 per cent and steps by 2.9%, the draws of the shadow rays the posts' edges stop.
        TEST_F(RtxVisibilityTest, theLampLightBesideAnEdgeHoldsStillAndAsBrightAsOpenAir)
        {
            constexpr std::uint32_t size = framedInColumns(4);
            constexpr std::size_t centre = centreOf(size);
            constexpr std::size_t frames = 96;
            constexpr std::size_t settled = 48;
            constexpr float eye = -2000.0f;
            constexpr float posts = eye + 760.0f;
            // A column being `2 * 760 * tan 5° / 4` = 33 units out there, a little less for the frame's
            // last pixel: the columns' middles, half a column either side of the centre, stand inside.
            constexpr float slit = 22.0f;
            constexpr float reach = 100.0f;
            const osg::Vec3f lamp(0.0f, eye + 800.0f, 20.0f);

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, eye, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 10.0f, size, size, 100000.0f);
            litThroughFog(camera, LampInTheAir::sExtinction);
            // **Even air, with no band**: the open air is to hold exactly still, and the band, drawn at
            // points that move from frame to frame, moved it by parts in a hundred thousand.
            camera.mFogUniform = 1.0f;
            camera.mFogColour = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;

            struct Settled
            {
                double mMean = 0.0;
                double mStep = 0.0;
            };

            const auto run = [&](bool withPosts) {
                SceneDesc scene = makeWall();
                if (withPosts)
                    for (const float side : { -1.0f, 1.0f })
                        addQuad(scene,
                            std::array{ osg::Vec3f(side * slit, posts, -500.0f),
                                osg::Vec3f(side * 500.0f, posts, -500.0f), osg::Vec3f(side * 500.0f, posts, 500.0f),
                                osg::Vec3f(side * slit, posts, 500.0f) });
                scene.addLight(Light{
                    .mPosition = lamp,
                    .mIntensity = osg::Vec3f(1.0f, 1.0f, 1.0f) * (48.0f / lampDelivered(lamp.z(), reach)),
                    .mReach = reach,
                });

                std::vector<float> radiance;
                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = frames,
                        .mAverage = false,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame& each) { radiance.push_back(each.at(centre * 4)); } });

                Settled out;
                for (std::size_t frame = frames - settled; frame < frames; ++frame)
                {
                    out.mMean += double{ radiance[frame] };
                    out.mStep += std::abs(double{ radiance[frame] } - double{ radiance[frame - 1] });
                }
                out.mMean /= double{ settled };
                out.mStep /= double{ settled } * out.mMean;
                return out;
            };

            const Settled open = run(false);
            const Settled edged = run(true);
            ASSERT_GT(open.mMean, 0.02) << "the lamp lights the air the pixel looks through";
            EXPECT_LT(open.mStep, 0x1p-10) << "the open air holds still, so a step beside the posts is theirs";
            EXPECT_NEAR(edged.mMean / open.mMean, 1.0, 0.02) << "the slit's air is lit as the open air is";
            EXPECT_LT(edged.mStep, 0.1) << "how far the pixel beside the posts moves between frames";
        }

        /// The air's history turns once a trace, and a frame closed untraced turns nothing. A trace writes one
        /// half of the air and reads the other as its history, so the trace after a frame the ring closed with
        /// no trace reads what the trace before it wrote: forty frames with one such frame in the middle end on
        /// the air forty frames end on without it, to the bit, and the same forty with the past lost at that
        /// frame end elsewhere.
        TEST_F(RtxVisibilityTest, aFrameClosedUntracedLeavesTheAirsHistoryWhereTheLastTraceLeftIt)
        {
            using Fixture = LampInTheAir;

            constexpr std::uint32_t size = 17;
            SceneDesc scene = makeWall();
            scene.addLight(Light{
                .mPosition = Fixture::sLamp,
                .mIntensity = Fixture::sIntensity,
                .mReach = Fixture::sReach,
            });

            Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(0.0f, -Fixture::sDistance, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 0.0f), 10.0f, size, size, 100000.0f);
            litThroughFog(camera, Fixture::sExtinction);
            camera.mFogUniform = sVolumeOverEvenAir;

            enum class Between
            {
                Nothing,
                Untraced,
                Lost,
            };
            const auto lastFrame = [&](const Between between) {
                return shoot(scene, {}, camera, size,
                    Shot{ .mFrames = 40,
                        .mAverage = false,
                        .mLoss = between == Between::Lost ? HistoryLoss::Cut : HistoryLoss::None,
                        .mLossAt = 20,
                        .mEachFrame =
                            [&, at = 0](const Frame&) mutable {
                                if (++at == 20 && between == Between::Untraced)
                                {
                                    mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                                    mRenderer.skipFrame();
                                }
                            } })
                    .mRadiance;
            };

            const std::vector<float> straight = lastFrame(Between::Nothing);
            EXPECT_EQ(lastFrame(Between::Untraced), straight) << "the untraced frame turned the air";
            EXPECT_NE(lastFrame(Between::Lost), straight) << "a history that changes nothing";
        }

        /// The volume lights the air up to a surface, wherever inside a slice the surface stands.
        ///
        /// A froxel grid gets the last slice wrong two ways: sampled on both sides of the surface unless the
        /// sampling knows where it is (`fogdepth.rgen`), and what a pixel reads between two slices' edges must
        /// be a shape the integrate pass agreed to (`FogSlice`). Either error depends on where inside its slice
        /// the surface stands, so the wall is put at four depths across three slices and held against the
        /// closed form: half and nine tenths of the way through the slice from 732 to 886 units, a tenth of the
        /// way through the next, and a quarter of the way through the one from 1055.
        ///
        /// The lamp stands in front of the wall at every one, so a draw landing behind the wall finds the lamp
        /// hidden (the contamination a froxel sampled on both sides carries, strongest where the lamp is
        /// brightest); and not behind it, since the closed form judges a whole ray by one shadow ray from the
        /// lamp's closest approach. The wall's own light, which the lamp lights identically on both paths, is
        /// taken off: a frame with no air measures the wall alone, and the air in front takes the transmittance
        /// the closed form states.
        ///
        /// Five per cent, where sampling on both sides of the wall lost seven to eleven per cent of the lit air
        /// at the first three depths, and the line the sampler draws through an inverse square is a few per cent
        /// from the closed form over a slice at the lamp's closest approach. Eight frames averaged after
        /// twenty-four: the froxels' history is the same on both paths so the ratio cancels it (within 0.09% of
        /// one at every depth), and the wall alone keeps no history so it draws only the frames it averages.
        TEST_F(RtxVisibilityTest, theVolumeLightsTheAirUpToASurfaceWhereverInASliceItStands)
        {
            using Fixture = LampInTheAir;

            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreOf(size);
            constexpr std::uint32_t frames = 32;
            constexpr std::uint32_t settled = 8;

            // The lamp stands where the fixture puts it relative to the eye, whatever the wall's
            // distance, so the air it lights is the same air in every frame and only where the wall
            // cuts it off moves.
            const float ahead = Fixture::sDistance + Fixture::sLamp.y();

            const auto settle = [&](float distance, bool banked, bool fogged) {
                SceneDesc scene = makeWall();
                scene.addLight(Light{
                    .mPosition = osg::Vec3f(0.0f, ahead - distance, Fixture::sLamp.z()),
                    .mIntensity = Fixture::sIntensity,
                    .mReach = Fixture::sReach,
                });

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 10.0f, size, size, 100000.0f);
                litThroughFog(camera, fogged ? Fixture::sExtinction : 0.0f);
                camera.mFogUniform = banked ? sVolumeOverEvenAir : 1.0f;

                // Nothing in the frame but the lamp: black air, a black sky and no ambient, for the
                // reason the test above gives.
                camera.mFogColour = osg::Vec3f();
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbientFromSky = 0.0f;

                const std::uint32_t drawn = fogged ? frames : settled;
                std::vector<float> radiance;
                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = drawn,
                        .mAverage = false,
                        .mFirstFrame = frames - drawn,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame& each) { radiance.push_back(each.at(centre * 4)); } });

                double total = 0.0;
                for (std::uint32_t frame = drawn - settled; frame < drawn; ++frame)
                    total += double{ radiance[frame] };
                return total / double{ settled };
            };

            for (const float distance : { 809.0f, 871.0f, 903.0f, 1101.0f })
            {
                const double wall = settle(distance, false, false);
                const double through = std::exp(-double{ Fixture::sExtinction } * double{ distance });

                const double closed = settle(distance, false, true) - wall * through;
                const double volume = settle(distance, true, true) - wall * through;

                ASSERT_GT(closed, 0.01) << "the lit air the volume is measured against, at " << distance;
                EXPECT_NEAR(volume / closed, 1.0, 0.05)
                    << "the volume's lit air against the closed form's, at " << distance;
            }
        }

        /// The air a step uncovers behind a surface is this frame's air. A column's ray is one of its block's, so
        /// a pixel beside the surface that ray met sees on past it into the column's further slices; a froxel
        /// there that kept what it had held the light of the last frame the column's ray reached it (under a
        /// clock run fast, an hour's light round every silhouette).
        ///
        /// A pillar two hundred units in front of the eye. The air settles in one colour seen from 200 units up
        /// (the air has to have been seen, or a froxel holds nothing, a darker rim and not an earlier hour's
        /// light); the eye comes down behind the pillar and the air turns to another colour; the eye then
        /// steps 150 units aside and looks past the pillar's edge at 60. That frame is held against the same
        /// frame of a run with the second colour all along, to the closed form's five per cent. Black sky, no
        /// ambient, no lamp: the pixel is the air alone, its colour times `1 - exp(-ln 2)`. One scene through
        /// the run, since a scene handed over has no previous frame to reproject from.
        ///
        /// The tolerance: after the step the middle ray is hidden from `150 * 200 / 60 = 500` units on, and
        /// the air from there to the wall holds `exp(-ln 2 / 4) - 0.5 = 0.341` of the colour. Drawn every frame
        /// those froxels carry the first colour at `0.9^65`, `0.341 * 0.3 * 0.00106 = 1.1e-4`, under the
        /// half-float step of `2.4e-4`; kept, they carry it up to `0.341 * 0.3 * 0.9 = 0.092`.
        TEST_F(RtxVisibilityTest, theAirAStepUncoversBehindASurfaceIsThisFramesAir)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreOf(size);
            constexpr float distance = 2000.0f;
            constexpr float step = 150.0f;
            constexpr float over = 200.0f;
            constexpr float pillarAway = 200.0f;
            constexpr float pillarHalf = 60.0f;
            constexpr std::uint32_t settled = 48;
            constexpr std::uint32_t turned = 64;

            const osg::Vec3f before(sHaze.z(), sHaze.y(), sHaze.x());

            SceneDesc scene = makeWall();
            addQuad(scene, uprightQuadAt(pillarHalf, pillarAway - distance));

            const auto eye = [&](float across, float up, const osg::Vec3f& colour) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(across, -distance, up), osg::Vec3f(across, 0.0f, up), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, 0.693147f / distance);
                camera.mFogColour = colour;
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbientFromSky = 0.0f;
                return camera;
            };

            const auto air = [&](const Frame& frame) {
                return osg::Vec3f(frame.at(centre * 4), frame.at(centre * 4 + 1), frame.at(centre * 4 + 2));
            };

            const osg::Vec3f expected = air(shoot(scene, {}, eye(step, 0.0f, sHaze), size,
                Shot{ .mFrames = settled + turned + 1, .mAverage = false, .mLoss = HistoryLoss::Cut }));

            shoot(scene, {}, eye(0.0f, over, before), size,
                Shot{ .mFrames = settled, .mAverage = false, .mLoss = HistoryLoss::Cut });
            shoot(scene, {}, eye(0.0f, 0.0f, sHaze), size,
                Shot{ .mFrames = turned, .mAverage = false, .mFirstFrame = settled, .mSetScene = false });
            const osg::Vec3f seen = air(shoot(scene, {}, eye(step, 0.0f, sHaze), size,
                Shot{ .mFrames = 1, .mAverage = false, .mFirstFrame = settled + turned, .mSetScene = false }));

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                EXPECT_NEAR(expected[channel] / (0.5f * sHaze[channel]), 1.0f, 0.05f)
                    << "the air of the run the other is held against, channel " << channel;
                EXPECT_NEAR(seen[channel], expected[channel], 2e-3f)
                    << "the air the step uncovered behind the pillar, channel " << channel;
            }
        }

        /// The banked field holds as much air as an even one, which is what `FOG_COVERAGE` is for. The noise
        /// redistributes the fog and does not remove it: the extinction is Morrowind's own view distance turned
        /// into a coefficient, and a band leaving 40% of the volume clear would make the world twice as clear as
        /// the game says. Dividing the coverage by its own mean holds the average, and this makes that constant
        /// a measurement, since moving the band without re-measuring moves this ratio by the constant's error.
        ///
        /// It settles just under one (0.969 against a thickness of 0.09), for two reasons and not a mistake:
        /// `exp` is convex and a banked field's optical depth varies more, so more light survives the same
        /// average density (this half scales with thickness); and a far step reads a coarser level, which
        /// `FOG_FIELD_COARSEST` holds to a twentieth, measured by
        /// `everyLevelAMarchMayReadClearsTheShareTheDensityIsDividedBy`. Nine viewpoints, since the steps bunch
        /// near the camera and one frame lands anywhere within six per cent, nine within about two.
        TEST_F(RtxVisibilityTest, theBankedFieldHoldsAsMuchAirAsAnEvenOne)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::size_t count = std::size_t{ size } * size;

            const auto air = [&](float uniform, float where) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(osg::Vec3f(where, -50000.0f, 0.0f),
                    osg::Vec3f(where, -60000.0f, 0.0f), 90.0f, size, size, 100000.0f);
                camera.mFogUniform = uniform;

                const std::vector<float> luminance = airThrough(camera, size);

                double total = 0.0;
                for (const float value : luminance)
                    total += double{ value };

                return total / static_cast<double>(count);
            };

            double ratio = 0.0;
            constexpr std::array places{ 0.0f, 12345.0f, -31000.0f, 77000.0f, 250000.0f, -140000.0f, 33000.0f,
                -420000.0f, 610000.0f };
            for (const float where : places)
                ratio += air(0.0f, where) / air(1.0f, where);

            ratio /= static_cast<double>(places.size());

            EXPECT_NEAR(ratio, 0.969, 0.05) << "banked air against even air, over nine viewpoints";
        }

        /// A store that is not finite is counted against the module that made it, and a sound frame counts
        /// nought. A fog colour of NaN puts one into every froxel that holds air (one scatter store each), and
        /// goes no further: the scatter store is a history rounded into a half (`roundedToHalf`), which counts
        /// the value and clamps it into range, so the integration and the trace are handed a finite number.
        /// That is why every module counts what it is about to store rather than trusting a NaN to show
        /// downstream. A report the renderer drops uncollected hands its count to the next report, and every
        /// frame's count reaches the test's total once, so no frame's store goes unread.
        TEST_F(RtxVisibilityTest, aStoreThatIsNotFiniteIsCountedAgainstItsModule)
        {
            constexpr std::uint32_t size = 8 * Shaders::FOG_VOLUME_SCALE;
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -50000.0f, 0.0f), osg::Vec3f(0.0f, -60000.0f, 0.0f), 90.0f, size, size, 100000.0f);

            camera.mFogColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
            camera.mFogExtinction = 3.0e-6f;
            const Frame clear = shoot(makeWall(), {}, camera, size);
            EXPECT_EQ(clear.mNotFinite.total(), 0u);

            camera.mFogColour = osg::Vec3f(std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f);
            camera.mFogExtinction = 3.0e-6f;
            const Frame frame = shoot(makeWall(), {}, camera, size);

            constexpr std::uint32_t columns = size / Shaders::FOG_VOLUME_SCALE;
            constexpr std::uint32_t froxels = columns * columns * Shaders::FOG_VOLUME_SLICES;
            EXPECT_EQ(frame.mNotFinite.of("fogscatter.rgen"), froxels)
                << "8 by 8 columns by 64 slices, every one in air short of a wall behind the eye";
            EXPECT_EQ(frame.mNotFinite.kernels().size(), 1u) << "no other module stores one";
            EXPECT_EQ(frame.mNotFinite.total(), froxels);
            EXPECT_EQ(mProbe.takeCountedStores().total(), froxels)
                << "the frames whose reports the test read count as well, the clear ones' nought";

            // **And none is lost with a report nothing collected**: the next report the renderer
            // makes carries it. The ring holds a report for each frame it can have in flight, so
            // two frames more than both together drop the reports of the first two, and the
            // reports left carry every frame's stores.
            constexpr std::uint32_t held = 2 * sFramesInFlight;
            constexpr std::uint32_t uncollected = held + 2;
            for (std::uint32_t at = 0; at < uncollected; ++at)
                mProbe.traceFrame(camera, FrameOptions{});
            NotFinite collected;
            std::uint32_t reports = 0;
            while (const std::optional<FrameResult> report = mRenderer.finishFrame())
            {
                collected.add(report->mNotFinite);
                ++reports;
            }
            EXPECT_EQ(reports, held) << "the reports the ring held, and those of the frames still in flight";
            EXPECT_EQ(collected.of("fogscatter.rgen"), uncollected * froxels);
            EXPECT_EQ(collected.total(), uncollected * froxels);
            EXPECT_EQ(mProbe.takeCountedStores().total(), uncollected * froxels)
                << "each frame once, and not again for the stores a report carried";

            // What a test's end reads: every frame, those nothing collected among them, and no
            // report carries their stores after.
            for (std::uint32_t at = 0; at < held + 1; ++at)
                mProbe.traceFrame(camera, FrameOptions{});
            const NotFinite unread = mProbe.takeCountedStores();
            EXPECT_EQ(unread.of("fogscatter.rgen"), (held + 1) * froxels);
            EXPECT_EQ(unread.total(), (held + 1) * froxels);
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "every frame was waited out and dropped";
            EXPECT_EQ(mProbe.takeCountedStores().total(), 0u) << "taken once";

            // A new world drops every report, and the first report of the new world carries them.
            mProbe.traceFrame(camera, FrameOptions{});
            mProbe.traceFrame(camera, FrameOptions{});
            camera.mFogColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
            const Frame newWorld = shoot(makeWall(), {}, camera, size);
            EXPECT_EQ(newWorld.mNotFinite.of("fogscatter.rgen"), 2 * froxels)
                << "the two frames of the world before, and none of the clear frame's own";
            EXPECT_EQ(newWorld.mNotFinite.total(), 2 * froxels);
            EXPECT_EQ(mProbe.takeCountedStores().total(), 2 * froxels);

            // **A door: the frame before stood 80000 units off**, farther than a half float holds,
            // and the reset says that no step from there reaches this frame's motion. The motion
            // took the step and stored an infinite distance at every pixel of the wall.
            const osg::Vec3f away(100.0f, -80000.0f, 0.0f);
            const osg::Vec3f lit(1.0f, 1.0f, 1.0f);
            const SceneDesc wall = makeWall();
            shoot(wall, {}, wallCamera(size, lit, away, away - osg::Vec3f(0.0f, 1.0f, 0.0f)), size);
            const Frame afterDoor
                = shoot(wall, {}, wallCamera(size, lit), size, Shot{ .mLoss = HistoryLoss::Cut, .mSetScene = false });
            EXPECT_GT(afterDoor.mHits, 0u);
            EXPECT_EQ(afterDoor.mNotFinite.total(), 0u);
        }

        /// The wind carries the banks downwind, and a camera that walks with the wind sees the air stand still.
        ///
        /// Exact and not a threshold, because advection is a translation: the field is read at
        /// `position - drift`, the host integrating the drift (`FogDrift`), so an eye moved by that much at the
        /// same moment reads what the unmoved eye reads with no wind, churn and all (the churn is a function of
        /// the moment and not of the wind). Nothing else in the frame knows where the eye is. The sign is the
        /// half that matters: sampling from further upwind as the drift grows carries a bank past, and a
        /// camera moved the wrong way sees a different field, which the last assertion checks.
        TEST_F(RtxVisibilityTest, theWindCarriesTheFieldAndAnEyeThatWalksWithItSeesItStandStill)
        {
            constexpr std::uint32_t size = 8 * Shaders::FOG_VOLUME_SCALE;
            constexpr std::size_t count = std::size_t{ size } * size;
            constexpr float seconds = 2.0f;
            const osg::Vec2f wind(0.3f, 0.4f);
            const osg::Vec2f drift = wind * (seconds * Shaders::FOG_GALE);

            const auto frame = [&](const osg::Vec2f& blown, const osg::Vec3f& eye) {
                Shaders::VisibilityConstants camera
                    = Testing::makeCamera(eye, eye + osg::Vec3f(0.0f, -10000.0f, 0.0f), 90.0f, size, size, 100000.0f);
                camera.mFogUniform = 0.0f;
                const std::array<osg::Vec3f, Shaders::FOG_SCALES> offsets
                    = fogOffsets(osg::Vec2d(blown), static_cast<double>(seconds));
                std::copy(offsets.begin(), offsets.end(), camera.mFogOffsets);

                const std::vector<float> luminance = airThrough(camera, size);
                return luminance;
            };

            const auto apart = [&](const std::vector<float>& a, const std::vector<float>& b) {
                double total = 0.0;
                for (std::size_t i = 0; i < count; ++i)
                    total += std::abs(double{ a[i] } - double{ b[i] });
                return total / static_cast<double>(count);
            };

            const osg::Vec3f eye(0.0f, -50000.0f, 0.0f);
            const osg::Vec3f carried(drift.x(), drift.y(), 0.0f);

            const std::vector<float> still = frame(osg::Vec2f(), eye);
            const std::vector<float> downwind = frame(drift, eye + carried);
            const std::vector<float> upwind = frame(drift, eye - carried);

            // The two read one field at one moment, and differ by the rounding of an eye moved
            // fourteen hundred units against a ray that runs thirty thousand.
            EXPECT_LT(apart(still, downwind), 1e-3) << "an eye walking with the wind sees the air stand still";

            // The wrong sign is two different fields, the gap between them being the banks themselves: 0.03 of
            // the frame's mean of about 0.5 by the march, 0.0087 by the volume, whose tent and slice-to-slice
            // line smooth a single frame's draws, against exactly nought for the right sign.
            EXPECT_GT(apart(still, upwind), 0.005) << "an eye walking against the wind sees another air";
        }

        /// The fog scatters the sun forward far harder than back, which is what a Mie phase is for. A single
        /// Henyey-Greenstein lobe cannot do this shape: real droplets throw a peak within a degree of the light
        /// far above anything one `g` reaches, and still send a sixth of isotropic backwards (the blaze around
        /// a low sun, and fog not going black when you turn away from it).
        ///
        /// A ratio, the only thing the fixture computes exactly: two frames differ only in which side of the
        /// camera the sun is on, at the same elevation, so the column, extinction, transmittance and march
        /// cancel, leaving `p(26.6 degrees) / p(153.4)`. An isotropic fog would give exactly one.
        TEST_F(RtxVisibilityTest, theFogScattersTheSunForwardFarHarderThanBack)
        {
            // Wider than the four columns most tests here use, because the volume answers per column: at four
            // the column the centre pixel reads points six degrees off that pixel's, climbs out of the layer and
            // carries air a fifth thinner than the level ray the closed form is written for. The bias falls with
            // the columns (0.63 at four, 0.53 at sixteen, 0.515 at thirty-two, 0.509 at sixty-four, towards
            // 0.4999) and the ratio does not care, since the transport it divides out is the column's either way.
            constexpr std::uint32_t size = framedInColumns(32);
            constexpr std::size_t centre = centreValueOf(size);

            // Bright enough to read against eight bits after the fog's own column has taken 83% of
            // it, and the forward case still has to stay inside one.
            constexpr float irradiance = 12.7f;

            // Level, so the centre ray holds one height and the medium along it is uniform — which
            // is what lets the two frames cancel to the phase function alone.
            const auto lookPast = [&](float towardsY) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1000.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // The sun a quarter of the way up, ahead of the camera or behind it. Both carry the
                // same climb, so `fogBeamDepth` is the same for each and cancels.
                osg::Vec3f towards(0.0f, towardsY, 0.5f);
                towards.normalize();
                camera.mSun = Shaders::sunSource(towards, osg::Vec3f(irradiance, irradiance, irradiance));
                camera.mFogExtinction = 3.0e-4f;
                camera.mFogUniform = 1.0f;

                // Nothing to hit and nothing else to scatter: every ray runs to `FOG_REACH` through even air
                // whose own colour is black, so the pixel is the sun and no more. Out of reach of the shadow
                // rays and not merely out of the frame: a wall at y = 0 is in the path of the *backward* case's
                // shadow rays and would shadow the near steps at a dither-dependent offset. A sheet below the
                // world is past `mFar` in every direction any ray here travels.
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, -200000.0f));

                const Frame frame = shoot(scene, {}, camera, size);

                return frame.at(centre);
            };

            const float ahead = lookPast(1.0f);
            const float behind = lookPast(-1.0f);

            // Hand-computed from Jendersie and d'Eon's fit at a droplet diameter of eight microns:
            // the blend is 47.4% Draine over a Henyey-Greenstein peak of g = 0.98447, and at
            // `cos = +-0.8944` that comes to 0.225158 forward against 0.011708 back.
            constexpr float forward = 0.225158f;
            constexpr float backward = 0.011708f;

            EXPECT_NEAR(ahead / behind, forward / backward, 1.0f) << "forward against backward scattering";

            // And the absolute value, because a ratio cannot see a factor of `4 pi`: normalise the phase so
            // isotropic reads one (a lamp's convention) and both frames grow together. What the eye gets is the
            // sun's irradiance times the phase per steradian, less the fog's own column on the way down, over a
            // ray that runs to `FOG_REACH`:
            //
            //   12.7 * 0.225158 * exp(-2600 * 3e-4 / 0.44721) * (1 - exp(-3e-4 * 30000)) = 0.4998
            //
            // which the sRGB curve puts at 188 of 255; four pi times that is white. The tolerance is what a
            // frame thirty-two columns across has left over: 0.515 measured against 0.4999, falling as it grows.
            const float climb = 0.5f / std::sqrt(1.25f);
            const float column = std::exp(-Shaders::FOG_HEIGHT * 3.0e-4f / climb);
            const float crossed = 1.0f - std::exp(-3.0e-4f * Shaders::FOG_REACH);

            EXPECT_NEAR(ahead, irradiance * forward * column * crossed, 0.02f)
                << "the sun's own irradiance through the phase function, per steradian";

            // And the backward half is not nothing, which is the other half of why a single lobe
            // will not do: fog behind you still glows.
            EXPECT_GT(behind, 0.01f) << "a sixth of isotropic still comes back";
        }

        /// A lid over the march takes the sun out of the air beneath it, and takes all of it. Exactly what
        /// sunless air scatters, since a shaft that leaked a tenth of the sun would still be darker than open
        /// sky and pass an assertion that only asked for darker. The lid spans the whole march and the camera's
        /// level ray never reaches it (five hundred units overhead), so only the shadow ray differs.
        TEST_F(RtxVisibilityTest, aLidOverTheMarchTakesTheSunOutOfTheAirBeneathIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float irradiance = 4.0f;

            const auto look = [&](bool lidded, bool lit) {
                // The same sheet either way, over the march or under it, so the frames differ in what the shadow
                // ray finds and nothing else, not in what is in the scene nor its size. Two-sided, since a roof
                // in the game has an underside the content modelled and the eye meets the sheet from either side.
                SceneDesc scene;
                addQuad(scene, sheetAt(40000.0f, lidded ? 500.0f : -500.0f),
                    scene.addMaterial(Material{ .mTwoSided = true }));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1000.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // Ahead of the camera and well up, so the phase function has something to scatter
                // forward and every shadow ray still climbs into the lid.
                osg::Vec3f travelling(0.0f, -0.6f, -0.8f);
                travelling.normalize();
                camera.mSun = Shaders::sunSource(
                    -travelling, lit ? osg::Vec3f(irradiance, irradiance, irradiance) : osg::Vec3f());

                // Even air with a colour of its own, so the frame is never empty and the two sunless
                // cases have something to agree about.
                camera.mFogColour = osg::Vec3f(0.02f, 0.02f, 0.02f);
                camera.mFogExtinction = 2.0e-4f;
                camera.mFogUniform = 1.0f;

                const Frame frame = shoot(scene, {}, camera, size);
                return int{ frame.byte(centre) };
            };

            const int open = look(false, true);
            const int shaded = look(true, true);
            const int sunless = look(true, false);

            EXPECT_EQ(shaded, sunless) << "the lid takes all of the sun, not most of it";
            EXPECT_GT(open, shaded + 20) << "and there was a sun to take";
        }

        /// A point under a bank's top is lit as the clear air its slant to the sun crosses (`slantCoverage`).
        /// The field (`VulkanRenderer::setFogField`) is full in the lower half of the tile's height and empty in
        /// the upper at every level, each scale's offset putting the change at z = 600 (the turns and warp are
        /// horizontal). The eye stands at z = 250 inside the bank looking level at a black wall, in air that
        /// scatters nothing of its own, under a sun at a cosine of 0.8.
        ///
        /// With a lift of 0.2 the layer's height is 520, the beam's slant 520 / 0.8 = 650 and its middle at
        /// z = 250 + 520 = 770, over the top: the slant's coverage is nought and the sun reaches the eye's air
        /// whole. Against a field full everywhere, whose slant is charged `1 / FOG_COVERAGE` = 2.8066, the
        /// centre pixel is brighter by
        ///
        ///   exp(8.9e-4 * exp(-250 / 520) * 2.8066 * 650) = exp(1.00383) = 2.7287
        ///
        /// (measured 2.7322), the air along the eye's ray being the same in both.
        TEST_F(RtxVisibilityTest, aPointUnderABanksTopIsLitThroughTheClearAirItsSlantCrosses)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float top = 600.0f;
            constexpr float eye = 250.0f;
            constexpr float lift = 0.2f;
            constexpr float extinction = 8.9e-4f;
            constexpr float irradiance = 20.0f;

            // The field the shader reads at each level, `sFogNoise`'s layout: two channels a texel,
            // slice by slice. Banked, the lower half of the tile's height is full and the upper half
            // empty; whole, every texel is full.
            const auto fieldOf = [](bool banked) {
                std::vector<std::uint8_t> noise(sFogFieldBytes);
                for (std::uint32_t level = 0; level < Shaders::FOG_FIELD_LEVELS; ++level)
                {
                    const std::size_t side = fogFieldSideAt(level);
                    const std::size_t slice = side * side * 2;
                    for (std::size_t z = 0; z < side; ++z)
                        std::fill_n(noise.begin() + static_cast<std::ptrdiff_t>(fogFieldOffsetAt(level) + z * slice),
                            slice, !banked || 2 * z < side ? std::uint8_t{ 255 } : std::uint8_t{ 0 });
                }
                return noise;
            };

            struct Restored
            {
                RendererProbe& mProbe;
                ~Restored() { mProbe.setFogField(sFogNoise); }
            } restored{ mProbe };

            const osg::Vec3f towards(0.6f, 0.0f, 0.8f);
            const auto look = [&](bool banked) {
                const std::vector<std::uint8_t> field = fieldOf(banked);
                mProbe.setFogField(FogField(field.data(), sFogFieldBytes));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, 0.0f, eye), osg::Vec3f(0.0f, 1000.0f, eye), 10.0f, size, size, 100000.0f);
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun = Shaders::sunSource(towards, osg::Vec3f(irradiance, irradiance, irradiance));
                camera.mFogColour = osg::Vec3f();
                camera.mFogExtinction = extinction;
                camera.mFogUniform = 0.0f;
                camera.mFogLift = lift;
                float tile = Shaders::FOG_TILE;
                for (osg::Vec3f& offset : camera.mFogOffsets)
                {
                    const float read = 0.5f - top / tile;
                    offset = osg::Vec3f(0.0f, 0.0f, read - std::floor(read));
                    tile /= Shaders::FOG_LACUNARITY;
                }

                SceneDesc scene;
                addQuad(scene, uprightQuadAt(2000.0f, 1000.0f, osg::Vec2f(0.0f, eye)),
                    scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f() }));
                const Frame frame = shoot(scene, {}, camera, size, { .mFrames = 64, .mAverage = false });
                return frame.at(centre);
            };

            const float banked = look(true);
            const float whole = look(false);
            ASSERT_GT(whole, 0.005f) << "the sun lights the air, or this proves nothing";

            const float height = Shaders::FOG_HEIGHT * lift;
            const float slant = height / towards.z();
            const float depth = extinction * std::exp(-eye / height) / Shaders::FOG_COVERAGE * slant;
            EXPECT_NEAR(banked / whole, std::exp(depth), std::exp(depth) * 0.01f)
                << "the slant was charged with coverage it does not cross";
        }

        /// A sprite fades through the layer's own integral, not through one sample of it. A descending ray
        /// tells the two apart: along a level ray one sample is the whole answer, but a ray dropping three scale
        /// heights crosses a layer that thickens under it, and the mean of an exponential over that stretch is
        /// not the exponential at its middle, so a puff of smoke and the wall behind it faded at two rates.
        ///
        /// An adding sprite, since what it puts into the pixel is the transmittance times a constant
        /// (`spritesAlong` writes `mAdded` as `(1 - addedThrough) * SUNLIT_WHITE`, for one sprite its own
        /// `glow` times `reaching`); a covering sprite is lit by the froxel it stands in, which moves with the
        /// fog. The camera stands three scale heights up looking down at 45 degrees to a sprite on the base,
        /// so the ray runs `7800 * sqrt(2)` = 11030.87 units. The layer's mean falloff over it is
        ///
        ///   (exp(-3) - exp(0)) / (0 - 3) = 0.3167376
        ///
        /// so the optical depth is `3e-4 * 11030.87 * 0.3167376` = 1.048177 and the sprite keeps
        /// `exp(-1.048177)` = 0.35058. One sample at the middle reads `exp(-1.5)` = 0.2231302, a depth of
        /// 0.738438 and a transmittance of 0.47784: a third more than the air leaves.
        TEST_F(RtxVisibilityTest, aSpriteFadesThroughTheWholeLayerAndNotOneSampleOfIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            const float height = 3.0f * Shaders::FOG_HEIGHT;

            constexpr std::array<std::uint8_t, 4> half{ 255, 255, 255, 128 };
            const std::array<TextureData, 1> flame{ describeTexel(half) };

            const auto glow = [&](float extinction) {
                SceneDesc scene;
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));

                // Wide enough to fill the middle of the frame from three scale heights away, which
                // is what the descent costs in distance.
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    .mRadius = 2000.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, BlendKind::Add);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -height, height), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // Nothing but the sprite in the pixel: black air adds no colour of its own, and an
                // unlit sky leaves the ray past the sprite carrying nothing.
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mFogColour = osg::Vec3f();
                camera.mFogExtinction = extinction;

                // Even air, which is the case the closed form is exact for: with no coverage band
                // along the ray, the height falloff is the whole of what varies.
                camera.mFogUniform = 1.0f;

                const Frame frame = shoot(scene, flame, camera, size);

                // The radiance and not the byte: what is asserted is a ratio, and eight bits of a
                // display curve is a coarse place to take one.
                return frame.at(centre);
            };

            const float clear = glow(0.0f);
            ASSERT_GT(clear, 0.02f) << "the sprite has to be in the frame before its fading means anything";

            const float hazed = glow(3.0e-4f);

            EXPECT_NEAR(hazed / clear, 0.35058f, 0.02f) << "the layer's integral over the whole descent";
        }

        /// The air behind a pane is the air that is there. A column of the volume ends where the eye's own ray
        /// ends, and the eye sees through glass: a column stopped at the first surface would leave every slice
        /// past a pane as it stood (a wall 4000 units off behind a pane at 1000 would keep 0.66 of its light
        /// where 0.25 is what the air leaves). A ratio against the same scene in clear air, so the pane's half
        /// and the wall's radiance divide out; the wall glows, for the reason the test below gives.
        TEST_F(RtxVisibilityTest, theAirBehindAPaneIsTheAirThatIsThere)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float wallAway = 4000.0f;
            constexpr float extinction = 3.5e-4f;

            const auto look = [&](bool paned, float thickness) {
                SceneDesc scene;

                addQuad(scene, wallAt(0.0f),
                    scene.addMaterial(Material{
                        .mEmissiveColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    }));

                // A quarter of the way along the path, and wide enough to fill the middle of the
                // frame from there.
                if (paned)
                    addPane(scene, uprightQuadAt(2400.0f, 1000.0f - wallAway), osg::Vec4f(0.0f, 0.0f, 0.0f, 0.5f));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -wallAway, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness);

                // Nothing in the pixel but the wall's own glow through the air: a black pane
                // reflects nothing, and air with no colour of its own scatters none in.
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mFogColour = osg::Vec3f();

                const Frame frame = shoot(scene, {}, camera, size);

                return frame.at(centre);
            };

            // The volume's own quadrature stands this a little over the closed form, which is what
            // the tolerance is: `exp(-3.5e-4 * 4000)` is 0.2466 and it reads 0.268.
            const float open = look(false, extinction) / look(false, 0.0f);
            EXPECT_NEAR(open, std::exp(-extinction * wallAway), 0.03f) << "the wall through four thousand units";

            // **The same air, and the pane changed none of it.** Exactly, because the two frames
            // differ in what stands at a thousand units and in nothing about the air.
            EXPECT_NEAR(look(true, extinction) / look(true, 0.0f), open, 1.0e-3f) << "and the same behind a pane";
        }

        /// A pane is hazed over its own distance and not over the path behind it. A composite run before the
        /// water and the air rather than after multiplies the pane by the transmittance measured to the surface
        /// behind it, so a lit window a few units out arrives as dim as the wall four thousand units further on.
        ///
        /// A surface that glows on its own, so the radiance under the asserted transmittance is a figure no
        /// shadow, ambient term or bounce can move: the glow joins the light and not the albedo, so the wall
        /// behind carries an albedo of nought, and the pane's own figure divides out of the ratio against clear
        /// air. The eye stands 4000 units from the wall and the pane is held at 1000 and at 3000, so over air of
        /// extinction 3.5e-4 the answers are `exp(-0.35)` = 0.70469 and `exp(-1.05)` = 0.34994, where
        /// compositing from the wall's own column would read one number for both.
        ///
        /// The same two figures with a second pane in front, the same fault a layer deeper: a stack charged the
        /// medium in front of its nearest layer reads a glowing pane behind a plain one at five hundred units as
        /// if it stood there. The plain pane halves both frames and divides out.
        ///
        /// The tolerances are the volume's own quadrature: its slices are quadratic in depth, so a longer
        /// stretch is read across coarser ones (0.003 over the closed form near, 0.014 far), each tolerance the
        /// next round figure above.
        TEST_F(RtxVisibilityTest, aPaneIsHazedOverItsOwnDistanceAndNotOverThePathBehindIt)
        {
            constexpr std::uint32_t size = framedInColumns(4);
            constexpr std::size_t centre = centreValueOf(size);
            constexpr float wallAway = 4000.0f;
            constexpr float extinction = 3.5e-4f;

            const std::array<osg::Vec3f, 4> wall = wallAt(0.0f);

            const auto glow = [&](float paneAway, float thickness, bool behindAnother = false) {
                SceneDesc scene;

                addQuad(scene, wall,
                    scene.addMaterial(Material{
                        .mDiffuseColour = osg::Vec3f(0.0f, 0.0f, 0.0f),
                    }));

                // Wide enough to fill the middle of the frame from the far end of the path, where
                // a sixty-degree frame covers 1732 units either side of the axis.
                const std::array<osg::Vec3f, 4> pane = uprightQuadAt(2400.0f, paneAway - wallAway);
                addQuad(scene, pane,
                    scene.addMaterial(Material{
                        .mEmissiveColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mOpacity = 0.5f,
                        .mAlphaMode = AlphaMode::Blend,
                    }));

                // Half way to the glowing one at its nearest, and black, so what it puts into the
                // pixel is nothing and what it does to the pane behind it is a half.
                if (behindAnother)
                    addPane(scene, uprightQuadAt(2400.0f, 500.0f - wallAway), osg::Vec4f(0.0f, 0.0f, 0.0f, 0.5f));

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -wallAway, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness);

                // Nothing in the pixel but the glass: black air scatters no colour of its own, and
                // an unlit sky leaves both surfaces with nothing to reflect.
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mAmbient = osg::Vec3f();
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mFogColour = osg::Vec3f();

                const Frame frame = shoot(scene, {}, camera, size);

                return frame.at(centre);
            };

            const float nearClear = glow(1000.0f, 0.0f);
            ASSERT_GT(nearClear, 0.02f) << "the pane has to be in the frame before its fading means anything";
            EXPECT_NEAR(glow(1000.0f, extinction) / nearClear, 0.70469f, 0.005f) << "a thousand units of air";

            const float farClear = glow(3000.0f, 0.0f);
            ASSERT_NEAR(farClear, nearClear, 1.0e-4f) << "the same glass, and only the air in front of it moved";
            EXPECT_NEAR(glow(3000.0f, extinction) / farClear, 0.34994f, 0.02f) << "three thousand units of it";

            // The same two, each behind a pane of its own at five hundred units.
            EXPECT_NEAR(glow(1000.0f, extinction, true) / glow(1000.0f, 0.0f, true), 0.70469f, 0.005f)
                << "a thousand units of air, behind a second pane";
            EXPECT_NEAR(glow(3000.0f, extinction, true) / glow(3000.0f, 0.0f, true), 0.34994f, 0.02f)
                << "three thousand units of it, behind a second pane";
        }

        /// A moon's light in the air is one level across its disc, with no spot at its centre (`fogMoonTerm`):
        /// the phase's diffraction peak is for a source half a degree across, and on a disc of twelve it drew a
        /// bright spot on a crescent's dark side. The eye looks along the moon's line and half a radius off it,
        /// both inside the disc and reading the phase at the limb within the 2% that 32 frames of the fog's
        /// draws leave, where the peak put them 13% apart; two radii off stands outside and reads less. The moon
        /// turns about the vertical, so its slant through the air is one at all three.
        TEST_F(RtxVisibilityTest, aMoonsLightInTheAirIsOneLevelAcrossItsDisc)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            const float rise = 0.5f;
            const auto lit = [&](float off) {
                const osg::Vec3f look(0.0f, std::cos(rise), std::sin(rise));
                Shaders::VisibilityConstants camera
                    = Testing::makeCamera(osg::Vec3f(0.0f, 0.0f, 0.0f), look * 1000.0f, 60.0f, size, size, 100000.0f);
                camera.mFogColour = osg::Vec3f(0.4f, 0.4f, 0.001f);
                camera.mFogExtinction = 3.0e-4f;
                camera.mFogUniform = 1.0f;

                // `cos off = sin^2 rise + cos^2 rise cos turn`, the angle between two lines of one rise.
                const float across
                    = (std::cos(off) - std::sin(rise) * std::sin(rise)) / (std::cos(rise) * std::cos(rise));
                const float turn = std::acos(across);
                const osg::Vec3f towards(
                    std::sin(turn) * std::cos(rise), std::cos(turn) * std::cos(rise), std::sin(rise));

                Shaders::MoonDisc moon{};
                moon.mSource = Shaders::skySource(towards, osg::Vec3f(0.0f, 0.0f, 1.0f), moonAngularRadius(94.0f));
                moon.mAlpha = 0.0f;
                moon.mFace = Shaders::NO_TEXTURE;
                camera.mMoons[0] = moon;

                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, -200000.0f));
                return shoot(scene, {}, camera, size, { .mFrames = 32 }).mRadiance[centre + 2];
            };

            const float radius = moonAngularRadius(94.0f);
            const float atCentre = lit(0.0f);
            EXPECT_NEAR(lit(0.5f * radius) / atCentre, 1.0f, 0.02f) << radius;
            EXPECT_LT(lit(2.0f * radius), atCentre) << "and the glow falls away outside the disc";
        }

        /// A moon too faint for a shadow ray still lights the air. `FOG_SHAFT_FLOOR` is a threshold on the ray
        /// and not on the light: it asks whether a shaft cut out of what the pair delivers would be visible, and
        /// ninety degrees off a moon it is not. The scatter pass read the same flag to decide whether the moons
        /// lit the air, so both left the frame together, and one column at a time (`fogVolumeAlong` takes the
        /// phase from the column's own direction), so the tent the integrate pass reads averaged a lit column
        /// with a black one: a seam across the air.
        ///
        /// Measured in a channel the air's own colour barely holds, and in radiance: the threshold is
        /// `FOG_SHAFT_FLOOR` of the brightest channel of the fog colour, so an air bright in red and green and
        /// near black in blue puts the crossing far above what the air puts into blue. The faintest leg adds
        /// eight parts in a hundred thousand there, a quarter of the display curve's smallest step, so through
        /// the curve the figure would fall on whichever side of a byte the air sits.
        ///
        /// Asserted is the light per unit of irradiance, bounded and not fixed: a leg under the threshold casts
        /// no ray and arrives unshadowed, delivering more per unit than one that pays for its shadow. Nothing
        /// may deliver less than the shadowed leg, by what the volume can hold and no more (the integrated air
        /// keeps the air's colour and the moons' light in one half float a channel, `FOG_VOLUME_FORMAT`, so a
        /// reading is off by up to half a step and a difference by a whole one; the faintest leg puts two
        /// hundred steps in the channel, good to half a per cent), nor more than twice it. The step this is
        /// about reads nought.
        TEST_F(RtxVisibilityTest, aMoonTooFaintForItsOwnShadowStillLightsTheAir)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            // Bright in red and green so the threshold is high, and near black in blue so the moon
            // has a channel of its own to be read in.
            const osg::Vec3f haze(0.4f, 0.4f, 0.001f);

            // A decade apart, which is what puts the threshold inside the sweep: measured here, the
            // brightest two legs are worth their ray and the faintest is not.
            constexpr std::array<float, 4> irradiances{ 0.001f, 0.01f, 0.1f, 1.0f };

            const auto lit = [&](float irradiance) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1000.0f, 0.0f), 60.0f, size, size, 100000.0f);

                camera.mFogColour = haze;
                camera.mFogExtinction = 3.0e-4f;
                camera.mFogUniform = 1.0f;

                // No disc, because what is measured is the air and not the moon: a disc where the ray runs would
                // put the moon's own radiance in the pixel, and the irradiance alone carries `HAS_MOONS`, so the
                // basis, colour and face a disc is cut from stay nothing. `mLimb` is how wide the shadow cone is.
                Shaders::MoonDisc moon{};
                osg::Vec3f towards(0.0f, 1.0f, 0.5f);
                towards.normalize();
                moon.mSource
                    = Shaders::skySource(towards, osg::Vec3f(0.0f, 0.0f, irradiance), moonAngularRadius(94.0f));
                moon.mAlpha = 0.0f;
                moon.mFace = Shaders::NO_TEXTURE;
                camera.mMoons[0] = moon;

                // The sheet is past `mFar` in every direction a ray here travels, for the reason
                // `theFogScattersTheSunForwardFarHarderThanBack` gives: a wall in the path of a
                // shadow ray would shadow what this measures.
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, -200000.0f));

                std::vector<float> radiance;
                radiance = shoot(scene, {}, camera, size).mRadiance;

                // Blue is the third of the pixel's four values, and the channel the moon has to
                // itself.
                return radiance[centre + 2];
            };

            const float dark = lit(0.0f);

            // What a figure taken as the difference of two readings and divided by an irradiance can be wrong by.
            // The composite reads the froxel's half through the ray's weights, which sum to a little under one,
            // so the froxel's value is the reading's or a little over, and where the two straddle a power of two
            // its half step is the reading's whole one: a whole step at each reading covers both.
            const auto rounding = [](float reading, float baseline, float irradiance) {
                return (halfStepAt(reading) + halfStepAt(baseline)) / irradiance;
            };

            // The brightest leg is far above the threshold and pays for its own shadow, so it is the
            // floor every fainter leg is measured against.
            const float bright = lit(irradiances.back());
            const float shadowed = (bright - dark) / irradiances.back();
            const float shadowedRounding = rounding(bright, dark, irradiances.back());
            EXPECT_GT(shadowed, shadowedRounding) << "a moon well above the threshold lit the air";

            for (const float irradiance : irradiances)
            {
                const float reading = lit(irradiance);
                const float delivered = (reading - dark) / irradiance;
                const float tolerance = rounding(reading, dark, irradiance) + shadowedRounding;

                EXPECT_GE(delivered, shadowed - tolerance)
                    << "the air went dark at an irradiance of " << irradiance << ": " << delivered << " a unit against "
                    << shadowed << ", with " << tolerance << " of rounding";
                EXPECT_LE(delivered, 2.0f * shadowed)
                    << "more than the moon's own shadow was worth, at an irradiance of " << irradiance;
            }
        }
    }
}
