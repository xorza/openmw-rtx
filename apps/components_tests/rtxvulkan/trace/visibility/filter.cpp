#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/memorylimits.hpp>
#include <apps/components_tests/rtxvulkan/support/device/rendererprobe.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// The denoiser, measured against the estimator it is smoothing. One flat floor under an open sky, so
        /// every pixel has the same expected answer: the mean is fixed and the scatter is pure sampling noise.
        /// Both halves are asserted (noise gone, mean where it was), since a filter that dimmed the picture
        /// would pass a test that only looked at the noise.
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
            mProbe.readChannel(Channel::Indirect, bounce);
            ASSERT_GT(*std::ranges::max_element(bounce), 0.0f)
                << "a bounce of nothing adds nothing twice, and this would prove nothing";

            const Frame summed = shoot(scene, {}, camera, size, { .mFrames = 1 });
            EXPECT_EQ(summed.mRadiance, raw.mRadiance) << "the sum of one unfiltered frame is that frame";

            // **The cascade's compose and the composite's are one picture**: a filtered frame nothing
            // sums is composed in the wavelet's last level (`atrouscompose.comp`), and one summed by the
            // composite after it; frame nought of each, value for value.
            const Frame filteredSummed = shoot(scene, {}, camera, size, { .mFrames = 1, .mFilter = true });
            EXPECT_EQ(filteredSummed.mRadiance, filtered.mRadiance)
                << "the cascade composed another picture than the composite";
        }

        /// A lamp that goes out leaves the picture on the frame it goes. One lamp over a floor for 32 frames,
        /// then the same floor without it, placed and not handed over, so every history carries on. The lamp's
        /// light is the shadow denoiser's unshadowed sum times a filtered bit and only the bit has a history:
        /// the sum is nought on the first frame without the lamp, and so is the light. Under a black sky the
        /// pixel under the lamp is the unlit floor's exactly.
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

        /// The filter rebuilds an arm's pixels through the arms' own eye, as the trace cast them, and so smooths
        /// an arm as it smooths the floor. The arms are traced through a ninety-degree eye beside the world's
        /// thirty here; rebuilt through the world's, a flat arm stood on planes it is not, a tap sixteen pixels
        /// off the centre ten units off the centre's plane, and the coarse levels turned their neighbours away.
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
            camera.mEyes.mArms = cameraAtFieldOfView(camera.mEyes.mWorld, 90.0f).value();
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
            // Measured over the cascade's four levels: the arms' eye leaves 2.4 per cent of the
            // spread, and the world's 16.4 — its coarser levels turn their taps away. A bound between
            // the two.
            EXPECT_LT(filteredSpread, rawSpread * 0.045f) << "the arm's noise was not taken away";
        }

        /// The same floor at a grazing angle, against the answer it is trying to reach.
        ///
        /// Terrain is nearly always seen this way, and a depth test gets it wrong: pixels down a grazing surface
        /// stand a long way apart in distance on one flat plane, so refusing taps by distance keeps only the
        /// taps across the slope. Weighing by how far a tap sits off the centre pixel's tangent plane costs one
        /// dot product and asks the question that was meant. The reference is what `--accumulate` builds: 64
        /// differently seeded samples averaged.
        ///
        /// The bound sits between the two weightings on purpose. On the composite (`readRadiance`) one sample is
        /// 0.0335 off the reference and the plane weight brings that to 0.0020 (17 times better); a plain depth
        /// weight, from a sample 0.0420 off, brought it to 0.0061 (7 times). Frame zero and a 64-frame average
        /// are deterministic, so a tenth is a bound this passes with room and a depth test cannot reach.
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

            // The temporal half is where the error actually falls: the spatial cascade borrows from correlated
            // neighbours and has a floor, while averaging over frames borrows independent samples, so its error
            // keeps falling as long as the history grows. Sequenced frames with no averaging in the composite,
            // so the accumulator alone combines them, on a still camera so no history is rejected.
            //
            // A cut before each run: the fixture's accumulator keeps what it built across all frames, so a
            // "one frame" baseline taken without a reset already has a history in it and reads as the
            // accumulator doing nothing.
            const auto renderSequence = [&](std::uint32_t frames) {
                std::vector<float> values;
                values = shoot(scene, {}, camera, size, filteredRun(frames)).mRadiance;
                return values;
            };

            const std::vector<float> settledPixels = renderSequence(Shaders::ACCUMULATE_FRAMES);
            const float settled = errorAgainstReference(settledPixels);

            // The accumulator may not make this worse, which on this surface is the whole of what it can be
            // asked: the cascade alone already lands at 0.0019 of the converged reference, a flat sheet under a
            // smooth sky being where à-trous has every advantage. What the history is for is the case this scene
            // lacks, `theHistoryCarriesWhereTheCascadeHasNoNeighboursToBorrow` (0.0050 settled against 0.0165
            // alone).
            //
            // Eight per cent of room, because the history is the filtered light (SVGF's feedback) and averaging
            // the cascade's answers over frames on a surface this easy only correlates them. Measured: cascade
            // 0.00196, settled history 0.00177, unfiltered 0.0335.
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

        /// A floor meeting a wall, and the filter keeping them apart. Everything else here would pass with a
        /// plain blur; what makes this a denoiser is that it refuses to mix two surfaces that happen to be
        /// neighbours on screen.
        ///
        /// It measures the step from one row to the next across the crease, since away from it a blur is nearly
        /// harmless (every level of a B3 kernel puts most of its weight near the centre), which is why a test
        /// comparing the two ends of the frame passes with the guide off and this one does not. The crease is
        /// the row boundary where the unfiltered picture jumps hardest. The surfaces are told apart by their
        /// normals alone, and lit differently for the same reason: a floor's cosine-weighted hemisphere is
        /// centred on the zenith, a wall's lies along the horizon, and this sky runs a long way between them.
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
        /// Adaptation is a time-domain thing: a histogram measured on the frame the curve is about to map and
        /// applied to that same frame turns a one-frame excursion into one in the whole image. Two skies a
        /// factor of thirty-two apart and nothing else, so the histogram is the only thing that changed. Told
        /// it has no past, the eye holds a bright day's exposure (`EXPOSURE_DAY`); given a long frame, it
        /// arrives; told it has a past, it has barely moved a frame later; told no time passed, or held, it has
        /// not moved.
        ///
        /// Driven frame by frame rather than through `shoot`, which calls `setScene` every time and a new scene
        /// costs every history, the eye's too, which the middle frame must not lose.
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
                mProbe.traceFrame(
                    camera, FrameOptions{ .mSinceLast = since, .mLoss = loss, .mExposure = ExposureRule{} });
                mRenderer.readPixels(pixels);
                return meanByte();
            };

            // Where the eye arrives on `camera`: a frame with no past, which holds a day, and the same
            // frame again over a time it closes the whole gap in.
            const auto settle = [&](const Shaders::VisibilityConstants& camera) {
                shot(camera, 1.0f / 60.0f, HistoryLoss::Cut);
                return shot(camera, 1000.0f);
            };

            // Nothing to hit, so every pixel is the sky and the mean of the frame is the sky. The
            // scene is set once: setting it again would clear the previous camera and reset the eye.
            const SceneDesc sky;
            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), sky, {});

            // The dim sky at a day's exposure, which every frame with no past draws.
            const double dayDim = shot(dim);
            EXPECT_EQ(shot(dim, 1.0f / 60.0f, HistoryLoss::Cut), dayDim) << "a cut did not start the eye at a day";

            const double lit = settle(bright);
            ASSERT_GT(lit, 0.0) << "the bright sky rendered as black";

            // The same sky thirty-two times darker, one frame later and with a past to move from.
            // The eye has had a sixtieth of a second against a time constant of a second and a half,
            // so it has gone almost nowhere.
            const double justAfter = shot(dim);

            // No time at all after that, and the eye has had none to move in: a frame that stood for
            // nothing is no reset.
            EXPECT_EQ(shot(dim, 0.0f), justAfter) << "the eye moved in no time";

            // And where it is headed: the eye settled on the dim sky.
            const double adapted = settle(dim);
            EXPECT_GT(adapted, dayDim) << "the dim sky did not open the eye past a day";

            EXPECT_GT(adapted, 0.0) << "the dark sky rendered as black even with the eye open";
            EXPECT_LT(justAfter, 0.5 * adapted)
                << "the exposure arrived in one frame: " << justAfter << " against " << adapted;

            // **Held, the eye stays where the frame before left it, reset or not**: the dim sky told
            // it has no past draws exactly what the adapted frame drew, and the bright sky under the
            // dim eye comes out brighter than it did under its own.
            const auto held
                = [&](const Shaders::VisibilityConstants& camera, const HistoryLoss loss = HistoryLoss::None) {
                      mProbe.traceFrame(camera,
                          FrameOptions{ .mSinceLast = 1.0f / 60.0f, .mLoss = loss, .mExposure = HeldExposure{} });
                      mRenderer.readPixels(pixels);
                      return meanByte();
                  };
            EXPECT_EQ(held(dim, HistoryLoss::Cut), adapted) << "a held eye measured the frame";
            EXPECT_GT(held(bright), lit) << "a held eye adapted to the bright sky";

            // A new extent keeps the eye: an eye adapted to the bright sky meets the dim one past two upscale
            // modes barely moved, where an eye that lost its past would start again at a day. The modes' reserves
            // are released and built again in between, as the uploader does, under a budget a byte short of the
            // textures' ceiling at the heap, so whichever mode grows the reserve finds content past what it leaves.
            EXPECT_EQ(settle(bright), lit);
            {
                MemoryAllocator& memory = mProbe.getDevice().getMemory();
                const Testing::BudgetLimit tight(memory, Testing::budgetAbove(memory, MemoryUse::Texture, 0) - 1);
                mRenderer.setUpscale(Upscale::Quality);
                mRenderer.setUpscale(Upscale::Off);
            }
            ASSERT_EQ(mRenderer.describeHeld(Rtx::SceneSlot::world()).mIdentity, 0u)
                << "neither mode grew the reserve past what content left, so nothing here was built again";
            mRenderer.setScene(Rtx::SceneSlot::world(), sky, {});
            EXPECT_LT(shot(dim), 0.5 * adapted) << "a new extent snapped the eye";

            // **And a new world loses it, whatever frame comes first**: a held frame after the world
            // is handed over spends nothing of the eye's loss, and the measured frame after it starts
            // at a day, as a cut's frame does, rather than easing from the old world's eye.
            mRenderer.setScene(Rtx::SceneSlot::world(), SceneDesc{}, {});
            held(bright);
            EXPECT_EQ(shot(dim), dayDim) << "the measured frame eased from the old world's eye";
        }

        /// A reset survives a frame that has no history to reset, and such a frame leaves none.
        ///
        /// A cut is spent by the frame that answers it, and a frame with neither denoiser answers nothing: a
        /// frame the renderer does not filter (`LightFilter`) runs no accumulator and `Upscale::Off` runs no
        /// upscaler, so a renderer that cleared the signal at the end of every frame would drop the reset
        /// rather than defer it, and a door walked through on an unfiltered frame would reproject one room
        /// onto another on the next filtered one.
        ///
        /// The claim is exact: a reset accumulator writes the frame's own sample and reads no past, so the
        /// picture equals the same trace from a fresh reset, value for value (the frame index seeds the
        /// sampler and the camera never moves). The accumulated frame between them proves the comparison can
        /// tell a history from none. And the reset's own sample is a history of one, so a filtered frame after
        /// it is not a fresh reset's picture: the same claim the other way round.
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

            const auto renderOne = [&](std::uint32_t frame, bool filter, HistoryLoss loss = HistoryLoss::None) {
                Shaders::VisibilityConstants sampled = camera;
                sampled.mFrame = frame;
                mProbe.traceFrame(sampled,
                    FrameOptions{ .mAccumulate = 0,
                        .mLoss = loss,
                        .mReconstruction = ReconstructionRequest{ .mDenoise = filter },
                        .mExposure = FixedExposure{ 1.0f } });
            };

            const auto radiance = [&] {
                std::vector<float> values;
                mProbe.readComposite(values);
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

        /// Stripes of shading normals a texel wide, tilted ten degrees one way and the other about the
        /// tangent space's `y`: across a stripe's edge two normals stand twenty degrees apart.
        TestTexture paintStripes()
        {
            constexpr std::uint32_t extent = 512;
            const float tilt = std::sin(osg::DegreesToRadians(10.0f));
            const auto encoded
                = [](float value) { return static_cast<std::uint8_t>(std::lround((value * 0.5f + 0.5f) * 255.0f)); };
            std::vector<std::uint8_t> texels(std::size_t{ extent } * extent * 4, 255);
            for (std::uint32_t y = 0; y < extent; ++y)
                for (std::uint32_t x = 0; x < extent; ++x)
                {
                    const std::size_t at = (std::size_t{ y } * extent + x) * 4;
                    texels[at] = encoded(x % 2 == 0 ? tilt : -tilt);
                    texels[at + 1] = encoded(0.0f);
                    texels[at + 2] = encoded(std::sqrt(1.0f - tilt * tilt));
                }
            TestTexture painted;
            paintFlat(painted, extent, texels, "stripes");
            return painted;
        }

        /// How a run of `RtxHistoryFixTest` is read.
        struct StripReading
        {
            /// Whether the clamp runs, which every run but the fast mean's leaves off.
            bool mAntilag = false;

            /// How many frames past the turn the strip is read: nought, the turn itself.
            std::uint32_t mAfter = 0;

            /// How many draws the spread is taken over.
            std::uint32_t mDraws = 4;
        };

        /// The floor and the wall of `theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo`, the eye that
        /// turns over them, and what a run of it reads off the strip it turns to. **A test traces only
        /// the runs it reads**: a frame under the layers is milliseconds of the host's, and a run
        /// is four draws of thirty-three of them.
        class RtxHistoryFixTest : public RtxVisibilityTest
        {
        protected:
            static constexpr std::uint32_t sSize = 64;
            static constexpr std::uint32_t sStill = 32;
            static constexpr std::uint32_t sStrip = 12;
            static constexpr std::uint32_t sInset = 14;
            static inline const float sTurn = osg::DegreesToRadians(24.0f);

            /// The strip held still, which only says the turn made it noisier, over six times where one
            /// and a half is asked: two draws read it as four do, 0.2115 of its mean against 0.2119.
            static constexpr StripReading sHeldStill{ .mDraws = 2 };

            /// Each column's green channel at the last frame of a run of `sStill` frames standing at
            /// one yaw and the rest at another: its spread over the draws, as a variance summed over
            /// the rows, and its sum over the rows and the draws.
            struct Columns
            {
                std::vector<double> mVariances;
                std::vector<double> mSums;
                std::uint32_t mDraws = 0;
            };

            /// Hands the floor and the wall over, and the lamp that lights a spot of the wall.
            ///
            /// @param bumped whether the floor and the wall wear `paintStripes` as their normal map.
            void stage(bool bumped)
            {
                if (bumped)
                {
                    mStripes = paintStripes();
                    mStripes.mData.mSlot = 0;
                    mTextures.push_back(mStripes.mData);
                    const Index normalMap = mScene.textures().add(
                        VFS::Path::NormalizedView("stripes_n.dds"), TextureWrap::Repeat, TextureEncoding::Normal);
                    const Index material = mScene.addMaterial(Material{ .mNormal = normalMap });
                    const auto bumpy = [&](const std::array<osg::Vec3f, 4>& quad, const osg::Vec3f& normal) {
                        const std::array normals{ normal, normal, normal, normal };
                        const osg::Vec4f tangent(1.0f, 0.0f, 0.0f, 1.0f);
                        const std::array tangents{ tangent, tangent, tangent, tangent };
                        mScene.addInstance(MeshInstance{ .mMesh = mScene.addMesh(MeshArrays{ .mPositions = quad,
                                                             .mNormals = normals,
                                                             .mTexCoords = sQuadUv,
                                                             .mTangents = tangents,
                                                             .mIndices = sQuadIndices }),
                            .mMaterial = material });
                    };
                    bumpy(sheetAt(4000.0f, -100.0f), osg::Vec3f(0.0f, 0.0f, 1.0f));
                    bumpy(uprightQuadAt(4000.0f, 300.0f), osg::Vec3f(0.0f, -1.0f, 0.0f));
                }
                else
                {
                    addQuad(mScene, sheetAt(4000.0f, -100.0f));
                    addQuad(mScene, uprightQuadAt(4000.0f, 300.0f));
                }
                mScene.addLight(Light{
                    .mPosition = osg::Vec3f(0.0f, 280.0f, -40.0f),
                    .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                    .mReach = 150.0f,
                });
                prepare(mScene, mTextures, sSize);
            }

            /// The eye at `yaw`, drawing the sampler's `frame`, under a black sky.
            static Shaders::VisibilityConstants standing(float yaw, std::uint32_t frame)
            {
                const osg::Vec3f eye(0.0f, -200.0f, 50.0f);
                const osg::Vec3f ahead(0.0f, 500.0f, -150.0f);
                const osg::Vec3f looking(ahead.x() * std::cos(yaw) - ahead.y() * std::sin(yaw),
                    ahead.x() * std::sin(yaw) + ahead.y() * std::cos(yaw), ahead.z());
                Shaders::VisibilityConstants camera
                    = Testing::makeCamera(eye, eye + looking, 60.0f, sSize, sSize, 10000.0f);
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mSkyHorizon = osg::Vec3f();
                camera.mSkyZenith = osg::Vec3f();
                camera.mFrame = frame;
                return camera;
            }

            /// The columns of the reading's draws, each standing at `from` and then at `to`, the
            /// history fix on where `fix` says.
            Columns columnsOf(float from, float to, bool fix, const StripReading& reading = {})
            {
                std::ranges::fill(mSums, 0.0);
                std::ranges::fill(mSquares, 0.0);
                const std::uint32_t draws = reading.mDraws;
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    for (std::uint32_t at = 0; at <= sStill + reading.mAfter; ++at)
                    {
                        mProbe.traceFrame(standing(at >= sStill ? to : from, 1000 + 100 * draw + at),
                            FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                                .mReconstruction = ReconstructionRequest{ .mDenoise = true,
                                    .mFilters
                                    = { .mAntilag = reading.mAntilag, .mHistoryFix = fix, .mAntiFirefly = false } },
                                .mExposure = FixedExposure{ 1.0f } });

                        // **Two frames in flight**, for the reason `RtxBounceTrailTest` gives: the
                        // layers' work on one frame stands beside the device's on the one before.
                        mRenderer.collectFrame();
                    }
                    while (mRenderer.finishFrame().has_value())
                    {
                    }
                    mProbe.readComposite(mRead);
                    for (std::size_t pixel = 0; pixel < mSums.size(); ++pixel)
                    {
                        const double value = static_cast<double>(mRead[pixel * 4 + 1]);
                        mSums[pixel] += value;
                        mSquares[pixel] += value * value;
                    }
                }
                Columns columns{ std::vector<double>(sSize, 0.0), std::vector<double>(sSize, 0.0), draws };
                for (std::uint32_t y = 0; y < sSize; ++y)
                    for (std::uint32_t x = 0; x < sSize; ++x)
                    {
                        const std::size_t pixel = std::size_t{ y } * sSize + x;
                        const double mean = mSums[pixel] / draws;
                        columns.mVariances[x] += (mSquares[pixel] - draws * mean * mean) / (draws - 1);
                        columns.mSums[x] += mSums[pixel];
                    }
                return columns;
            }

            /// The strip's noise as a deviation over its mean: a strip the brightness test darkened is
            /// quieter by as much as it is darker, and no less noisy for its light.
            static double noiseOf(const Columns& columns)
            {
                const auto overStrip = [](const std::vector<double>& column, std::uint32_t per) {
                    double sum = 0.0;
                    for (std::uint32_t x = sInset; x < sInset + sStrip; ++x)
                        sum += column[x];
                    return sum / static_cast<double>(sStrip * sSize * per);
                };
                return std::sqrt(overStrip(columns.mVariances, 1)) / overStrip(columns.mSums, columns.mDraws);
            }

            /// The scene `stage` handed over, and its textures, for a shot of it.
            const SceneDesc& getScene() const { return mScene; }
            std::span<const TextureData> getTextures() const { return mTextures; }

        private:
            SceneDesc mScene;
            TestTexture mStripes;
            std::vector<TextureData> mTextures;
            std::vector<float> mRead;
            std::vector<double> mSums = std::vector<double>(std::size_t{ sSize } * sSize);
            std::vector<double> mSquares = std::vector<double>(std::size_t{ sSize } * sSize);
        };

        /// The history fix takes the noise off what the eye turns to. A floor and a wall under a black sky and
        /// a lamp twenty units off the wall whose reach lights a spot of it: the floor's bounce is that spot,
        /// which a ray finds now and then, so one frame is noisy after the wavelet. The eye stands still for 32
        /// frames, then turns 24 degrees in one: the edge it turns toward brings in
        /// `(tan 30° - tan 6°) / (tan 30° / 32)` = 26 columns holding one frame. The strip is twelve of them
        /// fourteen columns in from that edge, as far as a new pixel's fix reaches, so its taps stand on both
        /// sides of it.
        ///
        /// Over four draws, the strip's spread from one draw to the next over its mean: the noise in the
        /// light's own units, since without the fix the brightness test passes over a fresh pixel's rare bright
        /// draws and a strip darker for it is quieter by as much and no better. The clamp is off in every run
        /// (its box of fifty samples mostly holds none of a light this rare, `ACCUMULATE_FAST_FRAMES`'s trade),
        /// and so is the ring (`ACCUMULATE_RING_FRAMES`'s).
        ///
        /// Measured: noise 1.33 of the mean without the fix, 0.57 with it, 0.21 held still. The strip's light
        /// is not held to its truth here: over 32 draws against 1024 frames it reads 1.00 held still, 0.84
        /// without the fix and 1.22 with it, and four draws and 128 frames place it anywhere within a fifth of
        /// that. The fix's excess is its flat kernel's, ReLAX's (`RELAX_HistoryFix`), which takes the mean of
        /// fourteen pixels either side of light that rises toward the spot faster than in a line.
        TEST_F(RtxHistoryFixTest, theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo)
        {
            stage(false);
            const double held = noiseOf(columnsOf(sTurn, sTurn, false, sHeldStill));
            // A yaw toward -x turns the eye left, so the new columns are the picture's first.
            const double without = noiseOf(columnsOf(0.0f, sTurn, false));
            const double with = noiseOf(columnsOf(0.0f, sTurn, true));
            ASSERT_GT(without, 1.5 * held)
                << "the strip the eye turned to is no noisier than the same strip held still, so this proves nothing: "
                << without << " against " << held;
            EXPECT_LT(with, 0.5 * without) << "the history fix left " << with << " of the strip's " << without
                                           << ", where the strip held still holds " << held;
        }

        /// A settled history keeps its light under the brightness test (`ATROUS_LUMINANCE_SIGMA`). The scene of
        /// `theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo`, the whole frame held still, against 128 unfiltered
        /// frames where the eye ends, which hold the frame's light to two thousandths where they hold a strip's
        /// to a fifth. Measured: the frame keeps 0.993 of its light.
        TEST_F(RtxHistoryFixTest, aSettledHistoryKeepsItsLightUnderTheBrightnessTest)
        {
            stage(false);
            const std::vector<float> reference
                = shoot(getScene(), getTextures(), standing(sTurn, 0), sSize, { .mFrames = 128, .mSetScene = false })
                      .mRadiance;
            const Columns held = columnsOf(sTurn, sTurn, false);

            double heldLight = 0.0;
            for (const double column : held.mSums)
                heldLight += column;
            double truthLight = 0.0;
            for (std::size_t pixel = 0; pixel < std::size_t{ sSize } * sSize; ++pixel)
                truthLight += static_cast<double>(reference[pixel * 4 + 1]);
            const double kept = heldLight / held.mDraws / truthLight;
            EXPECT_GT(kept, 0.985) << "the brightness test took light from a settled history: " << kept;
        }

        /// The history fix finds its neighbours on a bumpy surface (`FacingWeights::mFix`). The floor and the
        /// wall above wear stripes of shading normals two pixels wide, ten degrees one way and the other: most
        /// of a fixed pixel's taps stand on a stripe twenty degrees off it. NRD's power of eight weighs such a
        /// tap at 0.6, and the wavelet's 128 at 0.0003. Measured: noise 2.25 of the mean without the fix, 1.21
        /// with it.
        TEST_F(RtxHistoryFixTest, theHistoryFixFindsItsNeighboursOnABumpySurface)
        {
            stage(true);
            const double held = noiseOf(columnsOf(sTurn, sTurn, false, sHeldStill));
            const double without = noiseOf(columnsOf(0.0f, sTurn, false));
            const double with = noiseOf(columnsOf(0.0f, sTurn, true));
            ASSERT_GT(without, 1.5 * held) << "the strip the eye turned to is no noisier, so this proves nothing";
            EXPECT_LT(with, 0.6 * without) << "the history fix left " << with << " of the strip's " << without;
        }

        /// The pixels the history fix rebuilt keep the rebuilt light under the clamp. The turn of
        /// `theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo` with the clamp on, the strip read two frames after
        /// it. A fixed pixel's fast mean is its one raw sample, and the next frame's clamp would hold the
        /// rebuilt slow mean to a box grown by that sample and pull the noise back in, so the fix's answer goes
        /// into the fast mean too, as ReLAX's clamp leaves its two histories one light. Measured: noise 0.921 of
        /// the mean without the fix, 0.653 with it; 0.737 with the fast mean left raw.
        TEST_F(RtxHistoryFixTest, aFixedPixelsFastMeanStartsFromTheFixedLight)
        {
            stage(false);
            const StripReading reading{ .mAntilag = true, .mAfter = 2 };
            const double without = noiseOf(columnsOf(0.0f, sTurn, false, reading));
            const double with = noiseOf(columnsOf(0.0f, sTurn, true, reading));
            EXPECT_LT(with, 0.74 * without) << "the history fix left " << with << " of the strip's " << without;
        }

        /// The floor a moving bar uncovers starts with the history beside it (`occluderMotion`, Zeng et al.
        /// 2021's dual motion vector).
        ///
        /// The eye 300 units over a floor under a sky dark at the horizon and bright overhead, and a bar 40
        /// units wide spanning every row at 200, moving 10 units a frame across the columns after 32 still
        /// frames: on the screen the bar is a third as far as the floor, so it steps about five and a half
        /// columns a frame and every frame uncovers a strip the frame before did not see. Over four draws, the
        /// last strip's noise from one draw to the next over its mean: without the dual vector the strip holds
        /// one frame, which the history fix rebuilds from the floor around it; with it, the history of the
        /// floor the bar uncovered before.
        ///
        /// Each of those taps is another point of the floor, five pixels on and 3% further from the eye: a
        /// depth test refused every one, and the plane test takes them (`heldSurfaceMatches`). The table is
        /// advanced after each hand-over, as `SceneUploader` does, or the bar's motion is its whole travel
        /// since it stood still. Measured: 0.0097 without the dual vector, 0.0022 with it, 0.0019 beside a bar
        /// that stood still.
        TEST_F(RtxVisibilityTest, theFloorAMovingBarUncoversStartsWithTheHistoryBesideIt)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t still = 32;
            constexpr std::uint32_t moving = 4;
            constexpr std::uint32_t draws = 4;
            constexpr float step = 10.0f;
            constexpr float width = 40.0f;
            constexpr float start = -30.0f;
            constexpr float height = 200.0f;
            constexpr float eye = 300.0f;

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            const Index bar = addQuad(scene, roofOver(start, start + width, height));
            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, eye), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f(0.02f, 0.02f, 0.02f);
            camera.mSkyZenith = osg::Vec3f(0.6f, 0.6f, 0.6f);
            camera.mAmbientFromSky = 1.0f;

            // Whether the floor under `column` is under the bar placed `moved` along: where the
            // column's ray crosses the bar's height, a third of the way down, `(eye - height) / eye`
            // of the floor's offset. A column within one of either edge counts as neither.
            const float halfWidth = eye * std::tan(osg::DegreesToRadians(30.0f));
            const auto crossing = [&](std::uint32_t column) {
                const float floor = (static_cast<float>(column) + 0.5f - 0.5f * size) / (0.5f * size) * halfWidth;
                return floor * (eye - height) / eye;
            };
            const float pixelAtBar = halfWidth * (eye - height) / eye / (0.5f * size);
            const auto under = [&](std::uint32_t column, float moved, float margin) {
                const float x = crossing(column);
                return x >= start + moved - margin && x <= start + width + moved + margin;
            };

            std::vector<float> read;
            std::vector<double> sums(std::size_t{ size } * size);
            std::vector<double> squares(std::size_t{ size } * size);
            const auto run = [&](bool dual) {
                std::ranges::fill(sums, 0.0);
                std::ranges::fill(squares, 0.0);
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    for (std::uint32_t at = 0; at < still + moving; ++at)
                    {
                        const float moved = at < still ? 0.0f : step * static_cast<float>(at + 1 - still);
                        // Advanced after each hand-over, as `SceneUploader` does, or the bar's motion
                        // is every step since it stood still.
                        scene.placements().move(bar, osg::Matrixf::translate(moved, 0.0f, 0.0f));
                        mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                        scene.placements().advance();
                        camera.mFrame = 3000 + 100 * draw + at;
                        mProbe.traceFrame(camera,
                            FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                                .mReconstruction = ReconstructionRequest{ .mDenoise = true,
                                    .mFilters = { .mAntilag = false, .mDualMotion = dual, .mAntiFirefly = false } },
                                .mExposure = FixedExposure{ 1.0f } });
                        // A frame behind, as `shoot` collects them, and the last before its picture is read.
                        if (at > 0)
                            EXPECT_TRUE(mRenderer.finishFrame().has_value());
                    }
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                    mProbe.readComposite(read);
                    for (std::size_t pixel = 0; pixel < sums.size(); ++pixel)
                    {
                        const double value = static_cast<double>(read[pixel * 4 + 1]);
                        sums[pixel] += value;
                        squares[pixel] += value * value;
                    }
                }

                // The strip: under the bar the frame before the last and clear of it in the last.
                const float last = step * static_cast<float>(moving);
                double spread = 0.0;
                double mean = 0.0;
                std::uint32_t counted = 0;
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    if (!under(x, last - step, -pixelAtBar) || under(x, last, pixelAtBar))
                        continue;
                    for (std::uint32_t y = 0; y < size; ++y)
                    {
                        const std::size_t pixel = std::size_t{ y } * size + x;
                        const double average = sums[pixel] / draws;
                        spread += (squares[pixel] - draws * average * average) / (draws - 1);
                        mean += average;
                        ++counted;
                    }
                }
                EXPECT_GT(counted, 3u * size) << "the last frame uncovered too little floor to measure";
                return std::sqrt(spread / counted) / (mean / counted);
            };

            const double without = run(false);
            const double with = run(true);
            EXPECT_LT(with, 0.5 * without)
                << "the dual vector left " << with << " of the strip's noise, against " << without << " without it";
        }

        /// A floor under an open sky, and 300 units over it a sheet 40 units square glowing at `50 ×
        /// EMISSIVE_INTENSITY`, 400: a bounce off the floor beneath finds it at a chance of about
        /// `40² / 300² / π`, 0.0057, and brings back near two hundred times the floor's mean — the
        /// lanterns' paper over the M[FR] guild's tree, on a surface that holds still.
        SceneDesc lanternOverAFloor()
        {
            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, sheetAt(20.0f, 300.0f),
                scene.addMaterial(Material{ .mEmissiveColour = osg::Vec3f(50.0f, 50.0f, 50.0f), .mTwoSided = true }));
            return scene;
        }

        Shaders::VisibilityConstants overTheLantern(std::uint32_t size)
        {
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -200.0f, 150.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f(0.2f, 0.2f, 0.2f);
            camera.mSkyZenith = osg::Vec3f(0.2f, 0.2f, 0.2f);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mAmbientFromSky = 1.0f;
            return camera;
        }

        /// The composite of `renderer` after `frames` filtered frames from a cut, each its own draw from
        /// `first` on, with the ring or without it.
        void drawFromACut(VulkanRenderer& renderer, Shaders::VisibilityConstants camera, bool ring,
            std::uint32_t frames, std::uint32_t first, std::vector<float>& read)
        {
            RendererProbe probe(renderer);
            for (std::uint32_t at = 0; at < frames; ++at)
            {
                camera.mFrame = first + at;
                probe.traceFrame(camera,
                    FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                        .mReconstruction = ReconstructionRequest{ .mDenoise = true,
                            .mFilters = { .mAntilag = false, .mAntiFirefly = ring } },
                        .mExposure = FixedExposure{ 1.0f } });
                EXPECT_TRUE(renderer.finishFrame().has_value());
            }
            probe.readComposite(read);
        }

        /// A rare bright bounce on a history of a few frames is held to the light around it
        /// (`ACCUMULATE_RING_FRAMES`), over the lantern of `lanternOverAFloor`.
        ///
        /// After a cut every history is fresh, and three frames on a pixel whose bounce found the sheet stands
        /// far over the truth: counted where the green stands four times over 64 unfiltered frames', pooled over
        /// four draws, the ring leaves a tenth of them at most. Measured: 9319 fireflies without the ring and
        /// 173 with it; 9537 and 40 on a low-end AMD integrated GPU.
        TEST_F(RtxVisibilityTest, theRingHoldsAFreshFireflyToTheLightAroundIt)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t fresh = 3;
            constexpr std::uint32_t draws = 4;

            const SceneDesc scene = lanternOverAFloor();
            const Shaders::VisibilityConstants camera = overTheLantern(size);
            const std::vector<float> reference = shoot(scene, {}, camera, size, { .mFrames = 64 }).mRadiance;

            std::vector<float> read;
            const auto firefliesOf = [&](bool ring) {
                std::uint32_t count = 0;
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    drawFromACut(mRenderer, camera, ring, fresh, 1000 + 100 * draw, read);
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
        }

        /// A settled history keeps the light the ring held from its first frames, over the lantern of
        /// `lanternOverAFloor`: 160 still frames from a cut, whose means with and without the ring stand within
        /// a hundredth. The first eight frames, a quarter of a mean of 32, are `(31/32)^128` of that quarter 128
        /// frames later, under half a hundredth of the mean however much of their light the ring took.
        ///
        /// On the unvalidated renderer (`getUnvalidatedRenderer`), because the claim needs all of its 320
        /// frames (at 112 the means stood 2.6% apart on a low-end AMD integrated GPU) and the layers hold each
        /// at three times its cost, past what a test may take; the ring's passes are validated by
        /// `theRingHoldsAFreshFireflyToTheLightAroundIt`. Measured: settled means 0.7973 and 0.7928.
        TEST_F(RtxVisibilityTest, theRingLeavesASettledMeanItsLight)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t settled = 160;

            VulkanRenderer& renderer = getUnvalidatedRenderer();
            renderer.resize(size, size);
            renderer.setScene(Rtx::SceneSlot::world(), lanternOverAFloor(), {});
            const Shaders::VisibilityConstants camera = overTheLantern(size);

            std::vector<float> read;
            const auto meanOf = [&](bool ring) {
                drawFromACut(renderer, camera, ring, settled, 5000, read);
                double sum = 0.0;
                for (std::size_t at = 1; at < read.size(); at += 4)
                    sum += static_cast<double>(read[at]);
                return sum / static_cast<double>(size * size);
            };
            const double unheld = meanOf(false);
            EXPECT_NEAR(meanOf(true) / unheld, 1.0, 0.01) << "the ring kept a settled mean's light from it";
        }

        /// A fresh pixel is filtered the same under any light (`shortHistoryVariance`).
        ///
        /// A floor under a sky dark at the horizon and bright overhead, from a cut, at the sky's light and at
        /// 1024 times it: a power of two, so every sum, mean and packed colour of the brighter run is the
        /// dimmer one's scaled exactly. A filter whose weights hang on the light's ratios alone leaves the
        /// brighter picture 1024 times the dimmer one, pixel for pixel; one that reads a fixed variance does
        /// not. Taken two frames on, where the history fix rebuilds the first level and the later ones read the
        /// variance, and four, where every level reads it. Measured: 0.39 and 0.053 hundred-thousandths apart,
        /// which the brightness test's divide guard accounts for.
        ///
        /// At a sixteenth and a sixty-fourth of the light the variance the narrow levels carry falls under what
        /// a half holds, and the deviation they carry instead (`atrous.comp`) keeps the dimmer picture within
        /// 0.79 and 2.0 ten-thousandths of the brighter one scaled. Dimmer still, the divide guard decides
        /// either way.
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
                    mProbe.traceFrame(camera,
                        FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mReconstruction = ReconstructionRequest{ .mDenoise = filter,
                                .mFilters = { .mAntilag = false, .mAntiFirefly = false } },
                            .mExposure = FixedExposure{ 1.0f } });
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                }
                std::vector<float> read;
                mProbe.readComposite(read);
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

            for (const float level : { 1.0f, 1.0f / 16.0f, 1.0f / 64.0f })
                for (const std::uint32_t frames : { 2u, 4u })
                {
                    const std::vector<float> dim = run(level, frames, true);
                    const std::vector<float> bright = run(level * brighter, frames, true);
                    ASSERT_LT(spreadOf(dim), 0.5 * spreadOf(run(level, frames, false)))
                        << "the filter took little noise off " << frames << " frames, so this proves nothing";

                    double off = 0.0;
                    double sum = 0.0;
                    for (std::size_t at = 1; at < dim.size(); at += 4)
                    {
                        off += std::abs(static_cast<double>(bright[at]) / static_cast<double>(brighter)
                            - static_cast<double>(dim[at]));
                        sum += static_cast<double>(dim[at]);
                    }
                    EXPECT_LT(off / sum, 5e-4) << frames << " frames from a cut at " << level
                                               << " of the light are filtered by the light's own level";
                }
        }

        /// What the history is worth where the cascade has nothing to borrow from.
        ///
        /// The grazing sheet above is the wavelet's best case and cannot answer this: every pixel has the same
        /// expected bounce, so 125 taps average 125 draws of one distribution, and a history cannot improve on
        /// nothing left to remove. This is the other case, which is Morrowind's geometry: a surface whose
        /// neighbours disagree. The sheet is cut into a grid of coplanar cells whose shading normals alternate
        /// by forty degrees, far outside what `FacingWeights::mFilter` lets a tap carry, so the plane test
        /// passes everywhere, the normal test rejects nearly every neighbour, and the cascade is left with
        /// little more than the centre pixel. The accumulator is unchanged: a still camera reprojects every
        /// pixel onto itself.
        ///
        /// The alternating tilt is what a bumpy surface is, not a trick: the neighbours may not be averaged
        /// because they are genuinely lit differently, a cosine lobe tilted forty degrees sampling a different
        /// part of this sky.
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

            // The cascade alone over the same sixteen draws the history averaged, pooled as a root mean square:
            // one frame's error swings by four per cent with its draw, which put a single-frame ratio at the
            // mercy of the sampler's stream, where sixteen pooled swing by one per cent.
            double pooled = 0.0;
            for (std::uint32_t frame = 0; frame < Shaders::ACCUMULATE_FRAMES; ++frame)
            {
                std::vector<float> one;
                one = shoot(scene, {}, camera, size, filteredRun(1, frame)).mRadiance;
                const double error = errorAgainstReference(one);
                pooled += error * error / static_cast<double>(Shaders::ACCUMULATE_FRAMES);
            }
            const double alone = std::sqrt(pooled);

            // Measured through the composite (`readRadiance`): the cascade alone leaves 0.0165 pooled over
            // sixteen frames and the same sixteen accumulated 0.0050, the history removing seven tenths of the
            // error the filter cannot reach. Deterministic for one stream; over ten streams the ratio spread by
            // 0.011 about its mean, so the bound below stands far over the 0.30 measured.
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

        /// The widest level's taps stand off their lattice (`ATROUS_JITTER_STEP`). The history fix's scene at
        /// 128 square: sixteen frames, each filtered from a cut, and the noise each leaves about their mean. On
        /// a fixed lattice two pixels eight apart share six of the last level's nine taps and their noise moves
        /// together, correlated at 0.645 eight pixels apart; moved by ReLAX's offset, 0.424.
        TEST_F(RtxVisibilityTest, theWidestLevelsTapsStandOffTheirLattice)
        {
            constexpr std::uint32_t size = 128;
            constexpr std::uint32_t draws = 16;
            constexpr std::uint32_t lag = 8;

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, -100.0f));
            addQuad(scene, uprightQuadAt(4000.0f, 300.0f));
            scene.addLight(Light{
                .mPosition = osg::Vec3f(0.0f, 280.0f, -40.0f),
                .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                .mReach = 150.0f,
            });
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -200.0f, 50.0f), osg::Vec3f(0.0f, 300.0f, -100.0f), 60.0f, size, size, 10000.0f);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();

            std::vector<std::vector<float>> frames;
            for (std::uint32_t draw = 0; draw < draws; ++draw)
                frames.push_back(shoot(scene, {}, camera, size, filteredRun(1, 500 + 37 * draw)).mRadiance);

            std::vector<double> mean(frames.front().size(), 0.0);
            for (const std::vector<float>& frame : frames)
                for (std::size_t value = 0; value < frame.size(); ++value)
                    mean[value] += static_cast<double>(frame[value]) / draws;

            // Green's noise about the mean, against itself `lag` pixels along the row.
            double together = 0.0;
            double alone = 0.0;
            for (const std::vector<float>& frame : frames)
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = 0; x + lag < size; ++x)
                    {
                        const std::size_t here = (std::size_t{ y } * size + x) * 4 + 1;
                        const std::size_t there = here + std::size_t{ lag } * 4;
                        const double noise = static_cast<double>(frame[here]) - mean[here];
                        together += noise * (static_cast<double>(frame[there]) - mean[there]);
                        alone += noise * noise;
                    }

            EXPECT_LT(together / alone, 0.55) << "the noise eight pixels apart moves together";
        }

        /// The composite puts back what the trace divided out: every channel a filter averages holds light per
        /// unit of the albedo the composite multiplies back, so one frame through the filters is the frame the
        /// trace composed itself, to the rounding of the channels' storage. A floor under a two-sided sheet that
        /// glows with a radiance of one and covers every direction it gathers from: each diffuse bounce meets
        /// the sheet, so the bounce is one at every pixel, and each lobe ray off the white metal meets it too.
        /// A channel put back by another albedo than its own (the diffuse one, nought on the metal, for the
        /// lobe's) misses by the whole of it.
        TEST_F(RtxVisibilityTest, theCompositePutsBackWhatTheTraceDividedOut)
        {
            constexpr std::uint32_t size = 32;

            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            constexpr std::array<std::uint8_t, 4> metalTexel{ 255, 128, 255, 255 };
            const std::array<TextureData, 2> metalTextures{ describeTexel(white, 0), describeTexel(metalTexel, 1) };

            const auto under = [](bool metal) {
                SceneDesc scene;
                if (metal)
                {
                    const Index diffuse = scene.textures().add(VFS::Path::NormalizedView("white.dds"));
                    const Index map = scene.textures().add(
                        VFS::Path::NormalizedView("white_spec.dds"), TextureWrap::Repeat, TextureEncoding::Data);
                    scene.addInstance(MeshInstance{
                        .mMesh = scene.addMesh(MeshArrays{
                            .mPositions = sheetAt(40000.0f, 0.0f), .mTexCoords = sQuadUv, .mIndices = sQuadIndices }),
                        .mMaterial = scene.addMaterial(Material{ .mDiffuse = diffuse, .mSpecular = map }) });
                }
                else
                    addQuad(scene, sheetAt(40000.0f, 0.0f));

                addQuad(scene, sheetAt(40000.0f, 100.0f),
                    scene.addMaterial(
                        Material{ .mEmissiveColour = osg::Vec3f(0.125f, 0.125f, 0.125f), .mTwoSided = true }));
                return scene;
            };

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 50.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mAmbient = osg::Vec3f();
            camera.mAmbientFromSky = 1.0f;
            camera.mFrame = 4000;

            for (const bool metal : { false, true })
            {
                const SceneDesc scene = under(metal);
                const std::span<const TextureData> textures
                    = metal ? std::span<const TextureData>(metalTextures) : std::span<const TextureData>();

                const Frame composed = shoot(scene, textures, camera, size);
                const Frame filtered = shoot(scene, textures, camera, size, filteredRun(1, 4000));

                // Half floats, the narrowest any of these channels is stored at, hold a value to a
                // part in 2048; the composite multiplies two of them back.
                ASSERT_GT(composed.mean(0), 0.25f) << (metal ? "the metal" : "the grey floor") << " is lit";
                for (std::size_t value = 0; value < composed.mRadiance.size(); value += 4)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        ASSERT_NEAR(filtered.mRadiance[value + channel], composed.mRadiance[value + channel],
                            composed.mRadiance[value + channel] * 2e-3f)
                            << (metal ? "the metal" : "the grey floor") << ", value " << value + channel;
            }
        }
    }
}
