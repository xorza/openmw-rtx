#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtxvulkan/trace/visibility/fixture.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

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
        /// of a pixel, for 40 frames after 32 still ones, from four draws of the sampler; the profile is
        /// held against 96 still frames of the bar where the run left it.
        class RtxBounceTrailTest : public RtxVisibilityTest
        {
        protected:
            Trail trailOf(Blocked blocked, BounceReuse reuse)
            {
                constexpr std::uint32_t still = 32;
                constexpr std::uint32_t moving = 40;
                constexpr std::uint32_t draws = 4;
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
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                const Index bar = addQuad(scene, roofOver(-100.0f, -100.0f + width, height));

                mRenderer.resize(sSize, sSize);
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

                std::vector<std::uint8_t> pixels;
                std::vector<float> column(sSize);
                const auto profile = [&](std::uint32_t frames, std::uint32_t first, bool moves) {
                    for (std::uint32_t at = 0; at < frames; ++at)
                    {
                        if (moves && at >= still)
                        {
                            scene.placements().move(
                                bar, osg::Matrixf::translate(step * static_cast<float>(at + 1 - still), 0.0f, 0.0f));
                            mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                        }
                        Shaders::VisibilityConstants sampled = camera;
                        sampled.mFrame = first + at;
                        mRenderer.renderFrame(sampled,
                            FrameOptions{ .mLoss = at == 0 ? HistoryLoss::Cut : HistoryLoss::None,
                                .mReconstruction = ReconstructionRequest{ .mDenoise = true, .mBounceReuse = reuse },
                                .mExposure = FixedExposure{ 1.0f } });
                        EXPECT_TRUE(mRenderer.finishFrame().has_value());
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
                mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                const std::vector<float> standing = profile(96, 5000, false);

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
                    mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                    const std::vector<float>& moved = profile(still + moving, 100 + 1000 * draw, true);
                    trail.mLag += (standingEdge - edgeOf(moved)) / static_cast<float>(draws);
                    for (std::uint32_t x = 0; x < sSize && standing[x] >= 0.9f * lit; ++x)
                        trail.mTail += std::max(standing[x] - moved[x], 0.0f) / lit / static_cast<float>(draws);
                }
                return trail;
            }
        };

        /// **The sun's shadow follows its caster within a third of a pixel.** It is the shadow
        /// denoiser's, whose history the bits clamp: measured at 0.27 pixels of lag and 0.15 columns
        /// of darkness left behind, where the raw frame lags by 0.11 and leaves none.
        TEST_F(RtxBounceTrailTest, theSunsShadowFollowsItsCaster)
        {
            const Trail trail = trailOf(Blocked::Sun, BounceReuse::Off);
            EXPECT_LT(trail.mLag, 0.4f);
            EXPECT_LT(trail.mTail, 0.25f);
        }
    }
}
