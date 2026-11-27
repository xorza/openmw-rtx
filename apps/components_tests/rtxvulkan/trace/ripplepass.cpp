#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/hostwritten.hpp>
#include <apps/components_tests/rtxvulkan/support/device/readback.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/world/fognoise.hpp>
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
        std::size_t texelOf(const std::int32_t x, const std::int32_t y)
        {
            return (static_cast<std::size_t>(y) * Shaders::RIPPLE_GRID + static_cast<std::size_t>(x)) * 4;
        }

        /// Records a frame of each of `frames` sixtieths on the water's clock, and gives `impulses`
        /// on the first.
        void walk(RipplePass& ripples, CommandPool& pool, std::span<const RippleImpulse> impulses,
            std::span<const double> frames)
        {
            Testing::submitAndWaitWriting(pool, [&](VkCommandBuffer commands, Uploads& uploads) {
                // The first record stands the window, at a clock that steps nothing.
                ripples.record(commands, uploads, FrameSlot{ 0 }, {}, osg::Vec2f(0.0f, 0.0f), 0.0, nullptr);
                double clock = 0.0;
                for (std::size_t at = 0; at < frames.size(); ++at)
                {
                    clock += frames[at] / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
                    ripples.record(commands, uploads, FrameSlot{ 0 },
                        at == 0 ? impulses : std::span<const RippleImpulse>(), osg::Vec2f(0.0f, 0.0f), clock, nullptr);
                }
            });
        }

        /// `walk` through `frames` frames each `sixtieths` long — one sixtieth unless a test of the
        /// frame's length says otherwise.
        void run(RipplePass& ripples, CommandPool& pool, std::span<const RippleImpulse> impulses,
            const std::int32_t frames, const double sixtieths = 1.0)
        {
            const std::vector<double> lengths(static_cast<std::size_t>(frames), sixtieths);
            walk(ripples, pool, impulses, lengths);
        }

        /// How far from the middle texel the ring's outermost crest stands along +x: the last
        /// texel at a local peak of the slope over a tenth of the largest within sixty texels.
        std::int32_t frontOf(const std::vector<float>& surface)
        {
            constexpr auto centre = static_cast<std::int32_t>(Shaders::RIPPLE_GRID / 2);
            float largest = 0.0f;
            for (std::int32_t away = 0; away <= 60; ++away)
                largest = std::max(largest, std::abs(surface[texelOf(centre + away, centre)]));

            std::int32_t front = 0;
            for (std::int32_t away = 1; away < 60; ++away)
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

        /// One footfall at the eye presses a ring that spreads: after sixty steps its front stands where the springs'
        /// wave speed carried it and nothing stands far beyond, and the field is as symmetric as the stamp was.
        ///
        /// **The reach is the springs' own number.** `applySprings` couples a texel to its four neighbours at
        /// `RIPPLE_STIFFNESS`, 0.28, a wave speed of `sqrt(0.28)` = 0.53 texels a step, so sixty steps carry the front
        /// some thirty-two texels past the ring. Two hundred texels out nothing has arrived, and at the axis the slope
        /// along it is nought by symmetry.
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
            constexpr auto centre = static_cast<std::int32_t>(Shaders::RIPPLE_GRID / 2);

            float largestSlope = 0.0f;
            float largestCurvature = 0.0f;
            for (std::int32_t away = 0; away <= 40; ++away)
            {
                largestSlope = std::max(largestSlope, std::abs(surface[texelOf(centre + away, centre)]));
                largestCurvature = std::max(largestCurvature, std::abs(curvature[texelOf(centre + away, centre)]));
            }
            EXPECT_GT(largestSlope, 1.0e-3f) << "no ring within forty texels";
            EXPECT_GT(largestCurvature, 1.0e-5f) << "no curvature within forty texels";

            // **The front stands where the wave speed carried the ring.** The ring is 12 units across a radius, 4.8
            // texels of 2.5, and the stamp presses out to twice that. Its outermost crest is carried
            // `sqrt(RIPPLE_STIFFNESS) * 60 = 31.75` texels, to 36.55: the last lobe of the slope above a tenth of the
            // largest stands within two texels of it, the lattice's dispersion leaving the rest of the train behind.
            // Past the rim carried as far, 41.35 texels, the slope is under a hundredth of the largest.
            const float radius = footfall[0].mSize / Shaders::RIPPLE_TEXEL;
            const float carried = std::sqrt(Shaders::RIPPLE_STIFFNESS) * 60.0f;
            EXPECT_NEAR(static_cast<float>(frontOf(surface)), carried + radius, 2.0f) << "the front's crest";
            for (auto away = static_cast<std::int32_t>(std::ceil(carried + 2.0f * radius)); away <= 60; ++away)
                EXPECT_LT(std::abs(surface[texelOf(centre + away, centre)]), 0.01f * largestSlope)
                    << "a slope " << away << " texels out, past the rim";

            for (std::int32_t away = 200; away <= 210; ++away)
            {
                EXPECT_EQ(surface[texelOf(centre + away, centre)], 0.0f) << "a slope " << away << " texels out";
                EXPECT_EQ(surface[texelOf(centre + away, centre) + 1], 0.0f) << "a slope " << away << " texels out";
            }

            // Symmetric about the stamp: the x slope at `+d` is the negative of the one at `-d`,
            // and along the axis the y slope is nought.
            for (std::int32_t away = 1; away <= 40; ++away)
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

        /// **The field is the clock's and not the frame rate's**: a footfall pressed for a sixtieth and then sixty
        /// sixtieths more of the water's clock leave the same field to the bit, walked in frames of one, of a half, of
        /// a quarter, of three that take three steps each, and of 0.75 and 1.25 by turns, which take none and two —
        /// sixty-one steps of a sixtieth each, the same dispatches, and one compose after the last.
        ///
        /// **And so the field stays as stable as one fixed step is.** Stepped by each frame's own length, the turns
        /// of 0.75 and 1.25 ran the checkerboard up to a half's largest within a second and to NaNs across the window
        /// in twenty, each step within the length a constant step is stable at.
        TEST_F(RtxRipplePassTest, theFieldIsTheClocksAtAnyFrameRate)
        {
            const float middle = 0.5f * Shaders::RIPPLE_TEXEL;
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(middle, middle), .mSize = 12.0f } };

            const auto field = [&](const double each, const std::size_t count, const bool turns) {
                std::vector<double> frames{ 1.0 };
                for (std::size_t at = 0; at < count; ++at)
                    frames.push_back(turns ? (at % 2 == 0 ? 0.75 : 1.25) : each);
                RipplePass ripples(getDevice());
                walk(ripples, getPool(), footfall, frames);
                return Testing::readHalves(ripples.getSurface(), 0);
            };

            const std::vector<float> sixty = field(1.0, 60, false);
            EXPECT_TRUE(std::ranges::any_of(sixty, [](float value) { return value != 0.0f; })) << "nothing was pressed";
            EXPECT_EQ(field(0.5, 120, false), sixty) << "frames of half a sixtieth";
            EXPECT_EQ(field(0.25, 240, false), sixty) << "frames of a quarter";
            EXPECT_EQ(field(3.0, 20, false), sixty) << "frames of three sixtieths";
            EXPECT_EQ(field(0.0, 60, true), sixty) << "frames of 0.75 and 1.25 by turns";
        }

        /// **A frame longer than the steps it may take drops the rest of its time**: one frame of a whole second steps
        /// `RIPPLE_FRAME_STEPS_MOST` sixtieths and presses for as long, which is the field one frame of exactly that
        /// long leaves, to the bit; and owes nothing after, so a frame of one sixtieth after each takes one step.
        TEST_F(RtxRipplePassTest, aFrameLongerThanItsStepsDropsTheRestOfItsTime)
        {
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f } };
            const auto field = [&](const double sixtieths) {
                RipplePass ripples(getDevice());
                const std::array<double, 2> frames{ sixtieths, 1.0 };
                walk(ripples, getPool(), footfall, frames);
                return Testing::readHalves(ripples.getSurface(), 0);
            };

            const std::vector<float> capped = field(static_cast<double>(Shaders::RIPPLE_FRAME_STEPS_MOST));
            EXPECT_TRUE(std::ranges::any_of(capped, [](float value) { return value != 0.0f; }))
                << "nothing was pressed";
            EXPECT_EQ(field(60.0), capped) << "a second's frame stepped past the cap, or owed the rest";
        }

        /// **What a step presses is what reaches the window, and past what the buffer holds the nearest the eye.** Ten
        /// impulses five thousand units off, whose rings press no texel of the window, come first and take no room;
        /// then `RIPPLE_IMPULSES_MOST` a hundred texels east, and last one at the eye, which makes one too many. The
        /// one at the eye is pressed, being the nearest, and the east ones are too, a hundred and twenty-seven of
        /// them; pressed oldest first, the ten off the window would have taken ten of the room and the last none.
        ///
        /// **And one at no place reaches nothing**: an impulse the world handed a NaN, given first, takes no room and
        /// presses no texel, where pressed it was NaN across the window.
        TEST_F(RtxRipplePassTest, thePressesThatReachTheWindowAndTheNearestOfThemAreKept)
        {
            RipplePass ripples(getDevice());

            std::vector<RippleImpulse> impulses{ RippleImpulse{
                .mAt = osg::Vec2f(std::numeric_limits<float>::quiet_NaN(), 0.0f), .mSize = 12.0f } };
            impulses.insert(impulses.end(), 10, RippleImpulse{ .mAt = osg::Vec2f(5000.0f, 0.0f), .mSize = 12.0f });
            impulses.insert(impulses.end(), Shaders::RIPPLE_IMPULSES_MOST,
                RippleImpulse{ .mAt = osg::Vec2f(100.0f * Shaders::RIPPLE_TEXEL, 0.0f), .mSize = 12.0f });
            impulses.push_back(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });
            run(ripples, getPool(), impulses, 10);

            const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);
            constexpr auto centre = static_cast<std::int32_t>(Shaders::RIPPLE_GRID / 2);
            float near = 0.0f;
            float east = 0.0f;
            for (std::int32_t away = -10; away <= 10; ++away)
            {
                near = std::max(near, std::abs(surface[texelOf(centre + away, centre)]));
                east = std::max(east, std::abs(surface[texelOf(centre + 100 + away, centre)]));
            }
            EXPECT_GT(east, 0.0f) << "the east impulses pressed nothing";
            EXPECT_GT(near, 0.0f) << "the nearest was dropped";
            EXPECT_TRUE(std::ranges::all_of(surface, [](float value) { return std::isfinite(value); }))
                << "the impulse at no place was pressed";
        }

        /// **A press stands for the time its frame covers**: what a single step leaves on the ring, where a texel
        /// stands its radius from the impulse and keeps 0.8 a sixtieth, is the height pulled from still water toward
        /// -1 by `1 - 0.8^n` over `n` sixtieths: a fifth at one, 0.1056 at a half, 0.2434 at a sixtieth and a quarter.
        /// Each is a single step: the frame of a half crosses no sixtieth, and the press waits for the next frame's
        /// step, which gives none of its own. The surface tile holds the square of the height, so `(1 - 0.8^n)^2`; a
        /// press a frame of a fixed fifth pressed 41% a sixtieth at 144 frames a second and half that at 30.
        ///
        /// **So a wake is as deep at any frame rate**: a footfall given on both of two frames of a half, pressed by
        /// one step, keeps `0.8^½ × 0.8^½` of the still water, a fifth pulled as one frame of one pulls it.
        TEST_F(RtxRipplePassTest, aPressIsAsDeepAsTheTimeItStandsFor)
        {
            // The impulse on a texel's centre and four texels wide, so the texel four east stands on
            // the ring exactly.
            const float middle = 0.5f * Shaders::RIPPLE_TEXEL;
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(middle, middle), .mSize = 4.0f * Shaders::RIPPLE_TEXEL } };
            constexpr auto centre = static_cast<std::int32_t>(Shaders::RIPPLE_GRID / 2);

            for (const double sixtieths : { 1.0, 0.5, 1.25 })
            {
                SCOPED_TRACE(sixtieths);
                RipplePass ripples(getDevice());
                run(ripples, getPool(), footfall, sixtieths < 1.0 ? 2 : 1, sixtieths);
                const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);

                const double pulled = 1.0 - std::pow(0.8, sixtieths);
                EXPECT_NEAR(surface[texelOf(centre + 4, centre) + 3], pulled * pulled, 2.0e-3 * pulled * pulled);
            }

            RipplePass ripples(getDevice());
            const double half = 0.5 / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                ripples.record(commands, uploads, FrameSlot{ 0 }, {}, osg::Vec2f(0.0f, 0.0f), 0.0, nullptr);
                ripples.record(commands, uploads, FrameSlot{ 0 }, footfall, osg::Vec2f(0.0f, 0.0f), half, nullptr);
                ripples.record(
                    commands, uploads, FrameSlot{ 0 }, footfall, osg::Vec2f(0.0f, 0.0f), 2.0 * half, nullptr);
            });
            const std::vector<float> surface = Testing::readHalves(ripples.getSurface(), 0);
            EXPECT_NEAR(surface[texelOf(centre + 4, centre) + 3], 0.04f, 2.0e-3f * 0.04f) << "two frames of a half";
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
            Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                ripples.record(
                    commands, uploads, FrameSlot{ 0 }, {}, osg::Vec2f(walked, 0.0f), 31.0 * sixtieth, nullptr);
            });
            const std::vector<float> after = Testing::readHalves(ripples.getSurface(), 0);

            EXPECT_FLOAT_EQ(ripples.getOrigin().x(), walked - 0.5f * RipplePass::getExtent());

            // What stood `d` texels east of the middle now stands `d - 100` texels east of it, one
            // step older: the ring is still there, and not where the eye went.
            constexpr auto centre = static_cast<std::int32_t>(Shaders::RIPPLE_GRID / 2);
            float found = 0.0f;
            for (std::int32_t away = -40; away <= 40; ++away)
                found = std::max(found, std::abs(after[texelOf(centre + away - 100, centre)]));
            EXPECT_GT(found, 1.0e-3f) << "the ring did not come across with the window";

            float atEye = 0.0f;
            for (std::int32_t away = -5; away <= 5; ++away)
                atEye = std::max(atEye, std::abs(after[texelOf(centre + away, centre)]));
            EXPECT_EQ(atEye, 0.0f) << "still water where the eye went";
        }

        /// **A placement's footfalls are pressed once, however many traces read it.** One placement
        /// that stands a footfall, traced twice at one clock and then stepped thirty times,
        /// leaves the field one press leaves: `TraceMedia::stepRipples` spends what it kept. Without
        /// that, every trace of a frame the game paused on handed the pass the last footfalls again.
        TEST_F(RtxRipplePassTest, aPlacementsFootfallIsPressedOnceHoweverOftenItIsTraced)
        {
            SceneIdentities identities;
            const double sixtieth = 1.0 / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            SceneDesc scene(identities);
            scene.addRipple(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });

            // The window stood first, at a clock that steps nothing; the traces of the placement then
            // all at one clock, the first of them stepping, and thirty steps after.
            const auto field = [&](const std::int32_t traces) {
                TraceMedia media(getDevice(), sFogNoise);
                Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                    media.stepRipples(commands, uploads, FrameSlot{ 0 }, osg::Vec2f(0.0f, 0.0f), 0.0, nullptr);
                    media.keepRipples(scene);
                    for (std::int32_t trace = 0; trace < traces; ++trace)
                        media.stepRipples(commands, uploads, FrameSlot{ 0 }, osg::Vec2f(0.0f, 0.0f), sixtieth, nullptr);
                    for (std::int32_t step = 2; step <= 31; ++step)
                        media.stepRipples(
                            commands, uploads, FrameSlot{ 0 }, osg::Vec2f(0.0f, 0.0f), step * sixtieth, nullptr);
                });
                return Testing::readHalves(media.getRipples().getSurface(), 0);
            };

            const std::vector<float> once = field(1);
            const std::vector<float> twice = field(2);
            EXPECT_TRUE(std::ranges::any_of(once, [](float value) { return value != 0.0f; })) << "nothing was pressed";
            EXPECT_EQ(once, twice) << "a second trace of one placement pressed its footfall again";
        }
        /// **A field that has decayed is nought, the pass stops stepping it, and a press after is a press on still
        /// water.** A footfall and a thousand sixtieths, enough for every texel to fall under `RIPPLE_STILL`, which
        /// the field does after 842. The report of the last step is read on the next frame, which finds the field
        /// still and steps nothing. A footfall then leaves, thirty steps on, the tiles one footfall on a field just
        /// made leaves, to the bit: nothing of the first survived to be pressed on, and nothing the skipped frames did
        /// not do mattered.
        ///
        /// **The decay at `RIPPLE_FRAME_STEPS_MOST` sixtieths a frame**, each a step of one sixtieth as a frame of one
        /// takes, under one compose where four frames take four: every step reads and writes the whole field, and two
        /// thousand frames of one were three seconds on a low-end AMD integrated GPU. The footfall has a frame of one
        /// sixtieth to itself, as a press is as deep as the time its frame stands for.
        TEST_F(RtxRipplePassTest, aDecayedFieldIsStillAndIsPressedAsStillWater)
        {
            const double sixtieth = 1.0 / static_cast<double>(Shaders::RIPPLE_STEP_RATE);
            const std::array<RippleImpulse, 1> footfall{ RippleImpulse{
                .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f } };

            RipplePass fresh(getDevice());
            run(fresh, getPool(), footfall, 30);
            const std::vector<float> pressedOnce = Testing::readHalves(fresh.getSurface(), 0);

            RipplePass ripples(getDevice());
            run(ripples, getPool(), footfall, 1);
            constexpr std::uint32_t substeps = Shaders::RIPPLE_FRAME_STEPS_MOST;
            double clock = sixtieth;
            Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                for (std::uint32_t frame = 0; frame < 1000 / substeps; ++frame)
                {
                    clock += substeps * sixtieth;
                    ripples.record(commands, uploads, FrameSlot{ 0 }, {}, osg::Vec2f(0.0f, 0.0f), clock, nullptr);
                }
            });
            EXPECT_FALSE(ripples.isStill()) << "the report of a frame was read before its submit was done";

            Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                clock += sixtieth;
                ripples.record(commands, uploads, FrameSlot{ 0 }, {}, osg::Vec2f(0.0f, 0.0f), clock, nullptr);
            });
            ASSERT_TRUE(ripples.isStill()) << "a thousand sixtieths left the field moving";

            Testing::submitAndWaitWriting(getPool(), [&](VkCommandBuffer commands, Uploads& uploads) {
                for (std::int32_t step = 1; step <= 30; ++step)
                {
                    clock += sixtieth;
                    ripples.record(commands, uploads, FrameSlot{ 0 },
                        step == 1 ? std::span<const RippleImpulse>(footfall) : std::span<const RippleImpulse>(),
                        osg::Vec2f(0.0f, 0.0f), clock, nullptr);
                }
            });
            EXPECT_FALSE(ripples.isStill());
            EXPECT_EQ(Testing::readHalves(ripples.getSurface(), 0), pressedOnce);
        }
    }
}
