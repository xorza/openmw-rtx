#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtxvulkan/support/device/scenestander.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/visibility.h>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        constexpr std::uint32_t sSize = 96;

        /// What the moving bar takes off the floor: the sun's light, or the sky's fill.
        enum class Blocked
        {
            Sun,
            Sky,
        };

        /// How far a moving occluder's darkness stands behind it, against a still picture of it.
        struct Trail
        {
            /// How far the half-way level of the trailing edge stands behind the still picture's, in
            /// pixels.
            float mLag = 0.0f;

            /// The darkness left in what the still picture lights, behind the edge, in columns of full
            /// darkness.
            float mTail = 0.0f;
        };

        /// **A bar's darkness dragged over a floor**, seen from above: the bar spans the frame's rows
        /// and moves across its columns, so a column's mean over the rows is the picture's profile.
        ///
        /// Under the sun the bar stands 100 units up and casts a shadow of its own width; under the sky
        /// it stands 20 units up, low enough that the fill it blocks darkens the floor under it, as an
        /// actor darkens the ground at its feet. The bar moves 4 units a frame, about three quarters
        /// of a pixel, for 40 frames after 32 still ones, from two draws of the sampler; the profile is
        /// held against 48 still frames of the bar where the run left it. **Two draws and 48 frames**,
        /// because a frame is a millisecond of the host's: one draw's lag stood within 0.6 pixels of four
        /// draws' mean, and the still edge 0.18 pixels from 96 frames'. **The
        /// placement table is advanced after each hand-over**, as `SceneUploader` does, or the bar's
        /// motion vector is its whole travel since it stood still.
        class RtxBounceTrailTest : public RtxSettlingTest
        {
        protected:
            Trail trailOf(Blocked blocked, bool antilag)
            {
                SceneIdentities identities;
                constexpr std::uint32_t still = 32;
                constexpr std::uint32_t moving = 40;
                constexpr std::uint32_t draws = 2;
                constexpr float step = 4.0f;
                constexpr float width = 40.0f;

                Shaders::VisibilityConstants camera = overheadSun(sSize);
                if (blocked == Blocked::Sky)
                {
                    camera.mSun.mIrradiance = osg::Vec3f();
                    camera.mSkyHorizon = osg::Vec3f(0.6f, 0.6f, 0.6f);
                    camera.mSkyZenith = osg::Vec3f(0.6f, 0.6f, 0.6f);
                    camera.mAmbientFromSky = 1.0f;
                }

                const float height = blocked == Blocked::Sky ? 20.0f : 100.0f;
                SceneDesc scene(identities);
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                const PlacementIndex bar = addQuad(scene, roofOver(-100.0f, -100.0f + width, height));

                mRenderer.resize(sSize, sSize);
                Testing::standerOf(mRenderer).setScene(mRenderer, Rtx::SceneSlot::world(), scene, {});

                std::vector<std::uint8_t> pixels;
                std::vector<float> column(sSize);
                const auto profile = [&](std::uint32_t frames, std::uint32_t first, bool moves) {
                    for (std::uint32_t at = 0; at < frames; ++at)
                    {
                        if (moves && at >= still)
                        {
                            scene.placements().move(
                                bar, osg::Matrixf::translate(step * static_cast<float>(at + 1 - still), 0.0f, 0.0f));
                            Testing::standerOf(mRenderer).placeScene(mRenderer, Rtx::SceneSlot::world(), scene);
                            scene.placements().advance();
                        }
                        Shaders::VisibilityConstants sampled = camera;
                        sampled.mFrame = first + at;
                        mProbe.traceFrame(sampled,
                            FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                                .mReconstruction
                                = ReconstructionRequest{ .mDenoise = true, .mFilters = { .mAntilag = antilag } },
                                .mExposure = FixedExposure{ 1.0f } });

                        // **Two frames in flight, as the game keeps them**, and not each waited out:
                        // a frame here is a few dispatches at 96 pixels, so waiting each one out pays
                        // the queue's round trip on every one of a few hundred. The frames are the same
                        // either way, to the last bit of every figure the tests read.
                        mRenderer.collectFrame();
                    }
                    while (mRenderer.finishFrame().has_value())
                    {
                    }

                    // The rows the frame's edges leave alone, averaged per column.
                    mRenderer.readPixels(pixels);
                    for (std::uint32_t x = 0; x < sSize; ++x)
                    {
                        float sum = 0.0f;
                        for (std::uint32_t y = 12; y < sSize - 12; ++y)
                            sum += static_cast<float>(pixels[(std::size_t{ y } * sSize + x) * 4 + 1]);
                        column[x] = sum / static_cast<float>(sSize - 24);
                    }
                    return column;
                };

                scene.placements().move(bar, osg::Matrixf::translate(step * static_cast<float>(moving), 0.0f, 0.0f));
                Testing::standerOf(mRenderer).placeScene(mRenderer, Rtx::SceneSlot::world(), scene);
                scene.placements().advance();
                const std::vector<float> standing = profile(48, 5000, false);

                // The bar moves toward the high columns, so its trail is on the low ones: the first
                // column where a profile falls through the level half-way into the still shadow.
                const float lit = standing[4];
                const float middle = 0.5f * (lit + *std::min_element(standing.begin(), standing.end()));
                const auto edgeOf = [&](const std::vector<float>& row) {
                    for (std::uint32_t x = 1; x < sSize; ++x)
                        if (row[x] < middle)
                            return static_cast<float>(x - 1) + (row[x - 1] - middle) / (row[x - 1] - row[x]);
                    return static_cast<float>(sSize);
                };
                const float standingEdge = edgeOf(standing);

                Trail trail;
                for (std::uint32_t draw = 0; draw < draws; ++draw)
                {
                    scene.placements().move(bar, osg::Matrixf::identity());
                    Testing::standerOf(mRenderer).placeScene(mRenderer, Rtx::SceneSlot::world(), scene);
                    scene.placements().advance();
                    const std::vector<float>& moved = profile(still + moving, 100 + 1000 * draw, true);
                    trail.mLag += (standingEdge - edgeOf(moved)) / static_cast<float>(draws);
                    for (std::uint32_t x = 0; x < sSize && standing[x] >= 0.9f * lit; ++x)
                        trail.mTail += std::max(standing[x] - moved[x], 0.0f) / lit / static_cast<float>(draws);
                }
                return trail;
            }
        };

        /// **The sun's shadow follows its caster within a third of a pixel.** It is the shadow
        /// denoiser's, whose history the bits clamp: measured at 0.04 pixels of lag and no darkness
        /// left behind.
        TEST_F(RtxBounceTrailTest, theSunsShadowFollowsItsCaster)
        {
            const Trail trail = trailOf(Blocked::Sun, true);
            EXPECT_LT(trail.mLag, 0.4f);
            EXPECT_LT(trail.mTail, 0.25f);
        }

        /// **The clamp shortens the trail the sky's fill leaves**: measured at 8.72 pixels of lag without it and 5.31
        /// with it (`ACCUMULATE_FAST_FRAMES` gives the sweep), and the darkness left behind at 1.46 and 0.67 columns,
        /// under a quarter of a second of history (`ACCUMULATE_SECONDS`). Under thirty-two frames the drag was 15.95
        /// pixels and the clamp's 5.24: the shorter history took most of what the clamp had to take.
        ///
        /// **The history fix takes part of what the clamp did.** The floor the bar uncovers behind it
        /// is rebuilt from the floor around it (`ACCUMULATE_FIX_FRAMES`): under thirty-two frames, without
        /// the fix the clamp took the lag from 17.84 to 5.35 and the darkness from 2.39 to 0.82, so the fix
        /// left a fifth less darkness behind the bar.
        TEST_F(RtxBounceTrailTest, theClampShortensTheSkysTrail)
        {
            const Trail held = trailOf(Blocked::Sky, true);
            const Trail dragged = trailOf(Blocked::Sky, false);
            EXPECT_LT(held.mLag, 7.0f);
            EXPECT_LT(held.mLag, 0.75f * dragged.mLag) << "the clamp took little off the trail: " << dragged.mLag;
            EXPECT_LT(held.mTail, 0.9f);
            EXPECT_LT(held.mTail, dragged.mTail);
        }

        /// A floor under a sky that lights it, with a bar standing still on it, for the two tests of
        /// what the clamp does to light that does not move and to light that changes everywhere.
        class RtxBounceClampTest : public RtxSettlingTest
        {
        protected:
            static constexpr std::uint32_t sFloorSize = 64;
            static inline const osg::Vec3f sGrey{ 0.6f, 0.6f, 0.6f };

            /// The floor's mean radiance in `channel` after each of `frames` frames under a sky of `sky`, filtered by
            /// `filters`.
            std::vector<double> meansOf(const FilterSwitches& filters, std::uint32_t frames, std::uint32_t first,
                const osg::Vec3f& sky, bool cut, std::size_t channel = 1)
            {
                Shaders::VisibilityConstants camera = overheadSun(sFloorSize);
                camera.mSun.mIrradiance = osg::Vec3f();
                camera.mSkyHorizon = sky;
                camera.mSkyZenith = sky;
                camera.mAmbientFromSky = 1.0f;

                std::vector<double> means;
                std::vector<float> radiance;
                for (std::uint32_t at = 0; at < frames; ++at)
                {
                    Shaders::VisibilityConstants sampled = camera;
                    sampled.mFrame = first + at;
                    mProbe.traceFrame(sampled,
                        FrameOptions{ .mLoss = cut && at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                            .mReconstruction = ReconstructionRequest{ .mDenoise = true, .mFilters = filters },
                            .mExposure = FixedExposure{ 1.0f } });
                    EXPECT_TRUE(mRenderer.finishFrame().has_value());
                    mProbe.readComposite(radiance);

                    double sum = 0.0;
                    for (std::size_t value = channel; value < radiance.size(); value += 4)
                        sum += static_cast<double>(radiance[value]);
                    // A whole-number division on purpose.
                    // NOLINTNEXTLINE(bugprone-integer-division)
                    means.push_back(sum / static_cast<double>(radiance.size() / 4));
                }
                return means;
            }

            void SetUp() override
            {
                SceneIdentities identities;
                SceneDesc scene(identities);
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                addQuad(scene, roofOver(-20.0f, 20.0f, 20.0f));
                mRenderer.resize(sFloorSize, sFloorSize);
                Testing::standerOf(mRenderer).setScene(mRenderer, Rtx::SceneSlot::world(), scene, {});
            }
        };

        /// **The clamp leaves light that does not move nearly alone.** After 64 still frames the
        /// floor's mean with the clamp is 0.36% over the mean without it (0.75% under thirty-two frames of
        /// history): the box of two-frame fast
        /// means is one draw's spread wide, and a still slow mean stands outside it on a few frames —
        /// the trade `ACCUMULATE_FAST_FRAMES` takes for the trail. Under the spatiotemporal reuse,
        /// whose reservoirs smooth the bounce the box is made of, the two stood 0.06% apart.
        TEST_F(RtxBounceClampTest, theClampLeavesStillLightAlone)
        {
            const double held = meansOf({ .mAntilag = true }, 64, 100, sGrey, true).back();
            const double dragged = meansOf({ .mAntilag = false }, 64, 100, sGrey, true).back();
            EXPECT_NEAR(held / dragged, 1.0, 0.01);
        }

        /// **The floor follows a sky whose light halves.** Every term the floor's light holds is
        /// linear in the sky, so the new level is half the old one exactly. With the clamp the mean
        /// stood within 2% of it on frame 21 (0.1480 against 0.1451); without it at 0.1787, 24% over,
        /// though the quarter-second history (`ACCUMULATE_SECONDS`) had turned over since.
        TEST_F(RtxBounceClampTest, theFloorFollowsASkyWhoseLightHalves)
        {
            for (const bool antilag : { true, false })
            {
                const double before = meansOf({ .mAntilag = antilag }, 64, 100, sGrey, true).back();
                const std::vector<double> after = meansOf({ .mAntilag = antilag }, 21, 200, sGrey * 0.5f, false);
                const double share = after.back() / (0.5 * before);
                if (antilag)
                    EXPECT_LT(share, 1.1) << "the clamp did not follow the sky down";
                else
                    EXPECT_GT(share, 1.1) << "the history followed the sky without the clamp, so this proves nothing";
            }
        }

        /// **Unclamped, the history drags for as long as it spans** (`ACCUMULATE_SECONDS`): a history of `N` frames
        /// keeps `(1 - 1/N)^k` of the old light `k` frames on, so 21 frames after the sky halved a quarter of a
        /// second, 15 frames, keeps `(14/15)^21` = 0.2350 of the excess and half a second, 30 frames, `(29/30)^21` =
        /// 0.4908, 2.09 times as much. The floor's light is linear in the sky, so the excess over the new level
        /// stands in that ratio.
        TEST_F(RtxBounceClampTest, anUnclampedHistoryDragsForAsLongAsItSpans)
        {
            const auto excessOf = [&](float seconds) {
                const FilterSwitches filters{ .mAntilag = false, .mHistorySeconds = seconds };
                const double before = meansOf(filters, 64, 100, sGrey, true).back();
                const double after = meansOf(filters, 21, 200, sGrey * 0.5f, false).back();
                return after / (0.5 * before) - 1.0;
            };
            const double quarter = excessOf(0.25f);
            const double half = excessOf(0.5f);
            EXPECT_GT(quarter, 0.1) << "the history still drags a quarter of a second on, or this proves nothing";
            EXPECT_NEAR(half / quarter, std::pow(29.0 / 30.0, 21.0) / std::pow(14.0 / 15.0, 21.0), 0.2);
        }

        /// **The floor follows a sky that changes its hue and keeps its luminance.** From
        /// `(0.9, 0.5, 0.3)` to `(0.3, 0.5, 2.0668)`, both 0.5706 by Rec. 709's weights: a box over
        /// luminance alone holds the old red, and one over YCoCg's axes does not. The floor's red is
        /// linear in the sky's, so it falls to a third exactly. With the clamp it stood at 1.001 of it
        /// 30 frames on; without it at 1.253. Under thirty-two frames of history those were 1.077 and
        /// 2.026, and under the luminance box the clamp kept before, 1.941.
        TEST_F(RtxBounceClampTest, theFloorFollowsASkyThatChangesHueAtOneLuminance)
        {
            const osg::Vec3f warm(0.9f, 0.5f, 0.3f);
            const osg::Vec3f cold(0.3f, 0.5f, 2.0668f);
            ASSERT_NEAR(warm * Shaders::LUMINANCE_WEIGHTS, cold * Shaders::LUMINANCE_WEIGHTS, 1e-4f);

            for (const bool antilag : { true, false })
            {
                const double before = meansOf({ .mAntilag = antilag }, 64, 100, warm, true, 0).back();
                const double after = meansOf({ .mAntilag = antilag }, 30, 200, cold, false, 0).back();
                const double share = after / (before / 3.0);
                if (antilag)
                    EXPECT_LT(share, 1.1) << "the clamp did not follow the sky's hue";
                else
                    EXPECT_GT(share, 1.1) << "the history followed the hue without the clamp, so this proves nothing";
            }
        }
    }
}
