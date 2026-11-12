#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/statistics.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtx/support/wavemoments.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/environment/wavecascade.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/brdf.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtxvulkan/shaders/shared/ripple.h>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// How far a field varies, as a fraction of its own mean: the measure every water pattern here is judged
        /// by, because what a caustic or a shaft is asked to have is structure and not brightness, which a
        /// ratio against a flat sea has already divided out.
        float contrastOf(const std::vector<float>& field)
        {
            const float mean = meanOf(field);

            float spread = 0.0f;
            for (const float value : field)
                spread += (value - mean) * (value - mean);

            return std::sqrt(spread / static_cast<float>(field.size())) / mean;
        }

        /// The lobe the shader arrives at for a sea read through a cone this wide: what the chain lost at that
        /// footprint and not the whole spectrum. The shader reads `mLostSlope` off the level `waveLevel` picks,
        /// so it carries the slope a mip of that width averaged away, never all of it unless the cone reaches
        /// the coarsest level; this is the shader's own question and not the size of the answer. The lobe is the
        /// cone the roughness those slopes stand for fills, `ggxConeWidth`, as a solid's lobe is.
        float lobeOf(const SeaState& sea, float footprint)
        {
            float unresolved = 0.0f;
            for (const WaveCascade& cascade : makeWaveCascades(sea))
                unresolved += Testing::lostSlopeOf(cascade, footprint);

            const float roughness = Shaders::slopeRoughness(unresolved);
            return Shaders::ggxConeWidth(roughness * roughness, Shaders::BOUNCE_SPREAD);
        }

        /// A shaft is the surface's own lens carried along the ray, and nothing else. `ColumnRun` integrates
        /// the sun's beam exactly and an exact integral of a smooth thing is smooth: the water brightens toward
        /// the sun and goes dark away from it with no structure between. A beam of sunlight in water has
        /// structure because the surface over it is a lens, and carrying that down the ray is what this measures.
        ///
        /// A black sky and no ambient, so the beam is the whole pixel: looking up from deep water the surface
        /// reflects an unlit bed and refracts a black sky, sending down nothing, and the column's own sky term
        /// meets an ambient of nothing, leaving the sun scattered toward the eye over fifteen hundred units.
        ///
        /// Two seas, and the mean says it is light and not decoration: a flat sea is a lens of no power and its
        /// caustic is exactly one everywhere, so it renders the closed form, while a real one redistributes
        /// that same light. The pattern is the difference between them and the total is not.
        TEST_F(RtxVisibilityTest, theSunsBeamUnderWaterCarriesTheSurfacesPattern)
        {
            constexpr std::uint32_t size = 48;
            constexpr std::size_t count = std::size_t{ size } * size;

            const SceneDesc scene = makeFlooded(4000.0f, 2000.0f);

            // Seven metres under a sun all but overhead, looking all but straight up at it — well
            // inside the share `WATER_SHAFT_FLOOR` asks for, and at a depth a shaft happens at. Twenty
            // metres down there is little pattern left in the water to carry.
            const auto lookUp = [&](const SeaState& sea) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -0.05f, -500.0f), osg::Vec3f(0.0f, 0.0f, -490.0f), 60.0f, size, size, 10000.0f);
                litThroughWater(camera);

                const Frame frame = shoot(scene, {}, camera, size, { .mSea = sea });

                std::vector<float> field;
                field.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    field.push_back(frame.at(i * 4 + 1));

                return field;
            };

            const std::vector<float> flat = lookUp(SeaState{ .mSignificantHeight = 0.0f });
            const std::vector<float> running = lookUp(SeaState{});

            std::vector<float> ratio;
            ratio.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                ratio.push_back(running[i] / flat[i]);

            // The ratio, because the phase function is eighty times brighter down the sun's line than at the
            // edge of a sixty-degree frame; dividing the two frames cancels that along with the extinction and
            // geometry and leaves the lens alone. A flat sea's own caustic is exactly one everywhere, so the
            // ratio would be a field of ones.
            EXPECT_GT(contrastOf(ratio), 0.03f) << "measured 0.057, where a flat sea gives nought";
            EXPECT_NEAR(meanOf(ratio), 1.0f, 0.06f) << "measured 0.963: the shaft moves light and makes none";
        }

        /// A shaft is blocked by what stands over the water, and the gap is where the sun enters. A submerged
        /// surface is shadowed because `GatherRun` traces its own ray, and water carries a mask bit that keeps
        /// it out of occlusion, so a rock over the sea would darken the bed under it and leave the water in
        /// front of the bed as bright as ever; the march is where the volume gets the same question.
        ///
        /// Three lids, the last saying where the shadow belongs. A lid over everything only proves something is
        /// being asked, and a strip whose shadow falls where the light entered only proves the answer can be
        /// partial. The last stands over that same water and casts its shadow elsewhere, so a shader that
        /// looked straight up would find it squarely in the way and darken a shaft nothing is shading. The sun
        /// is 45 degrees off the vertical, which Snell bends to 32 under the water, so from 500 units down the
        /// light met the surface 312 units up-sun of the eye, and a lid 500 units over the sea shadows the water
        /// 500 units down-sun of itself; the second lid is placed against those two displacements. Nothing
        /// else is in the frame (no bed, a black sky, no ambient, no disc), so the shaft is alone.
        TEST_F(RtxVisibilityTest, aShaftIsBlockedByWhatStandsOverTheWaterWhereTheSunEnters)
        {
            constexpr std::uint32_t size = 48;
            constexpr std::size_t count = std::size_t{ size } * size;
            constexpr float eye = 500.0f;

            // Straight up from under the surface, which is where the phase function puts the beam.
            // A hair off the vertical, which `makeCamera` insists on.
            const auto shaft = [&](const std::optional<std::array<osg::Vec3f, 4>>& lid) {
                SceneDesc scene = makeOpenWater(4000.0f);
                if (lid.has_value())
                    addQuad(scene, *lid);

                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -0.05f, -eye), osg::Vec3f(0.0f, 0.0f, -eye + 10.0f), 60.0f, size, size, 10000.0f);
                litThroughWater(camera, osg::DegreesToRadians(45.0f));

                const Frame frame = shoot(scene, {}, camera, size);

                float total = 0.0f;
                for (std::size_t i = 0; i < count; ++i)
                    total += frame.at(i * 4 + 1);

                return total / static_cast<float>(count);
            };

            // Well clear of the water, and wider than anything the frame reaches.
            constexpr float over = 500.0f;
            const std::array<osg::Vec3f, 4> everything = sheetAt(4000.0f, over);

            // A ray leaving the water toward a sun 45 degrees over passes this height 500 units
            // up-sun of where it started, so a lid here shadows the water 500 units down-sun of
            // itself. Up-sun is toward -y: that is where the sun stands, and its light travels +y.
            const auto strip = [](float from, float to) {
                return std::array<osg::Vec3f, 4>{
                    osg::Vec3f(-4000.0f, from, over),
                    osg::Vec3f(4000.0f, from, over),
                    osg::Vec3f(4000.0f, to, over),
                    osg::Vec3f(-4000.0f, to, over),
                };
            };

            // Its shadow lands on the water between 320 and 150 units up-sun of the eye's column,
            // which is where the light entered for the deeper half of the march.
            const std::array<osg::Vec3f, 4> shadowing = strip(-820.0f, -650.0f);

            // The same strip of water, with the lid over it rather than over its light. Nothing here
            // shadows anything the frame can see, and a shader that looked straight up would find
            // this in the way.
            const std::array<osg::Vec3f, 4> overhead = strip(-320.0f, -150.0f);

            const float open = shaft(std::nullopt);
            const float covered = shaft(everything);
            const float shaded = shaft(shadowing);
            const float beside = shaft(overhead);

            // Measured 0.0182 open and 2.7e-7 under the lid. The beam is the whole of the frame, so
            // taking the sun off the water takes the frame with it.
            EXPECT_GT(open, 0.0f) << "there is a shaft to block";
            EXPECT_LT(covered, 1.0e-4f * open) << "and a lid over all of the water puts it out";

            // A strip takes nearly all of it and not half, because the beam is a narrow lobe: Henyey-Greenstein
            // at 0.92 puts the shaft in the pixels looking up-sun, and a ray that climbs toward the sun gains y
            // as fast as its entry point does, so its whole march enters the water through a stretch a couple of
            // tens of units long and one strip covers it. The pixels the strip misses were dark anyway.
            //
            // Sixteen per cent and not five, because the shadow has the edge every shadow has: the entry's ray
            // is drawn across the sun's own penumbra (`SUN_SHADOW_RADIUS`, three degrees, twenty-six units either
            // side of a lid's edge at five hundred up), so entries under the strip's rim take part of the sun.
            // Measured at 0.160 with the four steps the march takes.
            EXPECT_NEAR(shaded / open, 0.160f, 0.02f) << "a lid whose shadow falls on the entries";

            // **The one that says where the gap belongs**, and it is 0.9991 of the open shaft.
            // Standing over the same water and casting its shadow elsewhere, it changes nothing.
            EXPECT_NEAR(beside / open, 1.0f, 0.01f) << "and one that merely stands over them";
        }

        /// The waves gather the sun into moving lines on the bed, and move light rather than make it. Caustics
        /// are ray density, the determinant of the Jacobian of the map from where light met the surface to
        /// where it landed, and what isolates that term from everything else in the frame is a ratio: the same
        /// bed, camera and sun rendered with a sea state and without, which cancels the albedo, absorption,
        /// cosine and geometry. Looked at from underneath, so no wavy surface stands between the eye and the
        /// bed: from above, what the waves do to the view would be mixed into what they do to the light.
        TEST_F(RtxVisibilityTest, theWavesGatherSunlightOntoTheBedWithoutMakingAnyOfIt)
        {
            constexpr std::uint32_t size = 256;
            constexpr std::size_t count = std::size_t{ size } * size;
            constexpr float span = 1080.0f;

            // An orthographic look straight down from just under the surface, so every depth sees the same
            // patch of bed through pixels of one width and the caustics compare pixel for pixel. Wide enough to
            // be an ensemble: 1080 units of bed are thirty correlation lengths across of the waves that carry
            // the curvature, and a thousand patches in the frame, where a quarter of the width saw sixty and
            // every mean carried about ten per cent of sampling error, five times what the tolerances below
            // allow. The span and pixel count move together so the footprint stays 4.2 units and the level the
            // caustic reads does not move.
            const auto render = [&](float depth, const SeaState& sea, float seconds) {
                const SceneDesc scene = makeFlooded(4000.0f, depth);

                const osg::Matrixf view = osg::Matrixf::lookAt(
                    osg::Vec3f(0.0f, 0.0f, -1.0f), osg::Vec3f(0.0f, 0.0f, -depth), osg::Vec3f(0.0f, 1.0f, 0.0f));
                Shaders::VisibilityConstants camera = constantsFor(
                    makeOrthographicCameraFromView(view, span, span, size, size, 0.5f, 100000.0f).value());
                camera.mSkyRamp = Testing::sHemisphereRamp;
                litThroughWater(camera);
                camera.mWaterTime = splitSeconds(seconds);

                const Frame image = shoot(scene, {}, camera, size, { .mSea = sea });

                // **The radiance and not the byte, which is what a ratio of two dark pixels needs.**
                // Twenty metres of water leaves green at a fiftieth of the scale, where one step of
                // the display curve is a per cent of the ratio being measured — and every figure
                // here is a mean over that ratio.
                std::vector<float> linear;
                linear.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    linear.push_back(image.at(i * 4 + 1));

                return linear;
            };

            // Green, which at these depths has the most left to vary over: blue outlasts it but the
            // still sea is already bright in blue, and red is gone.
            const auto causticField = [&](float depth, float seconds = 0.0f) {
                // The still sea does not move, so one baseline serves whatever the clock says.
                const std::vector<float> still = render(depth, SeaState{ .mSignificantHeight = 0.0f }, 0.0f);
                const std::vector<float> running = render(depth, SeaState{}, seconds);

                std::vector<float> field;
                field.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    field.push_back(running[i] / still[i]);

                return field;
            };

            // Two metres down, where a caustic is at its sharpest.
            const std::vector<float> shallow = causticField(140.0f);
            const auto [dimmest, brightest] = std::minmax_element(shallow.begin(), shallow.end());
            const float mean = meanOf(shallow);

            // Measured over this patch: the brightest place on the bed is gathered to 1.32 of what
            // a flat sea would put there and the dimmest thinned to 0.72, so the pattern is bold
            // rather than a wobble.
            EXPECT_GT(*brightest, 1.2f) << "measured 1.32, gathered into lines";
            EXPECT_LT(*dimmest, 0.8f) << "measured 0.72, and thinned between them";

            // And the mean is one, the claim that makes it light and not decoration. A reciprocal of something
            // that fluctuates is worth more than the reciprocal of its mean, and `causticGain` is that excess
            // divided back out (1.001 here). The estimator it corrects reads 0.95 at two metres and 1.26 at
            // twenty before the fade blends either toward one, a quarter of the light where the slack allowed
            // below is a fiftieth of it.
            EXPECT_NEAR(mean, 1.0f, 0.02f) << "the waves redistribute the sun, they do not make any";

            // The pattern peaks in shallow water and fades as the inverse square root of the depth past it,
            // which is Snyder and Dera's 1970 measurement of the sea. Measured here 0.216, 0.065 and 0.014 at
            // two, six and twenty metres. It falls faster than the law by the fade's choice:
            // `WATER_CAUSTIC_FADE` is one, twice the law's half, for the look (`look.h`). From two metres to
            // six (140 units to 400) the fade alone keeps (400 / 140)^-1 = 0.35 of the contrast where the law
            // keeps 0.58, and the blur and grain growing with depth take it to the 0.30 measured; from six to
            // twenty (400 to 1400) the fade keeps 0.286 and the whole 0.215. Within 0.02, the contrast's tolerance.
            const std::vector<float> deeper = causticField(400.0f);
            const std::vector<float> deepest = causticField(1400.0f);

            EXPECT_NEAR(contrastOf(deeper) / contrastOf(shallow), 0.30f, 0.02f) << "six metres down, against two";
            EXPECT_NEAR(contrastOf(deepest) / contrastOf(deeper), 0.215f, 0.02f) << "and twenty, against six";

            // And the mean holds at every one of them, which a single coefficient could never do: the fold
            // saturates at a metre of depth and the read level keeps coarsening past it, so the estimator's
            // excess runs from 13 per cent at one metre to 32 at twenty, and one curve in the resolved fold
            // follows all of it. The fade moves nothing either way, since it blends toward one.
            EXPECT_NEAR(meanOf(deeper), 1.0f, 0.02f) << "measured 1.002, six metres down";
            EXPECT_NEAR(meanOf(deepest), 1.0f, 0.02f) << "and 0.999 at twenty";

            // How bold the pattern is and how fast it moves are the two halves of one choice. The spectrum's
            // short cutoff is a limit in time and not in space: the waves that focus hardest are the shortest,
            // and a wave's period falls with its length, so the waves that make the boldest caustics are the
            // ones that make them tear.
            // A fifth less bold than a table of sixty-four sinusoids draws, bought on purpose: curvature weights
            // a component by `A k²`, so such a table has the shortest few owning the Hessian outright, a handful
            // of plane waves crossing into hard repeating lines that read as a lattice. Tens of thousands of
            // components at the same wavelengths interfere into a mottle instead, the same energy spread over
            // every direction and no line drawn twice. It measures 0.216 against such a table's 0.277.
            EXPECT_NEAR(contrastOf(shallow), 0.213f, 0.02f) << "the pattern's contrast, as a fraction of its own mean";

            // A twelfth of a second, which is how long a frame is worth caring about. For two samples of one
            // field `E[(b - a)^2] = 2 sigma^2 (1 - rho)`, so half the ratio of the two sums is the share of the
            // pattern that is new.
            //
            // Taken against the best shift, because a pattern that travels is not one that tears: the sea
            // carries its caustics shoreward at the waves' own phase speed, and a field translated by a feature
            // width scores near one on the unshifted sum while looking coherent, so the raw figure counts the
            // sea's own motion as decay. The least over a small sweep of offsets separates them: the pattern
            // travels a pixel here, worth seventeen points of the 66 per cent the raw sum reports.
            //
            // Over a table of sixty-four sinusoids the same sweep reads otherwise: 18 units of wavelength
            // reshuffle 73% and read as stripes running across the bottom, 32 come out at 51%, 50 at a dull 33%,
            // the shortest few owning the Hessian in four directions. Tens of thousands of wavevectors
            // interfere into a mottle, and two shots a twelfth of a second apart show a net that slides rather
            // than one that boils.
            const std::vector<float> later = causticField(140.0f, 1.0f / 12.0f);

            float spread = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
                spread += (shallow[i] - mean) * (shallow[i] - mean);

            // Three pixels either way, which is three times what the sea carries the pattern in a
            // twelfth of a second. The margin also keeps every shifted read inside the frame rather
            // than wrapping, since the two fields are the same patch of bed.
            constexpr int sweep = 3;
            constexpr int inside = int(size) - 2 * sweep;
            const auto newAfter = [&](int across, int down) {
                float apart = 0.0f;
                for (int y = sweep; y < int(size) - sweep; ++y)
                    for (int x = sweep; x < int(size) - sweep; ++x)
                    {
                        const float gap
                            = later[std::size_t(y + down) * size + (x + across)] - shallow[std::size_t(y) * size + x];
                        apart += gap * gap;
                    }

                return 0.5f * apart / spread * float(count) / (float(inside) * float(inside));
            };

            const float unshifted = newAfter(0, 0);
            float least = unshifted;
            for (int down = -sweep; down <= sweep; ++down)
                for (int across = -sweep; across <= sweep; ++across)
                    least = std::min(least, newAfter(across, down));

            EXPECT_NEAR(unshifted, 0.661f, 0.03f) << "how much of the pattern is new a twelfth later";
            EXPECT_NEAR(least, 0.495f, 0.03f) << "and how much of that the sea did not simply carry";
        }

        /// The sun's disc carries exactly its irradiance, however wide the pixel that finds it. The sun is
        /// drawn in the sky and not answered by a highlight on each surface that could reflect it, so the one
        /// thing that must hold is that widening the disc never changes how much light is in it: a cone twice
        /// as wide covers four times the solid angle and is four times dimmer, which is what makes a rough sea
        /// spread the sun without brightening the sea. Two fields of view over the same sun, the pixel finding
        /// it ten times wider in one. The irradiance is small because nothing here has an exposure stage and
        /// the real thing saturates on sight.
        TEST_F(RtxVisibilityTest, theSunsDiscCarriesItsIrradianceHoweverWideThePixelThatFindsIt)
        {
            constexpr std::uint32_t size = 64;
            constexpr float irradiance = 4.0e-5f;

            struct Disc
            {
                float mPeak;
                float mTotal;
                float mSpreadAngle;
            };

            // Looking straight at a sun 45 degrees up, from clear of the only thing in the scene.
            const auto lookAtTheSun = [&](float fov) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(
                    osg::Vec3f(0.0f, -500.0f, 0.0f), osg::Vec3f(0.0f, -501.0f, 1.0f), fov, size, size, 10000.0f);
                camera.mSun = Shaders::sunSource(
                    sunStandingAt(osg::DegreesToRadians(45.0f)), osg::Vec3f(irradiance, irradiance, irradiance));

                // **The disc is drawn because there is light, which is one fact and not two.** What
                // it is painted with is still its own colour, and white is the plain noon of it.
                camera.mSunDiscColour = osg::Vec3f(1.0f, 1.0f, 1.0f);

                const SceneDesc scene = makeWall();
                const Frame frame = shoot(scene, {}, camera, size);

                // A pixel's solid angle is its side squared at these angles: the frame is two
                // degrees across in one case and twenty in the other, where the cos-cubed the exact
                // form carries is a part in two thousand.
                Disc disc{ .mPeak = 0.0f, .mTotal = 0.0f, .mSpreadAngle = camera.mEyes.mWorld.mSpreadAngle };
                for (std::size_t i = 0; i < std::size_t{ size } * size; ++i)
                {
                    const float radiance = frame.at(i * 4);
                    disc.mPeak = std::max(disc.mPeak, radiance);
                    disc.mTotal += radiance * camera.mEyes.mWorld.mSpreadAngle * camera.mEyes.mWorld.mSpreadAngle;
                }
                return disc;
            };

            // The same arithmetic the shader does, from the same two constants: a cap of angular
            // radius `r` subtends `pi * (2 sin(r / 2))^2`, and the disc's radiance is the sun's
            // irradiance spread over it.
            const auto capRadiance = [&](float spreadAngle) {
                const float edge = 2.0f * std::sin(0.5f * (Shaders::SUN_ANGULAR_RADIUS + 0.5f * spreadAngle));
                return irradiance / (0.5f * Shaders::TAU * edge * edge);
            };

            // A byte is worth about 0.006 of a linear value up here, so that is the tolerance.
            const Disc fine = lookAtTheSun(2.0f);
            EXPECT_NEAR(fine.mPeak, capRadiance(fine.mSpreadAngle), 0.007f)
                << "a pixel a third of the sun's width across";

            // **And the light in it is the sun's**, integrated over the frame rather than argued
            // for. Two hundred and fifty-six pixels land inside this disc, so the sum is a real
            // quadrature: it comes out at 4.015e-5 against the 4.0e-5 that was put in.
            EXPECT_NEAR(fine.mTotal, irradiance, 0.03f * irradiance) << "the disc holds what the sun sent";

            // Ten times the field of view, so ten times the pixel and a disc widened by 1.6 — which
            // by the cap's solid angle is 2.28 times dimmer, and measures so. **This is the whole
            // mechanism**: the same flux over a larger cap.
            const Disc coarse = lookAtTheSun(20.0f);
            EXPECT_NEAR(coarse.mPeak, capRadiance(coarse.mSpreadAngle), 0.007f)
                << "a pixel wider than the sun, which averages it rather than sampling it";

            // Its total is not asserted, and the reason is the honest one: this disc covers 5.7
            // pixels and four pixel centres fall inside it, so a sum over pixels is quadrature with
            // four samples and lands 30% low. The peak above is the exact statement.
            EXPECT_LT(coarse.mPeak, fine.mPeak) << "a wider cone is a dimmer sun, never a brighter one";
        }

        /// A rough sea spreads the sun into a road across the water, and a flat one does not. This is what the
        /// lost slopes are for: water too fine for the ray cone is averaged into a flat facet, a mirror that
        /// reflects the sun as one hard dot jumping between pixels as the camera moves, the field of crawling
        /// white sparks distant water becomes. Handing the variance of what was dropped to the sun's disc turns
        /// it into a road. Far enough out that the cone cannot resolve the waves; water and sky only, so every
        /// byte is the sun off the surface, the refraction ray leaving through the bottom and coming back black.
        TEST_F(RtxVisibilityTest, aRoughSeaSpreadsTheSunIntoARoadAndAFlatOneShowsOneDot)
        {
            constexpr std::uint32_t size = 64;
            constexpr float irradiance = 1.0e-3f;

            struct Road
            {
                int mPeak;
                std::size_t mLit;
            };

            const auto road = [&](const SeaState& sea) {
                const osg::Vec3f eye(0.0f, 0.0f, 6000.0f);
                const osg::Vec3f at(0.0f, 14000.0f, 0.0f);

                Shaders::VisibilityConstants camera = Testing::makeCamera(eye, at, 20.0f, size, size, 1000000.0f);

                // The sun put exactly where the centre pixel's reflection points, which is the view
                // mirrored in the water's plane and then reversed into a direction of travel. Off
                // the top of the frame by twice the camera's own tilt, so the disc in the sky and
                // the disc in the water are never in the same picture.
                osg::Vec3f view = at - eye;
                view.normalize();
                camera.mSun = Shaders::sunSource(
                    osg::Vec3f(view.x(), view.y(), -view.z()), osg::Vec3f(irradiance, irradiance, irradiance));
                camera.mSunDiscColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
                camera.mWaterLevel = 0.0f;

                const SceneDesc scene = makeOpenWater(20000.0f);
                const Frame frame = shoot(scene, {}, camera, size, { .mSea = sea });

                Road found{ .mPeak = 0, .mLit = 0 };
                for (std::size_t i = 0; i < std::size_t{ size } * size; ++i)
                {
                    found.mPeak = std::max(found.mPeak, int{ frame.byte(i * 4) });
                    found.mLit += frame.byte(i * 4) > 0 ? 1 : 0;
                }
                return found;
            };

            // A flat sea is a mirror and shows the sun once: four pixels of 4,096, at 202 of 255.
            const Road still = road(SeaState{ .mSignificantHeight = 0.0f });
            EXPECT_LT(still.mLit, 20u) << "measured 4 pixels: a mirror shows one dot";
            EXPECT_GT(still.mPeak, 150) << "measured 202: and shows it at full strength";

            // The same sun over a sea with a state in it reaches 884 pixels — a fifth of the frame —
            // at a peak of 23. **Over two hundred times the area at a ninth the strength**, which is
            // a road rather than a spark. The road is as wide as the lobe the lost slopes stand for,
            // `ggxConeWidth` of their roughness, which is the solids' own cone.
            const Road running = road(SeaState{});
            EXPECT_GT(running.mLit, 500u) << "measured 884 pixels: the sun spread across the water";
            EXPECT_LT(running.mPeak, 60) << "measured 23: and no pixel of it near the mirror's";
        }

        /// The sea runs the way the wind blows, and turning the wind turns the whole sea with it. The tiles are
        /// spread about their own +X and the frame turns them by `mSeaHeading` where they are read, so a sea
        /// under a north wind at world `p` is the sea under an east wind at `p` turned back, `(p.y, -p.x)`.
        /// Looked at straight down through a square orthographic frame centred on the origin, right along +X
        /// and up along +Y, that is the picture turned a quarter turn: pixel `(i, j)` of the north frame is
        /// pixel `(N - 1 - j, i)` of the east one. A quarter turn maps the tiles' texels onto themselves, so the
        /// frames sample the same texels and agree to the float.
        ///
        /// Lit so a turn of the surface is a turn of the picture and nothing else: a sun at the zenith, an even
        /// sky, and open water with no bed for a turned refraction to land elsewhere on. The water's own march
        /// is jittered, so eight frames are averaged and the two compared within a few bytes.
        TEST_F(RtxVisibilityTest, theSeaRunsTheWayTheWindBlows)
        {
            constexpr std::uint32_t size = 64;
            constexpr float across = 2000.0f;

            const auto looking = [&](const osg::Vec2f& heading, std::vector<std::uint8_t>& pixels) {
                const osg::Matrixf view = osg::Matrixf::lookAt(
                    osg::Vec3f(0.0f, 0.0f, 500.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f));
                Shaders::VisibilityConstants camera = constantsFor(
                    makeOrthographicCameraFromView(view, across, across, size, size, 1.0f, 100000.0f).value());

                camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
                camera.mSunDiscColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
                camera.mSkyHorizon = osg::Vec3f(0.5f, 0.5f, 0.5f);
                camera.mSkyZenith = camera.mSkyHorizon;
                camera.mWaterLevel = 0.0f;
                camera.mWaterTime = splitSeconds(3.0);
                camera.mSeaHeading = heading;

                const SceneDesc scene = makeOpenWater(20000.0f);
                pixels = shoot(scene, {}, camera, size, { .mFrames = 8 }).bytes();
            };

            std::vector<std::uint8_t> east;
            std::vector<std::uint8_t> north;
            looking(osg::Vec2f(1.0f, 0.0f), east);
            looking(osg::Vec2f(0.0f, 1.0f), north);

            // The two are pictures of a running sea and not of a mirror, or a turn would show nothing.
            std::size_t varied = 0;
            for (std::size_t i = 4; i < east.size(); i += 4)
                varied += east[i] != east[i - 4] ? 1 : 0;
            ASSERT_GT(varied, std::size_t{ size } * size / 10) << "the sea is flat";

            std::size_t agreeing = 0;
            std::size_t turnedAway = 0;
            for (std::uint32_t j = 0; j < size; ++j)
                for (std::uint32_t i = 0; i < size; ++i)
                {
                    const std::size_t here = (std::size_t{ j } * size + i) * 4;
                    const std::size_t there = (std::size_t{ i } * size + (size - 1 - j)) * 4;
                    agreeing += std::abs(int{ north[here] } - int{ east[there] }) <= 3 ? 1 : 0;
                    turnedAway += std::abs(int{ north[here] } - int{ east[here] }) <= 3 ? 1 : 0;
                }

            EXPECT_GT(agreeing, std::size_t{ size } * size * 99 / 100) << "the north sea is the east sea turned";
            EXPECT_LT(turnedAway, std::size_t{ size } * size * 9 / 10) << "and not the east sea as it was";
        }

        /// A wake's caustic turns with the sea it rides on. The sea's curvature is read in the tiles' frame and
        /// a wake's in the world's, and the caustic adds the two: the tiles' turns into the world's first, as
        /// their slope does. A footfall at the origin spreads a round ring, which a quarter turn about the
        /// origin maps onto itself, so the bed under a north wind and the ring is the bed under an east wind
        /// and the ring turned a quarter, pixel `(i, j)` against `(N - 1 - j, i)`, as
        /// `theSeaRunsTheWayTheWindBlows` has it. Looked at from under the surface under a sun at the zenith,
        /// the bed shows the caustic alone. Measured at the worst pixel against the bed's mean on RADV: 0.003
        /// per cent apart turned, 0.002 without the ring, 35 with the tiles' curvature added unturned.
        ///
        /// The bed is 256 across, just past the 240 the eye sees, because a hit is placed from its triangle's
        /// barycentrics (`placeHit`), whose rounding grows with the triangle: on a bed 4000 across a pixel and
        /// its turned partner were placed 1e-4 apart, which tipped the second cascade's 8-bit bilinear weight
        /// on the lines where it sat at its rounding point (1.4 per cent on RADV).
        TEST_F(RtxVisibilityTest, aWakesCausticTurnsWithTheSeaItRidesOn)
        {
            constexpr std::uint32_t size = 64;
            constexpr float across = 240.0f;
            constexpr float depth = 140.0f;

            const auto looking = [&](const osg::Vec2f& heading) {
                SceneDesc scene = makeFlooded(256.0f, depth);
                const osg::Matrixf view = osg::Matrixf::lookAt(
                    osg::Vec3f(0.0f, 0.0f, -1.0f), osg::Vec3f(0.0f, 0.0f, -depth), osg::Vec3f(0.0f, 1.0f, 0.0f));
                Shaders::VisibilityConstants camera = constantsFor(
                    makeOrthographicCameraFromView(view, across, across, size, size, 0.5f, 100000.0f).value());
                litThroughWater(camera, 0.0f);
                camera.mSeaHeading = heading;

                const Shot stand{ .mFrames = 1, .mAverage = false, .mWaterStep = 1.0f / Shaders::RIPPLE_STEP_RATE };
                const Shot run{ .mFrames = 30,
                    .mAverage = false,
                    .mFirstFrame = 1,
                    .mWaterStep = 1.0f / Shaders::RIPPLE_STEP_RATE,
                    .mSetScene = false };
                shoot(scene, {}, camera, size, stand);
                scene.addRipple(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });
                const Frame frame = shoot(scene, {}, camera, size, run);
                std::vector<float> green;
                for (std::size_t i = 0; i < std::size_t{ size } * size; ++i)
                    green.push_back(frame.at(i * 4 + 1));
                return green;
            };

            const std::vector<float> east = looking(osg::Vec2f(1.0f, 0.0f));
            const std::vector<float> north = looking(osg::Vec2f(0.0f, 1.0f));
            const float mean = meanOf(east);
            ASSERT_GT(mean, 0.0f) << "a bed that shows nothing proves nothing";

            float turned = 0.0f;
            float unturned = 0.0f;
            for (std::uint32_t j = 0; j < size; ++j)
                for (std::uint32_t i = 0; i < size; ++i)
                {
                    const std::size_t here = std::size_t{ j } * size + i;
                    const std::size_t there = std::size_t{ i } * size + (size - 1 - j);
                    turned = std::max(turned, std::abs(north[here] - east[there]) / mean);
                    unturned = std::max(unturned, std::abs(north[here] - east[here]) / mean);
                }
            EXPECT_LT(turned, 0.01f) << "the north bed is the east bed turned";
            EXPECT_GT(unturned, 0.1f) << "and not the east bed as it was";
        }

        /// Rain rings the still water under the eye, moves the rings on, and far off roughens it. Close, the
        /// rings are read against a graded sky: a resolved ring is a mirror facet that catches a quarter-degree
        /// sun only where its slope matches exactly, so a sun shows a handful of glints and says nothing of the
        /// rest, while a sky that darkens toward the horizon shows every tilt (a facet leaning toward it
        /// reflects a darker sky than the plane beside it). Forty units over a flat sea with a pixel about half
        /// a unit wide, a ring eleven centimetres across is sixteen pixels and rain differs from a dry frame
        /// over a good share of it; a fifth of a second on, a third of a ring's life, it differs from itself.
        /// The dry frame does not care what time it is.
        ///
        /// Far, the rings are read against the sun, as the road test reads a running sea: from its height a
        /// pixel is hundreds of units and no ring is resolved, so what a ring lost is roughness and the one dot
        /// a flat sea shows spreads into a road. A snowy frame is the plumbing's business and not the
        /// shader's: `mRainOnWater` is nought for snow before the frame is built (`Precipitation::getRainOnWater`).
        TEST_F(RtxVisibilityTest, rainRingsTheWaterUnderTheEyeAndRoughensItFarOff)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::size_t pixelCount = std::size_t{ size } * size;
            constexpr float irradiance = 1.0e-3f;

            // A flat sea, the eye at `eye` looking at `at`, and either a graded sky with no sun or
            // the road test's sun at the centre pixel's mirror under a black sky.
            const auto render = [&](const osg::Vec3f& eye, const osg::Vec3f& at, bool sunlit, float rain, float time,
                                    std::vector<std::uint8_t>& pixels) {
                Shaders::VisibilityConstants camera = Testing::makeCamera(eye, at, 20.0f, size, size, 1000000.0f);

                if (sunlit)
                {
                    osg::Vec3f view = at - eye;
                    view.normalize();
                    camera.mSun = Shaders::sunSource(
                        osg::Vec3f(view.x(), view.y(), -view.z()), osg::Vec3f(irradiance, irradiance, irradiance));
                    camera.mSunDiscColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
                    camera.mSkyHorizon = osg::Vec3f();
                    camera.mSkyZenith = osg::Vec3f();
                }
                else
                {
                    camera.mSun.mIrradiance = osg::Vec3f();
                    camera.mSkyHorizon = osg::Vec3f(0.1f, 0.1f, 0.1f);
                    camera.mSkyZenith = osg::Vec3f(0.9f, 0.9f, 0.9f);
                }

                camera.mWaterLevel = 0.0f;
                camera.mRainOnWater = rain;
                camera.mWaterTime = splitSeconds(time);

                const SceneDesc scene = makeOpenWater(20000.0f);
                pixels = shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } }).bytes();
            };

            const auto differing = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
                std::size_t count = 0;
                for (std::size_t i = 0; i < a.size(); i += 4)
                    count += std::abs(int{ a[i] } - int{ b[i] }) > 2 ? 1 : 0;
                return count;
            };

            const osg::Vec3f near(0.0f, 0.0f, 40.0f);
            const osg::Vec3f nearAt(0.0f, 100.0f, 0.0f);

            std::vector<std::uint8_t> dry;
            std::vector<std::uint8_t> dryLater;
            std::vector<std::uint8_t> raining;
            std::vector<std::uint8_t> rainingLater;
            render(near, nearAt, false, 0.0f, 0.5f, dry);
            render(near, nearAt, false, 0.0f, 0.7f, dryLater);
            render(near, nearAt, false, 1.0f, 0.5f, raining);
            render(near, nearAt, false, 1.0f, 0.7f, rainingLater);

            EXPECT_EQ(dry, dryLater) << "a still sea does not care what time it is";
            EXPECT_GT(differing(raining, dry), pixelCount / 20) << "the rings tilt the sky's reflection";
            EXPECT_GT(differing(rainingLater, raining), pixelCount / 20) << "a fifth of a second on, elsewhere";

            const osg::Vec3f far(0.0f, 0.0f, 6000.0f);
            const osg::Vec3f farAt(0.0f, 14000.0f, 0.0f);

            const auto lit = [](const std::vector<std::uint8_t>& pixels) {
                std::size_t count = 0;
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    count += pixels[i] > 0 ? 1 : 0;
                return count;
            };

            std::vector<std::uint8_t> farDry;
            std::vector<std::uint8_t> farRain;
            render(far, farAt, true, 0.0f, 0.5f, farDry);
            render(far, farAt, true, 1.0f, 0.5f, farRain);
            EXPECT_LT(lit(farDry), 20u) << "a still sea shows the sun once";
            EXPECT_GT(lit(farRain), 200u) << "what the cone could not resolve widened the sun's lobe";
        }

        /// Water too fine for the cone to resolve widens the cone that refracts through it. What the cone could
        /// not resolve is rough and not gone, and that roughness has to reach the texture filter and not only
        /// the specular lobe: a seabed seen through a mile of ruffled water is blurred by the averaged-away
        /// slopes, and read at its sharpest mip it comes back as crawling detail no filter downstream can take
        /// out.
        ///
        /// The sea is built so every wave is past the cone's reach: the spectrum runs from 32 units up to its
        /// peak, so with a peak of 64 a footprint of 66 resolves none of it. Every slope is averaged out and the
        /// surface is geometrically flat (the normal exactly up, the refraction straight down) while the whole
        /// spectrum's variance is still carried as roughness, leaving the cone's width as the only difference
        /// from a still sea, and a ladder of mips on the bed to read it off.
        TEST_F(RtxVisibilityTest, waterTooFineToResolveWidensTheConeItRefractsThrough)
        {
            constexpr std::uint32_t size = 64;
            constexpr float height = 12000.0f;
            constexpr float depth = 1500.0f;

            // How far either way from the middle the cone is read. The bed is a thousand units
            // across and a pixel covers 74 of them, so the sheet is thirteen pixels wide and this
            // leaves a pixel of margin at each edge — no cone in the patch straddles the rim.
            constexpr std::uint32_t across = 5;

            TestTexture ladder;
            paintMipLadder(ladder);
            const std::span<const TextureData> textures(&ladder.mData, 1);

            // A fifth of a degree off the vertical, which is the least `makeCamera` will take.
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -50.0f, height), osg::Vec3f(0.0f, 0.0f, -depth), 20.0f, size, size, 100000.0f);
            camera.mWaterLevel = 0.0f;

            // Water over a bed that glows its own ladder. Emissive rather than lit, so the pixel is the texture
            // and nothing else: an unlit albedo needs a light, and a light puts its own falloff between the mip
            // and the measurement.
            //
            // A patch of the bed and not one pixel of it: `mLostSlope` is a mean over the cone's own footprint,
            // 66 units against a spectrum that stops at 32, about four correlation cells of the waves that carry
            // the slope, so one reading is not an ensemble mean (over this patch the level has a standard
            // deviation of 0.12 and the centre pixel stood 0.8 of one from the patch's mean). The radiance and
            // not the byte: one byte of the display curve is 0.08 of a mip level here, four times the tolerance
            // below. The mean of the cones rather than of their levels, because a level is a logarithm and what
            // the prediction below names is a width.
            const auto coneOver = [&](const SeaState& sea) {
                SceneDesc scene = makeOpenWater(4000.0f);

                const Index bed = scene.addMesh(MeshArrays{
                    .mPositions = sheetAt(500.0f, -depth), .mTexCoords = sQuadUv, .mIndices = sQuadIndices });
                const Index glow = scene.addMaterial(
                    Material{ .mEmissive = scene.textures().add(VFS::Path::NormalizedView("ladder.dds")) });
                scene.addInstance(MeshInstance{ .mMesh = bed, .mMaterial = glow });

                const Frame frame = shoot(scene, textures, camera, size, { .mSea = sea });

                // Back out everything between the texture and the radiance: the emissive scale, the
                // water the glow crossed on its way up, and the two per cent the surface reflected.
                const float carried = Shaders::EMISSIVE_INTENSITY * std::exp(-Shaders::WATER_EXTINCTION[1] * depth)
                    * (1.0f - Shaders::WATER_F0);

                float total = 0.0f;
                for (std::uint32_t y = size / 2 - across; y <= size / 2 + across; ++y)
                    for (std::uint32_t x = size / 2 - across; x <= size / 2 + across; ++x)
                        total += std::exp2(ladderLevel(frame.at((std::size_t{ y } * size + x) * 4 + 1) / carried));

                // In ladder texels, which is a width up to the one scale that cancels below.
                return total / static_cast<float>((2 * across + 1) * (2 * across + 1));
            };

            // How wide the refraction's cone is where it lands, in world units: the pixel's own footprint where it
            // met the water plus what it gained over the leg down. The lobe's width is `ggxConeWidth` of the
            // roughness the lost slopes stand for, and an eighth of it (`refractedConeWidth`), because a facet's
            // tilt turns a reflection by twice itself and a refraction into water by `1 - 1 / n` of itself, so
            // what is seen through a rough surface is blurred that much less. The pixel's cone where it met the
            // water is both what the ladder is read through and what `waveLevel` picks a mip by: one quantity.
            const float footprint = camera.mEyes.mWorld.mSpreadAngle * height;

            const auto coneAtBed = [&](float lobe) {
                const float bent = Shaders::refractedConeWidth(lobe, Shaders::WATER_IOR, false);
                return footprint + (camera.mEyes.mWorld.mSpreadAngle + bent) * depth;
            };

            const SeaState fine{ .mSignificantHeight = 3.0f, .mPeakWavelength = 64.0f };
            const float still = coneOver(SeaState{ .mSignificantHeight = 0.0f });
            const float ruffled = coneOver(fine);

            // The ladder has seven levels and the readout is only meaningful off both ends of it.
            EXPECT_GT(std::log2(still), 1.0f) << "measured 2.25, clear of the sharpest mip";
            EXPECT_LT(std::log2(ruffled), 5.0f) << "measured 3.68, clear of the coarsest";

            // Mip level is the log of the cone's width, so the ratio of the two widths is the ratio of the
            // cones, and the base term (the triangle's texels against its world area) cancels.
            //
            // The lobe is asked of the chain and not of the spectrum: `waveLevel` reads at `log2(footprint /
            // texel)`, so the slope the shader carries is what a mip that wide averaged away and never the
            // whole of it, and `RtxWavePassTest` says the chain and `lostSlopeOf` agree to within the half
            // floats it is stored in. What is left is the two Jensen terms: the patch's mean cone is a mean over
            // pixels of a width in `sqrt(sqrt(lost))`'s cone, where the prediction takes the cone of the mean,
            // and the log of a mean stands over the mean of the logs (0.635 measured against a prediction of
            // 0.629).
            //
            // What the assertion settles is the optics: the lobe's own width, `ggxConeWidth` of the roughness the
            // lost slopes stand for, as a solid's lobe widens its cone too, and half of the `1 - 1/n` that says a
            // refraction is bent by an eighth of what a reflection is. A quarter of the lobe would predict 1.066,
            // twenty tolerances away.
            EXPECT_NEAR(
                std::log2(ruffled / still), std::log2(coneAtBed(lobeOf(fine, footprint)) / coneAtBed(0.0f)), 0.02f)
                << "the cone widened by the lobe the sea's lost slopes stand for";
        }
    }
}
