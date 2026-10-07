#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/device/readback.hpp>
#include <components/rtx/environment/fogbuilder.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/shaders/shared/ripple.h>
#include <components/rtxvulkan/trace/ripplepass.hpp>
#include <components/rtxvulkan/trace/tracemedia.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxRipplePassTest : Testing::DeviceTest
        {
        };

        /// Where texel `(x, y)` of the finest level begins in a read-back of it: four halves a texel.
        std::size_t texelOf(const int x, const int y)
        {
            return (static_cast<std::size_t>(y) * Shaders::RIPPLE_GRID + static_cast<std::size_t>(x)) * 4;
        }

        /// Presses `impulses` and records `frames` frames on the water's clock, each `sixtieths`
        /// long — one sixtieth unless a test of the step's length says otherwise.
        void run(RipplePass& ripples, CommandPool& pool, std::span<const RippleImpulse> impulses, const int frames,
            const double sixtieths = 1.0)
        {
            const double frame = sixtieths / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            pool.submitAndWait([&](VkCommandBuffer commands) {
                // The first record stands the window and keeps the impulses, at a clock that steps
                // nothing; each one after steps, and the first step presses what was kept.
                ripples.record(commands, FrameSlot{ 0 }, impulses, osg::Vec2f(0.0f, 0.0f), 0.0, nullptr);
                for (int at = 1; at <= frames; ++at)
                    ripples.record(commands, FrameSlot{ 0 }, {}, osg::Vec2f(0.0f, 0.0f), at * frame, nullptr);
            });
        }

        /// How far from the middle texel the ring's outermost crest stands along +x: the last
        /// texel at a local peak of the slope over a tenth of the largest within sixty texels.
        int frontOf(const std::vector<float>& surface)
        {
            constexpr int centre = static_cast<int>(Shaders::RIPPLE_GRID / 2);
            float largest = 0.0f;
            for (int away = 0; away <= 60; ++away)
                largest = std::max(largest, std::abs(surface[texelOf(centre + away, centre)]));

            int front = 0;
            for (int away = 1; away < 60; ++away)
            {
                const float here = std::abs(surface[texelOf(centre + away, centre)]);
                const bool crest = here >= std::abs(surface[texelOf(centre + away - 1, centre)])
                    && here >= std::abs(surface[texelOf(centre + away + 1, centre)]);
                if (crest && here > 0.1f * largest)
                    front = away;
            }
            return front;
        }

        /// A field nothing disturbed is still water: no slope, no curvature, and a window centred
        /// on the eye.
        TEST_F(RtxRipplePassTest, aFieldNothingDisturbedIsStillWater)
        {
            RipplePass ripples(getDevice());
            run(ripples, getPool(), {}, 10);

            const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);
            const std::vector<float> curvature = Testing::readHalves(ripples.getCurvature(), 0);
            ASSERT_EQ(surface.size(), std::size_t{ Shaders::RIPPLE_GRID } * Shaders::RIPPLE_GRID * 4);

            for (std::size_t at = 0; at < surface.size(); ++at)
            {
                ASSERT_EQ(surface[at], 0.0f) << "surface value " << at;
                ASSERT_EQ(curvature[at], 0.0f) << "curvature value " << at;
            }

            // The eye at the origin stands in the middle of the window: half the grid back along
            // each axis, in texels of two and a half units.
            const float half = -0.5f * Shaders::RIPPLE_TEXEL * static_cast<float>(Shaders::RIPPLE_GRID);
            EXPECT_FLOAT_EQ(ripples.getOrigin().x(), half);
            EXPECT_FLOAT_EQ(ripples.getOrigin().y(), half);
            EXPECT_FLOAT_EQ(RipplePass::getExtent(), 2560.0f);
        }

        /// One footfall at the eye presses a ring that spreads: after sixty steps its front stands
        /// where the springs' wave speed carried it and nothing stands far beyond, and the field is
        /// as symmetric as the stamp was.
        ///
        /// **The reach is the springs' own number.** `applySprings` couples a texel to its four
        /// neighbours at `RIPPLE_STIFFNESS`, 0.28, which is a wave speed of `sqrt(0.28)` texels a
        /// step — 0.53 — so sixty steps carry the front some thirty-two texels past the ring. Two
        /// hundred texels out nothing has arrived, and at the axis the slope along it is nought by
        /// symmetry.
        TEST_F(RtxRipplePassTest, aFootfallPressesARingThatSpreadsAndStaysSymmetric)
        {
            RipplePass ripples(getDevice());

            // On the middle texel's own centre, half a texel past the origin along each axis, so
            // the ring is symmetric about that texel and not about the corner between two.
            const float middle = 0.5f * Shaders::RIPPLE_TEXEL;
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(middle, middle), .mSize = 12.0f } };
            run(ripples, getPool(), footfall, 60);

            const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);
            const std::vector<float> curvature = Testing::readHalves(ripples.getCurvature(), 0);

            // The eye's own texel: the impulse landed on the middle texel's centre, which the
            // window puts at the grid's middle.
            constexpr int centre = static_cast<int>(Shaders::RIPPLE_GRID / 2);

            float largestSlope = 0.0f;
            float largestCurvature = 0.0f;
            for (int away = 0; away <= 40; ++away)
            {
                largestSlope = std::max(largestSlope, std::abs(surface[texelOf(centre + away, centre)]));
                largestCurvature = std::max(largestCurvature, std::abs(curvature[texelOf(centre + away, centre)]));
            }
            EXPECT_GT(largestSlope, 1.0e-3f) << "no ring within forty texels";
            EXPECT_GT(largestCurvature, 1.0e-5f) << "no curvature within forty texels";

            // **The front stands where the wave speed carried the ring.** The ring is 12 units
            // across a radius, 4.8 texels of 2.5, and the stamp presses out to twice that. Its
            // outermost crest is carried `sqrt(RIPPLE_STIFFNESS) * 60 = 31.75` texels, to 36.55: the
            // last lobe of the slope above a tenth of the largest stands within two texels of it,
            // the lattice's dispersion leaving the rest of the train behind. Past the rim carried
            // as far, 41.35 texels, the slope is under a hundredth of the largest.
            const float radius = footfall[0].mSize / Shaders::RIPPLE_TEXEL;
            const float carried = std::sqrt(Shaders::RIPPLE_STIFFNESS) * 60.0f;
            EXPECT_NEAR(static_cast<float>(frontOf(surface)), carried + radius, 2.0f) << "the front's crest";
            for (int away = static_cast<int>(std::ceil(carried + 2.0f * radius)); away <= 60; ++away)
                EXPECT_LT(std::abs(surface[texelOf(centre + away, centre)]), 0.01f * largestSlope)
                    << "a slope " << away << " texels out, past the rim";

            for (int away = 200; away <= 210; ++away)
            {
                EXPECT_EQ(surface[texelOf(centre + away, centre)], 0.0f) << "a slope " << away << " texels out";
                EXPECT_EQ(surface[texelOf(centre + away, centre) + 1], 0.0f) << "a slope " << away << " texels out";
            }

            // Symmetric about the stamp: the x slope at `+d` is the negative of the one at `-d`,
            // and along the axis the y slope is nought.
            for (int away = 1; away <= 40; ++away)
            {
                const float east = surface[texelOf(centre + away, centre)];
                const float west = surface[texelOf(centre - away, centre)];
                EXPECT_NEAR(east, -west, 1.0e-4f) << "at " << away;
                EXPECT_NEAR(surface[texelOf(centre + away, centre) + 1], 0.0f, 1.0e-4f) << "at " << away;
            }

            // The second moment is the slope's own square, level nought being one texel's mean.
            const std::size_t at = texelOf(centre + 6, centre);
            const float slopeSquared = surface[at] * surface[at] + surface[at + 1] * surface[at + 1];
            EXPECT_NEAR(surface[at + 2], slopeSquared, 1.0e-3f * std::max(slopeSquared, 1.0e-3f));
        }

        /// **The wake keeps its pace at any frame rate**: a second of the water's clock spreads the
        /// ring as far in 120 frames of half a sixtieth, in 240 of a quarter, and in 20 frames of
        /// three that each take three steps, as in 60 of one.
        ///
        /// **Measured by where the slope's energy stands**, `Σ r s² / Σ s²` out along +x, which
        /// moves with the ring smoothly where its outermost crest jumps a lobe at a time. At 60
        /// steps of one it is 21.59 texels; shorter steps bring it in by the lattice's dispersion
        /// alone, which a step of one partly cancels — 21.32 at half and 21.18 at a quarter — so
        /// every pace stands within half a texel of it. Stepped once a sixtieth as upstream steps,
        /// the 20 frames took 20 steps, and the ring stood where a third of a second leaves it.
        TEST_F(RtxRipplePassTest, theWakeKeepsItsPaceAtAnyFrameRate)
        {
            const float middle = 0.5f * Shaders::RIPPLE_TEXEL;
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(middle, middle), .mSize = 12.0f } };

            const auto spreadAt = [&](const int frames, const double sixtieths) {
                RipplePass ripples(getDevice());
                run(ripples, getPool(), footfall, frames, sixtieths);
                const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);

                constexpr int centre = static_cast<int>(Shaders::RIPPLE_GRID / 2);
                double moment = 0.0;
                double total = 0.0;
                for (int away = 0; away <= 80; ++away)
                {
                    const double slope = surface[texelOf(centre + away, centre)];
                    moment += away * slope * slope;
                    total += slope * slope;
                }
                return moment / total;
            };

            const double sixty = spreadAt(60, 1.0);
            EXPECT_NEAR(sixty, 21.59, 0.01);
            EXPECT_NEAR(spreadAt(120, 0.5), sixty, 0.5) << "120 frames of half a sixtieth";
            EXPECT_NEAR(spreadAt(240, 0.25), sixty, 0.5) << "240 frames of a quarter";
            EXPECT_NEAR(spreadAt(20, 3.0), sixty, 0.5) << "20 frames of three sixtieths";
        }

        /// **A frame longer than the steps it may take drops the rest of its time**: one frame of a
        /// whole second steps `RIPPLE_SUBSTEPS_MOST` of the longest steps and no more, which is the
        /// field one frame of exactly that long leaves, to the bit.
        ///
        /// **And the longest step is the springs' bound**: `√(2 (1 + 0.96²) / (8 × 0.28 + 0.04))`,
        /// 1.2983 sixtieths, under which the checkerboard's pull over the step stays within what the
        /// carry of the step holds back, `s² × 2.28 ≤ 2 (1 + 0.96^s)`.
        TEST_F(RtxRipplePassTest, aFrameLongerThanItsStepsDropsTheRestOfItsTime)
        {
            const float longest = RipplePass::getLongestStep();
            EXPECT_NEAR(longest, 1.2983f, 1.0e-4f);
            EXPECT_LE(longest * longest * 2.28f, 2.0f * (1.0f + std::pow(0.96f, longest)));

            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f } };
            const auto field = [&](const double sixtieths) {
                RipplePass ripples(getDevice());
                run(ripples, getPool(), footfall, 1, sixtieths);
                return Testing::readHalves(ripples.getSurface(), 0);
            };

            const std::vector<float> capped
                = field(static_cast<double>(Shaders::RIPPLE_SUBSTEPS_MOST) * static_cast<double>(longest));
            EXPECT_TRUE(std::ranges::any_of(capped, [](float value) { return value != 0.0f; }))
                << "nothing was pressed";
            EXPECT_EQ(field(60.0), capped) << "a second's frame stepped past the cap";
        }

        /// **What a step presses is capped at what the buffer holds, oldest first**: one impulse
        /// past `RIPPLE_IMPULSES_MOST` is dropped, and the water where it fell stays still. The
        /// kept ones fall a hundred texels east, where the dropped one's ring could not reach.
        TEST_F(RtxRipplePassTest, theImpulsesPastWhatTheBufferHoldsAreDropped)
        {
            RipplePass ripples(getDevice());

            std::vector<RippleImpulse> impulses(Shaders::RIPPLE_IMPULSES_MOST,
                RippleImpulse{ .mAt = osg::Vec2f(100.0f * Shaders::RIPPLE_TEXEL, 0.0f), .mSize = 12.0f });
            impulses.push_back(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });
            run(ripples, getPool(), impulses, 10);

            const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);
            constexpr int centre = static_cast<int>(Shaders::RIPPLE_GRID / 2);
            float near = 0.0f;
            float kept = 0.0f;
            for (int away = -10; away <= 10; ++away)
            {
                near = std::max(near, std::abs(surface[texelOf(centre + away, centre)]));
                kept = std::max(kept, std::abs(surface[texelOf(centre + 100 + away, centre)]));
            }
            EXPECT_GT(kept, 0.0f) << "the kept impulses pressed nothing";
            EXPECT_EQ(near, 0.0f) << "the one past the cap was pressed";
        }

        /// The window follows the eye by whole texels and the ring stays where it was pressed.
        TEST_F(RtxRipplePassTest, theWindowFollowsTheEyeAndTheRingStaysWhereItWasPressed)
        {
            RipplePass ripples(getDevice());

            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f } };
            run(ripples, getPool(), footfall, 30);

            // The eye walks a hundred texels east between two steps, and the field is read again.
            const double sixtieth = 1.0 / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            const float walked = 100.0f * Shaders::RIPPLE_TEXEL;
            getPool().submitAndWait([&](VkCommandBuffer commands) {
                ripples.record(commands, FrameSlot{ 0 }, {}, osg::Vec2f(walked, 0.0f), 31.0 * sixtieth, nullptr);
            });
            const std::vector<float> after = Testing::readHalves(ripples.getSurface(), 0);

            EXPECT_FLOAT_EQ(ripples.getOrigin().x(), walked - 0.5f * RipplePass::getExtent());

            // What stood `d` texels east of the middle now stands `d - 100` texels east of it, one
            // step older: the ring is still there, and not where the eye went.
            constexpr int centre = static_cast<int>(Shaders::RIPPLE_GRID / 2);
            float found = 0.0f;
            for (int away = -40; away <= 40; ++away)
                found = std::max(found, std::abs(after[texelOf(centre + away - 100, centre)]));
            EXPECT_GT(found, 1.0e-3f) << "the ring did not come across with the window";

            float atEye = 0.0f;
            for (int away = -5; away <= 5; ++away)
                atEye = std::max(atEye, std::abs(after[texelOf(centre + away, centre)]));
            EXPECT_EQ(atEye, 0.0f) << "still water where the eye went";
        }

        /// **A placement's footfalls are pressed once, however many traces read it.** One placement
        /// that stands a footfall, traced twice before a step is due and then stepped thirty times,
        /// leaves the field one press leaves: `TraceMedia::stepRipples` spends what it kept. Without
        /// that, every trace of a frame the game paused on handed the pass the last footfalls again.
        TEST_F(RtxRipplePassTest, aPlacementsFootfallIsPressedOnceHoweverOftenItIsTraced)
        {
            const double sixtieth = 1.0 / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            SceneDesc scene;
            scene.addRipple(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });

            const auto field = [&](const int traces) {
                TraceMedia media(getDevice(), bakeFogNoise());
                media.keepRipples(scene);
                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    for (int trace = 0; trace < traces; ++trace)
                        media.stepRipples(commands, FrameSlot{ 0 }, osg::Vec2f(0.0f, 0.0f), 0.0, nullptr);
                    for (int step = 1; step <= 30; ++step)
                        media.stepRipples(commands, FrameSlot{ 0 }, osg::Vec2f(0.0f, 0.0f), step * sixtieth, nullptr);
                });
                return Testing::readHalves(media.getRipples().getSurface(), 0);
            };

            const std::vector<float> once = field(1);
            const std::vector<float> twice = field(2);
            EXPECT_TRUE(std::ranges::any_of(once, [](float value) { return value != 0.0f; })) << "nothing was pressed";
            EXPECT_EQ(once, twice) << "a second trace of one placement pressed its footfall again";
        }
    }
}
