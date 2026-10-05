#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/visibility.h>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// The denoiser, measured against the estimator it is smoothing.
        ///
        /// One flat floor under an open sky, so every pixel is one bounce off the same normal with
        /// the same answer in expectation: the mean is fixed and the scatter around it is pure
        /// sampling noise. A filter has one job on a surface like this — take the scatter away and
        /// leave the mean where it was — and both halves are asserted, because a filter that dimmed
        /// the picture would pass a test that only looked at the noise.
        TEST_F(RtxVisibilityTest, theFilterTakesTheNoiseOffAFlatSurfaceAndLeavesTheLightWhereItWas)
        {
            constexpr std::uint32_t size = 64;
            constexpr float samples = float{ size } * size;

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.20f, 0.15f, 0.60f);
            camera.mSkyZenith = osg::Vec3f(0.80f, 0.65f, 0.15f);
            camera.mAmbientFromSky = 1.0f;

            const auto shade = [&](bool filter) {
                Frame drawn = shoot(scene, {}, camera, size, { .mFilter = filter });
                EXPECT_EQ(drawn.mHits, size * size);
                return drawn;
            };

            const auto measure = [&](const Frame& drawn, std::size_t channel) {
                float sum = 0.0f;
                float squares = 0.0f;
                for (std::size_t i = channel; i < drawn.mRadiance.size(); i += 4)
                {
                    const float linear = drawn.at(i);
                    sum += linear;
                    squares += linear * linear;
                }

                const float mean = sum / samples;
                return std::pair{ mean, std::sqrt(std::max(squares / samples - mean * mean, 0.0f)) };
            };

            const Frame raw = shade(false);
            const Frame filtered = shade(true);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto [rawMean, rawSpread] = measure(raw, channel);
                const auto [filteredMean, filteredSpread] = measure(filtered, channel);

                // Half an sRGB step at this brightness, which is the most the two can differ by
                // without one of them having moved the light.
                EXPECT_NEAR(filteredMean, rawMean, 0.004f) << "channel " << channel << " keeps its light";

                EXPECT_LT(filteredSpread, rawSpread * 0.2f)
                    << "channel " << channel << " has most of its noise taken away";
            }

            // **The raw frame is the trace's own composition, and a sum of one frame is that
            // frame.** Nothing filters it, so the trace put the bounce into the direct channel
            // itself, and the composite that takes the sum must not add it a second time: frame
            // nought summed once reads back as frame nought, value for value.
            std::vector<float> bounce;
            mRenderer.readChannel(Channel::Indirect, bounce);
            ASSERT_GT(*std::ranges::max_element(bounce), 0.0f)
                << "a bounce of nothing adds nothing twice, and this would prove nothing";

            const Frame summed = shoot(scene, {}, camera, size, { .mFrames = 1 });
            EXPECT_EQ(summed.mRadiance, raw.mRadiance) << "the sum of one unfiltered frame is that frame";
        }

        /// **A lamp that goes out leaves the picture on the frame it goes.** One lamp over a floor for
        /// 32 frames, then the same floor without it, placed and not handed over, so every history
        /// carries on. The lamp's light is the shadow denoiser's unshadowed sum times a filtered bit,
        /// and only the bit has a history: the sum is nought on the first frame without the lamp, and
        /// so is the light. Under a black sky the floor's bounce finds nothing, so the pixel under
        /// the lamp is the unlit floor's exactly. Through the accumulator, as the lamps' light went
        /// before, 24% of it was left eight frames on.
        TEST_F(RtxVisibilityTest, aLampThatGoesOutLeavesThePictureOnTheFrameItGoes)
        {
            constexpr std::uint32_t size = 32;
            constexpr std::size_t centre = centreValueOf(size);

            SceneDesc unlit;
            addQuad(unlit, sheetAt(4000.0f, 0.0f));

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            scene.addLight(Light{
                .mPosition = osg::Vec3f(0.0f, 0.0f, 60.0f),
                .mIntensity = osg::Vec3f(4000.0f, 4000.0f, 4000.0f),
                .mReach = 500.0f,
            });

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();

            const float dark = shoot(unlit, {}, camera, size,
                { .mFrames = 32, .mAverage = false, .mFilter = true, .mLoss = HistoryLoss::Cut })
                                   .at(centre);
            const float lit = shoot(scene, {}, camera, size,
                { .mFrames = 32, .mAverage = false, .mFilter = true, .mLoss = HistoryLoss::Cut })
                                  .at(centre);
            ASSERT_GT(lit - dark, 0.01f) << "a lamp that lights nothing proves nothing";

            // The lamp goes: the per-frame lists are emptied and the floor placed again.
            scene.clearPlacement();
            const float after = shoot(scene, {}, camera, size,
                { .mFrames = 1, .mAverage = false, .mFirstFrame = 32, .mFilter = true, .mSetScene = false })
                                    .at(centre);
            EXPECT_EQ(after, dark) << "lit " << lit;
        }

        /// **The filter rebuilds an arm's pixels through the arms' own eye**, as the trace cast them,
        /// and so smooths an arm as it smooths the floor. The player's arms are traced through a
        /// ninety-degree eye beside the world's thirty here, and rebuilt through the world's, a flat
        /// arm stood on planes it is not: a tap sixteen pixels off the centre stood ten units off the
        /// centre's plane where a pixel is under one wide, and the coarse levels of the cascade
        /// turned their neighbours away and left the noise.
        TEST_F(RtxVisibilityTest, theFilterRebuildsAnArmThroughTheArmsOwnEye)
        {
            constexpr std::uint32_t size = 64;
            constexpr float samples = float{ size } * size;

            SceneDesc scene;
            const std::array<osg::Vec3f, 4> arm = uprightQuadAt(400.0f, 100.0f);
            scene.addInstance(MeshInstance{ .mMesh
                = scene.addMesh(MeshArrays{ .mPositions = arm, .mTexCoords = sQuadUv, .mIndices = sQuadIndices }),
                .mClass = InstanceClass::FirstPerson });

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 100.0f, 0.0f), 30.0f, size, size, 100000.0f);
            camera.mEyes.mArms = cameraAtFieldOfView(camera.mEyes.mWorld, 90.0f);
            camera.mSkyHorizon = osg::Vec3f(0.20f, 0.15f, 0.60f);
            camera.mSkyZenith = osg::Vec3f(0.80f, 0.65f, 0.15f);
            camera.mAmbientFromSky = 1.0f;

            const auto spreadOf = [&](bool filter) {
                const Frame drawn = shoot(scene, {}, camera, size, { .mFilter = filter });
                EXPECT_EQ(drawn.mHits, size * size) << "the arm does not fill the arms' eye";

                float sum = 0.0f;
                float squares = 0.0f;
                for (std::size_t i = 1; i < drawn.mRadiance.size(); i += 4)
                {
                    sum += drawn.at(i);
                    squares += drawn.at(i) * drawn.at(i);
                }
                const float mean = sum / samples;
                return std::pair{ mean, std::sqrt(std::max(squares / samples - mean * mean, 0.0f)) };
            };

            const auto [rawMean, rawSpread] = spreadOf(false);
            const auto [filteredMean, filteredSpread] = spreadOf(true);
            ASSERT_GT(rawSpread, 0.0f) << "a bounce with no noise proves nothing";
            EXPECT_NEAR(filteredMean, rawMean, 0.004f) << "the arm keeps its light";
            // Measured over the cascade's four levels: the arms' eye leaves 2.3 per cent of the
            // spread, and the world's 16.4 — its coarser levels turn their taps away. A bound between
            // the two.
            EXPECT_LT(filteredSpread, rawSpread * 0.045f) << "the arm's noise was not taken away";
        }

        /// The same floor at a grazing angle, against the answer it is trying to reach.
        ///
        /// **Terrain is nearly always seen this way, and it is the case a depth test gets wrong.**
        /// Pixels down a grazing surface stand a long way apart in distance while remaining one
        /// flat plane, so a filter that refused taps by how far away they are keeps only the taps
        /// across the slope and throws away the ones along it — it still smooths, just half as
        /// well, which is why this measures the error rather than the smoothness. Weighing by how
        /// far a tap sits off the centre pixel's tangent plane costs one dot product and asks the
        /// question that was meant.
        ///
        /// The reference is what `--accumulate` builds: sixty-four differently seeded samples of
        /// the same unbiased estimator, averaged. One sample against that is the error a denoiser
        /// exists to reduce, and the ratio of the two is the only honest way to say it worked.
        ///
        /// **The bound sits between the two weightings on purpose.** Measured here on the composite
        /// (`readRadiance`): one sample is 0.0335 off the reference and the plane weight brings
        /// that to 0.0019, seventeen times better; a plain depth weight, measured when one sample
        /// stood 0.0420 off, brought it to 0.0061, seven times. Every number is repeatable, because
        /// frame zero and a sixty-four frame average are both deterministic, so a tenth is a bound
        /// this passes with room and a depth test cannot reach.
        TEST_F(RtxVisibilityTest, theFilterAndItsHistoryConvergeOnAGrazingSurface)
        {
            constexpr std::uint32_t size = 64;

            SceneDesc scene;
            addQuad(scene, sheetAt(40000.0f, 0.0f));

            // A degree and a half above the floor: the horizon sits near the top of the frame and
            // the ground runs from a few hundred units away to eight thousand, so the distance
            // between vertical neighbours changes by more than a pixel footprint nearly everywhere.
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -8000.0f, 200.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.20f, 0.15f, 0.60f);
            camera.mSkyZenith = osg::Vec3f(0.80f, 0.65f, 0.15f);
            camera.mAmbientFromSky = 1.0f;

            const auto render = [&](std::uint32_t accumulate, bool filter) {
                std::vector<float> values;
                values = shoot(scene, {}, camera, size, { .mFrames = accumulate, .mFilter = filter }).mRadiance;
                return values;
            };

            // Unfiltered, because a thousand filtered frames converge on the filter's opinion and
            // not on the answer.
            const std::vector<float> reference = render(64, false);
            const std::vector<float> raw = render(0, false);
            const std::vector<float> filtered = render(0, true);

            const auto errorAgainstReference = [&](const std::vector<float>& values) {
                float squares = 0.0f;
                std::size_t counted = 0;
                for (std::size_t i = 1; i < values.size(); i += 4)
                {
                    // Only where there is a surface: the sky above the horizon is not being
                    // filtered and averages to itself, so counting it would dilute both figures.
                    if (values[i] == reference[i] && values[i] == 0.0f)
                        continue;

                    const float error = values[i] - reference[i];
                    squares += error * error;
                    ++counted;
                }

                return std::sqrt(squares / static_cast<float>(counted));
            };

            const float before = errorAgainstReference(raw);
            const float after = errorAgainstReference(filtered);

            EXPECT_GT(before, 0.02f) << "one sample is noisy enough here for the question to mean something";
            EXPECT_LT(after, before * 0.10f)
                << "and the filter takes most of that error away: " << before << " becomes " << after;

            // **And the temporal half, which is where the error actually falls.** The spatial cascade
            // borrows samples sideways from neighbours looking at the same surface, which has a
            // floor: there are only so many of those and they are correlated. Averaging over frames
            // borrows from samples that are genuinely independent, so its error keeps falling for as
            // long as the history is allowed to grow.
            //
            // Sequenced frames with no averaging in the composite: the sampler advances, so each
            // frame is a different draw, and the only thing combining them is the accumulator under
            // test. A still camera, so every pixel reprojects onto itself and no history is
            // rejected — which is the case this has to get right before any other.
            //
            // **A cut before each run, and leaving it out is what made this test lie.** The
            // fixture renders many frames, the accumulator keeps what it built across all of them,
            // and a "one frame" baseline taken without a reset is a baseline that already has a
            // history in it — which reads as the accumulator doing nothing at all.
            const auto renderSequence = [&](std::uint32_t frames) {
                std::vector<float> values;
                values = shoot(scene, {}, camera, size, filteredRun(frames)).mRadiance;
                return values;
            };

            const std::vector<float> settledPixels = renderSequence(Shaders::ACCUMULATE_FRAMES);
            const float settled = errorAgainstReference(settledPixels);

            // **The accumulator may not make this worse, and on this surface that is the whole of
            // what it can be asked.** Measured here, the cascade alone already lands at 0.0019 of
            // the converged reference — a flat sheet under a smooth sky is precisely
            // where à-trous has every advantage, since the signal is uniform and
            // every neighbour is a valid sample of it. What the history is for is the case this
            // scene does not have: contact regions, small geometry, and pixels with few neighbours
            // looking at the same thing, which is what
            // `theHistoryCarriesWhereTheCascadeHasNoNeighboursToBorrow` is for.
            //
            // **Eight per cent of room, because the history is the filtered light** (SVGF's
            // feedback), and averaging the cascade's answers over frames on a surface this easy
            // correlates them more than it adds. Measured: the cascade 0.00194 and the settled
            // history 0.00177, against an unfiltered 0.0335.
            //
            // **A flat sheet is where feeding the filtered light back has least to give**, since the
            // cascade has every neighbour it could want and averaging its answers over frames only
            // correlates them. What the feedback is for is the other scene, and
            // `theHistoryCarriesWhereTheCascadeHasNoNeighboursToBorrow` records what it does there:
            // 0.0050 settled against 0.0164 alone.
            EXPECT_LE(settled, after * 1.08f)
                << "the history does not cost what the cascade gained: " << after << " becomes " << settled;

            // **And it converges toward the reference rather than toward its own opinion.** An
            // average that drifted would still be quieter, and quieter is not the claim: the mean of
            // the settled picture has to sit where the converged one does, or the accumulator is
            // dimming the frame and calling it denoising.
            double settledMean = 0.0;
            double referenceMean = 0.0;
            std::size_t counted = 0;
            for (std::size_t i = 1; i < settledPixels.size(); i += 4)
            {
                if (settledPixels[i] == reference[i] && settledPixels[i] == 0.0f)
                    continue;

                settledMean += static_cast<double>(settledPixels[i]);
                referenceMean += static_cast<double>(reference[i]);
                ++counted;
            }

            ASSERT_GT(counted, 0u);
            settledMean /= static_cast<double>(counted);
            referenceMean /= static_cast<double>(counted);

            EXPECT_NEAR(settledMean, referenceMean, referenceMean * 0.02)
                << "the accumulated mean is " << settledMean << " against a converged " << referenceMean;
        }

        /// A floor meeting a wall, and the filter keeping them apart.
        ///
        /// **Everything else here would pass with a plain blur.** Smoothing noise and preserving a
        /// mean are what any average does; what makes this a denoiser rather than a soft-focus
        /// filter is that it refuses to mix two surfaces that happen to be neighbours on screen.
        ///
        /// So this measures the one place where that shows: the step from one row to the next
        /// across the crease. Away from it a blur is nearly harmless, because every level of a
        /// B3 kernel puts most of its weight near the centre however far the taps reach — which is
        /// exactly why a test comparing the two ends of the frame passes with the guide switched
        /// off, and this one does not.
        ///
        /// The crease is found rather than assumed: it is the row boundary where the unfiltered
        /// picture jumps hardest, which is where the geometry says it should be. The two surfaces
        /// are told apart by their normals alone, and lit differently for the same reason — a
        /// floor's cosine-weighted hemisphere is centred on the zenith and a wall's lies along the
        /// horizon, and this sky runs a long way between the two.
        TEST_F(RtxVisibilityTest, theFilterWillNotMixAFloorIntoTheWallStandingOnIt)
        {
            constexpr std::uint32_t size = 64;

            const std::array wall = uprightQuadAt(2000.0f, 0.0f, osg::Vec2f(0.0f, 2000.0f));

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, wall);

            // The floor fills the bottom of the frame and the wall the top, with the crease running
            // straight across the middle of it.
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1200.0f, 900.0f), osg::Vec3f(0.0f, 0.0f, 250.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.20f, 0.15f, 0.60f);
            camera.mSkyZenith = osg::Vec3f(0.80f, 0.65f, 0.15f);
            camera.mAmbientFromSky = 1.0f;

            // Green, where this sky has its widest range between the horizon and the zenith. A row
            // at a time, so that sixty-four pixels stand behind every number and the sampling noise
            // that is left cannot be mistaken for a step.
            const auto rowMeans = [&](std::uint32_t accumulate, bool filter) {
                const Frame frame = shoot(scene, {}, camera, size, { .mFrames = accumulate, .mFilter = filter });
                EXPECT_EQ(frame.mHits, size * size);

                std::array<float, size> rows{};
                for (std::uint32_t y = 0; y < size; ++y)
                {
                    float sum = 0.0f;
                    for (std::uint32_t x = 0; x < size; ++x)
                        sum += frame.at((std::size_t{ y } * size + x) * 4 + 1);

                    rows[y] = sum / size;
                }

                return rows;
            };

            // Where the crease is, off a converged frame rather than a noisy one. A single sample's
            // row means swing by more than the step does, so asking a noisy picture where its
            // biggest jump is answers with the loudest pixel and not with the geometry.
            const std::array<float, size> converged = rowMeans(64, false);

            std::uint32_t crease = 0;
            for (std::uint32_t y = 1; y < size; ++y)
                if (std::abs(converged[y] - converged[y - 1]) > std::abs(converged[crease + 1] - converged[crease]))
                    crease = y - 1;

            const float truth = std::abs(converged[crease + 1] - converged[crease]);
            const std::array<float, size> filtered = rowMeans(0, true);
            const float kept = std::abs(filtered[crease + 1] - filtered[crease]);

            ASSERT_GT(truth, 0.02f) << "the two surfaces have to part company for this to mean anything";
            EXPECT_GT(kept, truth * 0.7f) << "the step at row " << crease << " survives the filter: it is " << truth
                                          << " in the converged frame and " << kept << " in the filtered one";
        }

        /// The exposure moves toward what it measured rather than snapping to it.
        ///
        /// **Adaptation is a time-domain thing.** A histogram measured on the frame the curve is
        /// about to map and applied to that same frame turns any one-frame excursion in it into a
        /// one-frame excursion in the whole image — and the degenerate branch could take a night
        /// exterior from an exposure of order tens to exactly one between two frames.
        ///
        /// Two skies a factor of thirty-two apart and nothing else in the picture, so the histogram
        /// is the only thing that changed. Every case is claimed: told it has no past, the eye
        /// arrives at once; told it has one, it has barely moved a frame later; told no time passed,
        /// it has not moved at all; held, it has not moved whatever it was told.
        ///
        /// **Driven frame by frame rather than through `shoot`**, because that helper calls
        /// `setScene` every time and a new scene costs every history — the eye's too, and a lost eye
        /// is exactly what the middle frame here must not have.
        TEST_F(RtxVisibilityTest, theExposureMovesTowardWhatItMeasuresRatherThanSnappingToIt)
        {
            constexpr std::uint32_t size = 32;

            Shaders::VisibilityConstants bright = Testing::makeCamera(
                osg::Vec3f(0.0f, 0.0f, 200.0f), osg::Vec3f(0.0f, 1000.0f, 200.0f), 60.0f, size, size, 100000.0f);
            bright.mSkyHorizon = osg::Vec3f(0.8f, 0.8f, 0.8f);
            bright.mSkyZenith = bright.mSkyHorizon;
            bright.mAmbientFromSky = 1.0f;

            Shaders::VisibilityConstants dim = bright;
            dim.mSkyHorizon = bright.mSkyHorizon / 32.0f;
            dim.mSkyZenith = dim.mSkyHorizon;
            dim.mAmbientFromSky = 1.0f;

            std::vector<std::uint8_t> pixels;

            const auto meanByte = [&pixels] {
                double sum = 0.0;
                std::size_t counted = 0;
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        sum += pixels[i + channel];
                        ++counted;
                    }

                return counted > 0 ? sum / static_cast<double>(counted) : 0.0;
            };

            // The exposure measured rather than pinned, which is the whole subject, `since` after the
            // last frame.
            const auto shot = [&](const Shaders::VisibilityConstants& camera, const float since = 1.0f / 60.0f,
                                  const HistoryLoss loss = HistoryLoss::None) {
                mRenderer.renderFrame(
                    camera, FrameOptions{ .mSinceLast = since, .mLoss = loss, .mExposure = ExposureRule{} });
                mRenderer.readPixels(pixels);
                return meanByte();
            };

            // Nothing to hit, so every pixel is the sky and the mean of the frame is the sky. The
            // scene is set once: setting it again would clear the previous camera and reset the eye.
            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), SceneDesc{}, {});

            const double lit = shot(bright);
            ASSERT_GT(lit, 0.0) << "the bright sky rendered as black";

            // The same sky thirty-two times darker, one frame later and with a past to move from.
            // The eye has had a sixtieth of a second against a time constant of a second and a half,
            // so it has gone almost nowhere.
            const double justAfter = shot(dim);

            // No time at all after that, and the eye has had none to move in: the exposure a reset
            // takes outright is the reset's to take, and a frame that stood for nothing is no reset.
            EXPECT_EQ(shot(dim, 0.0f), justAfter) << "the eye moved in no time";

            // And the same sky again with no past, which is where it is headed.
            const double adapted = shot(dim, 1.0f / 60.0f, HistoryLoss::Cut);

            EXPECT_GT(adapted, 0.0) << "the dark sky rendered as black even with the eye open";
            EXPECT_LT(justAfter, 0.5 * adapted)
                << "the exposure arrived in one frame: " << justAfter << " against " << adapted;

            // **Held, the eye stays where the frame before left it, reset or not**: the dim sky told
            // it has no past draws exactly what the adapted frame drew, and the bright sky under the
            // dim eye comes out brighter than it did under its own.
            const auto held
                = [&](const Shaders::VisibilityConstants& camera, const HistoryLoss loss = HistoryLoss::None) {
                      mRenderer.renderFrame(camera,
                          FrameOptions{ .mSinceLast = 1.0f / 60.0f, .mLoss = loss, .mExposure = HeldExposure{} });
                      mRenderer.readPixels(pixels);
                      return meanByte();
                  };
            EXPECT_EQ(held(dim, HistoryLoss::Cut), adapted) << "a held eye measured the frame";
            EXPECT_GT(held(bright), lit) << "a held eye adapted to the bright sky";

            // **A new extent keeps the eye**: an eye adapted to the bright sky meets the dim one past
            // two upscale modes as it met it before them, barely moved, where an eye that lost its
            // past would take the dim sky outright.
            EXPECT_EQ(shot(bright, 1.0f / 60.0f, HistoryLoss::Cut), lit);
            mRenderer.setUpscale(Upscale::Quality);
            mRenderer.setUpscale(Upscale::Off);
            EXPECT_LT(shot(dim), 0.5 * adapted) << "a new extent snapped the eye";

            // **And a new world loses it, whatever frame comes first**: a held frame after the world
            // is handed over spends nothing of the eye's loss, and the measured frame after it takes
            // the dim sky outright, as a cut's frame does, rather than easing from the old world's eye.
            mRenderer.setScene(Rtx::SceneSlot::world(), SceneDesc{}, {});
            held(bright);
            EXPECT_EQ(shot(dim), adapted) << "the measured frame eased from the old world's eye";
        }

        /// A reset survives a frame that has no history to reset, and such a frame leaves none.
        ///
        /// **A cut is spent by the frame that answers it, and a frame with neither denoiser
        /// answers nothing.** A frame not `mDenoised` runs no accumulator and `Upscale::Off` runs no
        /// upscaler, so nothing reads the signal — and a renderer that cleared it at the end of every
        /// frame regardless dropped the reset rather than deferring it. What the game does with that
        /// is walk through a door on an unfiltered frame and reproject one room onto another on the
        /// next filtered one.
        ///
        /// **The claim is exact rather than statistical.** An accumulator that was reset writes the
        /// frame's own sample and reads no past at all, so the picture is the one the same trace
        /// makes from a fresh reset — value for value, since the frame index is what seeds the
        /// sampler and the camera never moves. The accumulated frame between them is what proves the
        /// comparison can tell a history from none.
        ///
        /// **And the reset's own sample is a history of one**, so a filtered frame after it is not a
        /// fresh reset's picture: the same exact claim, the other way round.
        ///
        /// **A frame that takes no indirect light ends the bounce's history too**, though
        /// the denoisers run on it: it keeps the surface's history and lets the bounce's mean go, so
        /// the traced frame after it reads none — the fresh reset's picture again, with the mean's
        /// images let go and made again between them. So does a traced frame after a menu let the
        /// images go and made them again with no frame between.
        TEST_F(RtxVisibilityTest, aResetSurvivesAFrameThatHasNoHistoryToReset)
        {
            constexpr std::uint32_t size = 64;

            // The index every measured frame is drawn at, so the three of them differ only in what
            // the accumulator was handed. The frames around them take other indices.
            constexpr std::uint32_t measured = 3;

            SceneDesc scene;
            addQuad(scene, sheetAt(40000.0f, 0.0f));

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -8000.0f, 200.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.20f, 0.15f, 0.60f);
            camera.mSkyZenith = osg::Vec3f(0.80f, 0.65f, 0.15f);
            camera.mAmbientFromSky = 1.0f;

            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            const auto renderOne = [&](std::uint32_t frame, bool filter, HistoryLoss loss = HistoryLoss::None,
                                       IndirectLight indirect = IndirectLight::Traced) {
                Shaders::VisibilityConstants sampled = camera;
                sampled.mFrame = frame;
                mRenderer.renderFrame(sampled,
                    FrameOptions{ .mAccumulate = 0,
                        .mLoss = loss,
                        .mReconstruction = ReconstructionRequest{ .mDenoise = filter,
                            .mBounceReuse = BounceReuse::Off,
                            .mIndirect = indirect },
                        .mExposure = FixedExposure{ 1.0f } });
            };

            const auto radiance = [&] {
                std::vector<float> values;
                mRenderer.readComposite(values);
                return values;
            };

            const auto mostTheyDifferBy = [](const std::vector<float>& left, const std::vector<float>& right) {
                float most = 0.0f;
                for (std::size_t i = 0; i < left.size(); ++i)
                    most = std::max(most, std::abs(left[i] - right[i]));

                return most;
            };

            // A previous camera to reproject against, so nothing below is reset by the zero basis
            // that catches a renderer's first frame instead of by the flag under test.
            renderOne(measured + 1, true);

            renderOne(measured, true, HistoryLoss::Cut);
            const std::vector<float> single = radiance();

            // The same trace with a history behind it, at indices the sampler has not drawn yet so
            // the mean is over genuinely different draws.
            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
                renderOne(frame + 100, true);

            renderOne(measured, true);
            const std::vector<float> accumulated = radiance();

            ASSERT_EQ(accumulated.size(), single.size());
            ASSERT_GT(mostTheyDifferBy(accumulated, single), 0.0f)
                << "a history behind the same trace has to change the picture, or this proves nothing";

            // The frame under test sits between the reset and the frame that can act on it, and
            // reads the signal nowhere.
            renderOne(measured + 2, false, HistoryLoss::Cut);
            renderOne(measured, true);
            const std::vector<float> carried = radiance();

            ASSERT_EQ(carried.size(), single.size());
            EXPECT_EQ(mostTheyDifferBy(carried, single), 0.0f) << "the unfiltered frame spent a reset it could not use";

            // **And an unfiltered frame with no reset ends the history behind it all the same**: the
            // filtered frame after it reads none, rather than the one from the frame before it as if
            // it were the last frame's.
            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
                renderOne(frame + 200, true);
            renderOne(measured + 2, false);
            renderOne(measured, true);
            const std::vector<float> skipped = radiance();

            ASSERT_EQ(skipped.size(), single.size());
            EXPECT_EQ(mostTheyDifferBy(skipped, single), 0.0f)
                << "a filtered frame after an unfiltered one read a history from before it";

            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
                renderOne(frame + 300, true);
            renderOne(measured + 2, true, HistoryLoss::None, IndirectLight::Off);
            renderOne(measured, true);
            const std::vector<float> resumed = radiance();

            ASSERT_EQ(resumed.size(), single.size());
            EXPECT_EQ(mostTheyDifferBy(resumed, single), 0.0f)
                << "a traced frame after one with no indirect light read a mean of the bounce from before it";

            // **And where the menu lets the mean's images go and makes them again with no frame
            // between**, the frame after reads none of what the new images hold.
            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
                renderOne(frame + 400, true);
            mRenderer.setIndirectLight(IndirectLight::Off);
            mRenderer.setIndirectLight(IndirectLight::Traced);
            renderOne(measured, true);
            const std::vector<float> remade = radiance();

            ASSERT_EQ(remade.size(), single.size());
            EXPECT_EQ(mostTheyDifferBy(remade, single), 0.0f)
                << "a traced frame read the mean's images the menu had just made as a history";

            // And a filtered frame in its place keeps the sample the reset took. Counted as no
            // history, it would blend at a weight of one — the next frame alone, which is the fresh
            // reset's picture to the value.
            renderOne(measured + 2, true, HistoryLoss::Cut);
            renderOne(measured, true);
            const std::vector<float> followed = radiance();

            ASSERT_EQ(followed.size(), single.size());
            EXPECT_GT(mostTheyDifferBy(followed, single), 0.0f)
                << "the frame after a reset threw the reset's sample away";
        }

        /// **The history fix takes the noise off what the eye turns to.** A floor and a
        /// wall standing on it under a black sky, and a lamp twenty units off the wall whose reach
        /// lights a spot of it: the floor's bounce is that spot, which a ray finds now and then, so
        /// one frame of it is noisy after the wavelet, as a lamp's bounce in a room is. The eye stands
        /// still for 32 frames, then turns 14 degrees in one: the edge it turns toward brings in
        /// `(tan 30° - tan 16°) / (tan 30° / 32)` = 16 columns that hold one frame, and the rest of
        /// the frame holds thirty-two. The strip is the twelve columns at that edge.
        ///
        /// Over four draws: the strip's spread from one draw to the next, over its mean, and the mean
        /// against 128 unfiltered frames where the eye ends. **The noise in the light's own units**:
        /// without the fix the brightness test passes over a fresh pixel's rare bright draws, and a
        /// strip at a third of its light is quieter by as much and no better for it. **The clamp is
        /// off in every run**: with no reuse, its box of fifty samples mostly holds none of a light
        /// this rare and holds the strip near nought, which is `ACCUMULATE_FAST_FRAMES`'s trade and not
        /// this test's question. So is the ring, which holds the fresh strip down before the fix
        /// borrows, `ACCUMULATE_RING_FRAMES`'s trade.
        ///
        /// Measured: the strip's noise 1.78 of its mean without the fix and 0.72 with it, its mean 0.28
        /// of the truth without the fix and 1.03 with it; the same strip held still, 0.30 at 0.61. With
        /// the fixed variance `shortHistoryVariance` replaced and three 5×5 levels, 13.6 and 0.87, and
        /// 0.61 and 1.08.
        TEST_F(RtxVisibilityTest, theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t still = 32;
            constexpr std::uint32_t draws = 4;
            constexpr std::uint32_t strip = 12;
            const float turn = osg::DegreesToRadians(14.0f);

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, -100.0f));
            addQuad(scene, uprightQuadAt(4000.0f, 300.0f));
            scene.addLight(Light{
                .mPosition = osg::Vec3f(0.0f, 280.0f, -40.0f),
                .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                .mReach = 150.0f,
            });

            const osg::Vec3f eye(0.0f, -200.0f, 50.0f);
            const osg::Vec3f ahead(0.0f, 500.0f, -150.0f);
            const auto standing = [&](float yaw, std::uint32_t frame) {
                const osg::Vec3f looking(ahead.x() * std::cos(yaw) - ahead.y() * std::sin(yaw),
                    ahead.x() * std::sin(yaw) + ahead.y() * std::cos(yaw), ahead.z());
                Shaders::VisibilityConstants camera
                    = Testing::makeCamera(eye, eye + looking, 60.0f, size, size, 10000.0f);
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mFrame = frame;
                return camera;
            };

            const std::vector<float> reference
                = shoot(scene, {}, standing(turn, 0), size, { .mFrames = 128 }).mRadiance;

            // Each column's green channel at the last frame of a run of `still` frames standing at
            // `from` and one at `to`: its spread over the draws, as a variance summed over the rows,
            // and its sum over the rows and the draws.
            struct Columns
            {
                std::vector<double> mVariances;
                std::vector<double> mSums;
            };
            std::vector<float> read;
            std::vector<double> sums(std::size_t{ size } * size);
            std::vector<double> squares(std::size_t{ size } * size);
            const auto columnsOf = [&](float from, float to, bool fix) {
                std::ranges::fill(sums, 0.0);
                std::ranges::fill(squares, 0.0);
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    for (std::uint32_t at = 0; at <= still; ++at)
                    {
                        mRenderer.renderFrame(standing(at == still ? to : from, 1000 + 100 * draw + at),
                            FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                                .mReconstruction = ReconstructionRequest{ .mDenoise = true,
                                    .mBounceReuse = BounceReuse::Off,
                                    .mAntilag = false,
                                    .mHistoryFix = fix,
                                    .mAntiFirefly = false },
                                .mExposure = FixedExposure{ 1.0f } });
                        EXPECT_TRUE(mRenderer.finishFrame().has_value());
                    }
                    mRenderer.readComposite(read);
                    for (std::size_t pixel = 0; pixel < sums.size(); ++pixel)
                    {
                        const double value = static_cast<double>(read[pixel * 4 + 1]);
                        sums[pixel] += value;
                        squares[pixel] += value * value;
                    }
                }
                Columns columns{ std::vector<double>(size, 0.0), std::vector<double>(size, 0.0) };
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = 0; x < size; ++x)
                    {
                        const std::size_t pixel = std::size_t{ y } * size + x;
                        const double mean = sums[pixel] / draws;
                        columns.mVariances[x] += (squares[pixel] - draws * mean * mean) / (draws - 1);
                        columns.mSums[x] += sums[pixel];
                    }
                return columns;
            };
            // The strip's mean, and its noise as a deviation over that mean: a strip the brightness
            // test darkened is quieter by as much as it is darker, and no less noisy for its light.
            const auto overStrip = [&](const std::vector<double>& column, std::uint32_t per) {
                double sum = 0.0;
                for (std::uint32_t x = 0; x < strip; ++x)
                    sum += column[x];
                return sum / static_cast<double>(strip * size * per);
            };
            const auto meanOf = [&](const Columns& columns) { return overStrip(columns.mSums, draws); };
            const auto noiseOf
                = [&](const Columns& columns) { return std::sqrt(overStrip(columns.mVariances, 1)) / meanOf(columns); };

            // A yaw toward -x turns the eye left, so the strip is the picture's first twelve columns.
            const Columns held = columnsOf(turn, turn, false);
            const Columns without = columnsOf(0.0f, turn, false);
            const Columns with = columnsOf(0.0f, turn, true);
            ASSERT_GT(noiseOf(without), 1.5 * noiseOf(held))
                << "the strip the eye turned to is no noisier than the same strip held still, so this proves nothing: "
                << noiseOf(without) << " against " << noiseOf(held);
            EXPECT_LT(noiseOf(with), 0.5 * noiseOf(without))
                << "the history fix left " << noiseOf(with) << " of the strip's " << noiseOf(without)
                << ", where the strip held still holds " << noiseOf(held);

            double truth = 0.0;
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < strip; ++x)
                    truth += static_cast<double>(reference[(std::size_t{ y } * size + x) * 4 + 1]);
            truth /= static_cast<double>(strip * size);
            const double unfixedShare = meanOf(without) / truth;
            const double fixedShare = meanOf(with) / truth;
            EXPECT_LT(std::abs(fixedShare - 1.0), std::abs(unfixedShare - 1.0))
                << "the history fix took the strip's light from " << unfixedShare << " of the truth to " << fixedShare;
            EXPECT_NEAR(fixedShare, 1.0, 0.1) << "the history fix moved the strip's light off the truth";
        }

        /// **A rare bright bounce on a history of a few frames is held to the light around it, and a
        /// settled history keeps it** (`ACCUMULATE_RING_FRAMES`).
        ///
        /// A floor under an open sky, and 300 units over it a sheet 40 units square glowing at `50 ×
        /// EMISSIVE_INTENSITY`, 400: a bounce off the floor beneath finds it at a chance of about
        /// `40² / 300² / π`, 0.0057, and brings back near two hundred times the floor's mean — the
        /// lanterns' paper over the M[FR] guild's tree, on a surface that holds still.
        ///
        /// After a cut every history is fresh, and three frames on a pixel whose bounce found the sheet
        /// stands far over the truth: counted where the green stands four times over 64 unfiltered
        /// frames', pooled over four draws. With the ring, a tenth of them at most. Then 160 still
        /// frames, whose means with and without the ring stand within a hundredth: a mean of more than
        /// eight frames keeps what it took in, and the first eight frames, a quarter of a mean of 32,
        /// are `(31/32)^128` of that quarter 128 frames later, under half a hundredth of the mean
        /// however much of their light the ring took.
        ///
        /// Measured: 8944 fireflies without the ring, the history fix having spread each over its
        /// taps, and 164 with it; the settled means 0.6969 and 0.6955.
        TEST_F(RtxVisibilityTest, theRingHoldsAFreshFireflyAndLeavesASettledMeanItsLight)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t fresh = 3;
            constexpr std::uint32_t draws = 4;
            constexpr std::uint32_t settled = 160;

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, sheetAt(20.0f, 300.0f),
                scene.addMaterial(Material{ .mEmissiveColour = osg::Vec3f(50.0f, 50.0f, 50.0f), .mTwoSided = true }));

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -200.0f, 150.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.2f, 0.2f, 0.2f);
            camera.mSkyZenith = osg::Vec3f(0.2f, 0.2f, 0.2f);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mAmbientFromSky = 1.0f;

            const std::vector<float> reference = shoot(scene, {}, camera, size, { .mFrames = 64 }).mRadiance;

            // The composite after `frames` frames from a cut, each its own draw from `first` on.
            std::vector<float> read;
            const auto run = [&](bool ring, std::uint32_t frames, std::uint32_t first) {
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    camera.mFrame = first + at;
                    mRenderer.renderFrame(camera,
                        FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mReconstruction = ReconstructionRequest{ .mDenoise = true,
                                .mBounceReuse = BounceReuse::Off,
                                .mAntilag = false,
                                .mAntiFirefly = ring },
                            .mExposure = FixedExposure{ 1.0f } });
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                }
                mRenderer.readComposite(read);
            };

            const auto firefliesOf = [&](bool ring) {
                std::uint32_t count = 0;
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    run(ring, fresh, 1000 + 100 * draw);
                    for (std::size_t at = 1; at < read.size(); at += 4)
                        count += read[at] > 4.0f * reference[at] ? 1u : 0u;
                }
                return count;
            };
            const std::uint32_t without = firefliesOf(false);
            ASSERT_GT(without, 20u) << "three frames from a cut stand under the sheet's fireflies nowhere, so this "
                                       "proves nothing";
            const std::uint32_t with = firefliesOf(true);
            EXPECT_LT(10 * with, without) << "the ring left " << with << " of " << without << " fireflies";

            const auto meanOf = [&](bool ring) {
                run(ring, settled, 5000);
                double sum = 0.0;
                for (std::size_t at = 1; at < read.size(); at += 4)
                    sum += static_cast<double>(read[at]);
                return sum / static_cast<double>(size * size);
            };
            const double unheld = meanOf(false);
            EXPECT_NEAR(meanOf(true) / unheld, 1.0, 0.01) << "the ring kept a settled mean's light from it";
        }

        /// **A fresh pixel is filtered the same under any light** (`shortHistoryVariance`).
        ///
        /// A floor under a sky dark at the horizon and bright overhead, from a cut, at the sky's light and at 1024
        /// times it: a power of two, so every sum, mean and packed colour of the brighter run is the dimmer one's
        /// scaled exactly, the same draws in both. A filter whose weights hang on the light's ratios alone leaves the
        /// brighter picture 1024 times the dimmer one, pixel for pixel; one that reads a fixed variance does not. Taken
        /// two frames on, where the history fix rebuilds the first level and the later ones read the variance, and
        /// four, where every level reads it.
        ///
        /// Measured: the brighter picture stands 1.1 hundred-thousandths from the dimmer one scaled two
        /// frames on, and 0.35 four frames on, which the brightness test's divide guard accounts for.
        /// With the constant variance this replaced, 3.2% and 6.9%.
        TEST_F(RtxVisibilityTest, aFreshPixelIsFilteredTheSameUnderAnyLight)
        {
            constexpr std::uint32_t size = 64;
            constexpr float brighter = 1024.0f;

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -200.0f, 150.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mAmbientFromSky = 1.0f;

            const auto run = [&](float scale, std::uint32_t frames, bool filter) {
                camera.mSkyHorizon = osg::Vec3f(0.02f, 0.02f, 0.02f) * scale;
                camera.mSkyZenith = osg::Vec3f(0.4f, 0.4f, 0.4f) * scale;
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    camera.mFrame = 2000 + at;
                    mRenderer.renderFrame(camera,
                        FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mReconstruction = ReconstructionRequest{ .mDenoise = filter,
                                .mBounceReuse = BounceReuse::Off,
                                .mAntilag = false,
                                .mAntiFirefly = false },
                            .mExposure = FixedExposure{ 1.0f } });
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                }
                std::vector<float> read;
                mRenderer.readComposite(read);
                return read;
            };

            // The green channel's spread over its mean, which says how much noise a picture holds.
            const auto spreadOf = [&](const std::vector<float>& picture) {
                double sum = 0.0;
                double squares = 0.0;
                for (std::size_t at = 1; at < picture.size(); at += 4)
                {
                    sum += static_cast<double>(picture[at]);
                    squares += static_cast<double>(picture[at]) * static_cast<double>(picture[at]);
                }
                const double mean = sum / static_cast<double>(size * size);
                return std::sqrt(std::max(squares / static_cast<double>(size * size) - mean * mean, 0.0)) / mean;
            };

            for (const std::uint32_t frames : { 2u, 4u })
            {
                const std::vector<float> dim = run(1.0f, frames, true);
                const std::vector<float> bright = run(brighter, frames, true);
                ASSERT_LT(spreadOf(dim), 0.5 * spreadOf(run(1.0f, frames, false)))
                    << "the filter took little noise off " << frames << " frames, so this proves nothing";

                double off = 0.0;
                double sum = 0.0;
                for (std::size_t at = 1; at < dim.size(); at += 4)
                {
                    off += std::abs(
                        static_cast<double>(bright[at]) / static_cast<double>(brighter) - static_cast<double>(dim[at]));
                    sum += static_cast<double>(dim[at]);
                }
                EXPECT_LT(off / sum, 1e-3) << frames << " frames from a cut are filtered by the light's own level";
            }
        }

        /// What the history is worth where the cascade has nothing to borrow from.
        ///
        /// **The grazing sheet above is the wavelet's best case and cannot answer this.** Every pixel
        /// of it looks at one flat surface under one smooth sky, so every pixel has the *same*
        /// expected bounce — a hundred and twenty-five taps average a hundred and twenty-five draws
        /// from one distribution, and the error falls by the square root of that. It converges to
        /// within a third of a byte on a single frame, and a history cannot improve on nothing left
        /// to remove.
        ///
        /// So this is the other case, and it is the one Morrowind's geometry actually is: a surface
        /// whose neighbours disagree. The sheet is cut into a grid of coplanar cells whose
        /// *shading* normals alternate by forty degrees, which is far outside what
        /// `ATROUS_NORMAL_POWER` lets a tap carry — so the plane test passes everywhere, the normal
        /// test rejects nearly every neighbour, and the cascade is left with little more than the
        /// centre pixel. Nothing about the accumulator changes: a still camera reprojects every
        /// pixel onto itself.
        ///
        /// The alternating tilt is not a trick to defeat the filter. It is what a bumpy surface is,
        /// and the reason the two neighbours may not be averaged is that they are genuinely lit
        /// differently — a cosine lobe tilted forty degrees samples a different part of this sky.
        TEST_F(RtxVisibilityTest, theHistoryCarriesWhereTheCascadeHasNoNeighboursToBorrow)
        {
            constexpr std::uint32_t size = 64;
            constexpr int cells = 16;
            constexpr float extent = 4000.0f;
            constexpr float tilt = 20.0f;

            std::vector<osg::Vec3f> positions;
            std::vector<osg::Vec3f> normals;
            std::vector<std::uint32_t> indices;
            for (int y = 0; y < cells; ++y)
                for (int x = 0; x < cells; ++x)
                {
                    const float lowX = -extent + 2.0f * extent * static_cast<float>(x) / cells;
                    const float highX = -extent + 2.0f * extent * static_cast<float>(x + 1) / cells;
                    const float lowY = -extent + 2.0f * extent * static_cast<float>(y) / cells;
                    const float highY = -extent + 2.0f * extent * static_cast<float>(y + 1) / cells;

                    const auto base = static_cast<std::uint32_t>(positions.size());
                    positions.emplace_back(lowX, lowY, 0.0f);
                    positions.emplace_back(highX, lowY, 0.0f);
                    positions.emplace_back(highX, highY, 0.0f);
                    positions.emplace_back(lowX, highY, 0.0f);

                    // Coplanar, so nothing here is a step in the geometry; only the normal moves.
                    const float lean = osg::DegreesToRadians((x + y) % 2 == 0 ? tilt : -tilt);
                    const osg::Vec3f leaning(std::sin(lean), 0.0f, std::cos(lean));
                    for (int corner = 0; corner < 4; ++corner)
                        normals.push_back(leaning);

                    for (const std::uint32_t offset : sQuadIndices)
                        indices.push_back(base + offset);
                }

            SceneDesc scene;
            scene.addInstance(MeshInstance{ .mMesh
                = scene.addMesh(MeshArrays{ .mPositions = positions, .mNormals = normals, .mIndices = indices }) });

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -2600.0f, 2600.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);

            // A sky that changes a great deal between the two tilts, so that two neighbouring cells
            // really do have different answers and averaging them really is wrong.
            camera.mSkyHorizon = osg::Vec3f(0.90f, 0.10f, 0.05f);
            camera.mSkyZenith = osg::Vec3f(0.05f, 0.20f, 0.90f);
            camera.mAmbientFromSky = 1.0f;

            const auto renderSequence = [&](std::uint32_t frames) {
                std::vector<float> values;
                values = shoot(scene, {}, camera, size, filteredRun(frames)).mRadiance;
                return values;
            };

            // Unfiltered, because a converged reference has to be the answer and not the filter's
            // opinion of it.
            std::vector<float> reference;
            reference = shoot(scene, {}, camera, size, { .mFrames = 128 }).mRadiance;

            const auto errorAgainstReference = [&](const std::vector<float>& values) {
                double squares = 0.0;
                std::size_t counted = 0;
                for (std::size_t i = 0; i < values.size(); i += 4)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const std::size_t at = i + channel;
                        // Only where the grid is: the sky around it is not being filtered.
                        if (values[at] == reference[at] && reference[at] == 0.0f)
                            continue;

                        const double error = static_cast<double>(values[at]) - static_cast<double>(reference[at]);
                        squares += error * error;
                        ++counted;
                    }

                return std::sqrt(squares / static_cast<double>(counted));
            };

            const std::vector<float> settledPixels = renderSequence(Shaders::ACCUMULATE_FRAMES);
            const double settled = errorAgainstReference(settledPixels);

            // **The cascade alone over the same sixteen draws the history averaged, pooled as a root
            // mean square.** One frame's error swings by four per cent with the draw it got, so a
            // single frame under this ratio put it at the mercy of the sampler's stream: anything that
            // reshuffled the stream moved the figure by a fiftieth, and a bound a fiftieth above it
            // failed on a change that touched neither the cascade nor the history. Sixteen pooled
            // swing by one per cent.
            double pooled = 0.0;
            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
            {
                std::vector<float> one;
                one = shoot(scene, {}, camera, size, filteredRun(1, frame)).mRadiance;
                const double error = errorAgainstReference(one);
                pooled += error * error / static_cast<double>(Shaders::ACCUMULATE_FRAMES);
            }
            const double alone = std::sqrt(pooled);

            // Measured on this box through the composite (`readRadiance`): the cascade alone leaves
            // 0.0164 pooled over sixteen frames, and the same sixteen accumulated leave 0.0050 — the
            // history removes seven tenths of the error the filter cannot reach. Deterministic to the
            // last digit for one stream, and a different stream is what any change to the sampler or
            // the scene hands this test: over ten streams, with an earlier cascade, the ratio spread
            // by 0.011 about its mean, so the bound below stands far over the 0.31 measured.
            EXPECT_GT(alone, 0.003) << "the cascade alone leaves enough error here for the question to mean something: "
                                    << alone;
            EXPECT_LT(settled, alone * 0.65) << "and a history of " << Shaders::ACCUMULATE_FRAMES
                                             << " frames takes over a third of what the cascade "
                                             << "cannot: " << alone << " becomes " << settled;

            // **And it converges on the reference rather than on its own opinion.** Quieter is not
            // the claim — an average that drifted would be quieter too, and wrong.
            double settledMean = 0.0;
            double referenceMean = 0.0;
            std::size_t counted = 0;
            for (std::size_t i = 0; i < settledPixels.size(); i += 4)
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const std::size_t at = i + channel;
                    if (settledPixels[at] == reference[at] && reference[at] == 0.0f)
                        continue;

                    settledMean += static_cast<double>(settledPixels[at]);
                    referenceMean += static_cast<double>(reference[at]);
                    ++counted;
                }

            ASSERT_GT(counted, 0u);
            settledMean /= static_cast<double>(counted);
            referenceMean /= static_cast<double>(counted);

            EXPECT_NEAR(settledMean, referenceMean, referenceMean * 0.02)
                << "the accumulated mean is " << settledMean << " against a converged " << referenceMean;
        }
    }
}
