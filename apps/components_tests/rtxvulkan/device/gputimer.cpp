#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

#include <gtest/gtest.h>

#include <volk.h>

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sSize = 64;

        /// A square across the view, far enough away to fill the frame.
        const std::array<osg::Vec3f, 4> sWallCorners{
            osg::Vec3f(-500.0f, 200.0f, -500.0f),
            osg::Vec3f(500.0f, 200.0f, -500.0f),
            osg::Vec3f(500.0f, 200.0f, 500.0f),
            osg::Vec3f(-500.0f, 200.0f, 500.0f),
        };

        /// That wall, on its own, as a scene.
        SceneDesc wall()
        {
            SceneDesc scene;
            Testing::addQuad(scene, sWallCorners);

            return scene;
        }

        /// The wall over a sheet of water, which is what makes a scene one the ripple field is
        /// stood for: the field is read where a ray meets water, so a scene with none stands none.
        SceneDesc wallOverWater()
        {
            SceneDesc scene = wall();
            Material water;
            water.mKind = MaterialKind::Water;
            Testing::addQuad(scene, Testing::sheetAt(1000.0f, -100.0f), scene.addMaterial(water));

            return scene;
        }

        bool reports(std::span<const GpuSpan> spans, FrameZone zone)
        {
            return std::any_of(spans.begin(), spans.end(), [&](const GpuSpan& span) { return span.mZone == zone; });
        }

        double totalOf(std::span<const GpuSpan> spans)
        {
            double sum = 0.0;
            for (const GpuSpan& span : spans)
                sum += span.mMs;

            return sum;
        }

        /// One frame's result and the wall clock around it.
        struct Drawn
        {
            std::uint32_t mHits = 0;
            /// From before the submit to after the wait: the whole of what the device did, and the
            /// CPU sat through, for this frame.
            double mWallMs = 0.0;
            GpuZones mGpu;
        };

        /// Draws one frame and waits for it, so what comes back is that frame's own report.
        Drawn draw(VulkanRenderer& renderer, Shaders::VisibilityConstants camera, double waterSeconds = 0.0,
            std::optional<ReconstructionRequest> reconstruction = std::nullopt)
        {
            camera.mWaterTime = splitSeconds(waterSeconds);
            const auto start = std::chrono::steady_clock::now();
            renderer.renderFrame(camera, FrameOptions{ .mReconstruction = reconstruction },
                WorldOptions{ .mWaterSeconds = waterSeconds });
            const std::optional<FrameResult> result = renderer.finishFrame();
            const double wallMs = since(start, std::chrono::steady_clock::now());

            EXPECT_TRUE(result.has_value()) << "a frame was submitted and nothing came back";
            if (!result.has_value())
                return Drawn{};

            return Drawn{ .mHits = result->mHits, .mWallMs = wallMs, .mGpu = result->mGpu };
        }

        /// A frame accounts for its own device time, pass by pass.
        ///
        /// **The thing a wall clock around the submit cannot do.** One `renderFrame` is a trace, a
        /// wavelet, a composite and two tone passes, and the CPU sees one number for all of them.
        /// What is asserted is that each is measured separately, that each is a real duration, and
        /// that together they fit inside the submit that contained them — which is the cross-check
        /// that says these are the device's clock and not something invented.
        struct RtxGpuTimerTest : Testing::RendererTest
        {
        };

        TEST_F(RtxGpuTimerTest, aFrameAccountsForItsOwnDeviceTimePassByPass)
        {
            mRenderer.resize(sSize, sSize);

            SceneDesc scene = wall();
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            const Shaders::VisibilityConstants camera
                = Testing::makeCamera(osg::Vec3f(), osg::Vec3f(0.0f, 100.0f, 0.0f), 60.0f, sSize, sSize, 10000.0f);

            const Drawn drawn = draw(mRenderer, camera);
            if (drawn.mGpu.spans().empty())
                GTEST_SKIP() << "this device cannot write timestamps";

            // The passes every frame records, whatever it is drawing. `filter` is here too — the
            // shared renderer does not upscale, so the wavelet runs — and is left out of the list
            // because a build without it is not a failure of this. The composite is not: the trace
            // composes a frame nothing filters, and the cascade's last level one it filters, so the
            // composite runs only to add a frame to a sum.
            for (const FrameZone pass : { FrameZone::Trace, FrameZone::Exposure, FrameZone::Glare, FrameZone::Tone })
                EXPECT_TRUE(reports(drawn.mGpu.spans(), pass)) << "no zone called " << sFrameZoneNames.name(pass);

            // **And the sea is not among them where the frame has none.** `makeCamera` names no
            // water, so nothing can sample the wave tiles and nothing should synthesise them; a
            // frame that does name a level pays for them once, before the trace — at a moment of
            // the water's clock no test of the shared renderer stood at, since tiles that already
            // hold a frame's moment are read as they stand (`WavePass::holds`).
            EXPECT_FALSE(reports(drawn.mGpu.spans(), FrameZone::Waves)) << "a dry frame synthesised the sea";

            Shaders::VisibilityConstants flooded = camera;
            flooded.mWaterLevel = 0.0f;
            constexpr double moment = 7919.25;
            const Drawn wet = draw(mRenderer, flooded, moment);
            EXPECT_TRUE(reports(wet.mGpu.spans(), FrameZone::Waves)) << "a frame with water in it synthesised no sea";
            EXPECT_EQ(wet.mGpu.spans().front().mZone, FrameZone::Waves)
                << "the sea was synthesised somewhere other than before the trace";
            EXPECT_FALSE(reports(draw(mRenderer, flooded, moment).mGpu.spans(), FrameZone::Waves))
                << "a second frame at the same moment synthesised the same sea again";

            for (const GpuSpan& span : drawn.mGpu.spans())
            {
                EXPECT_GT(span.mMs, 0.0) << sFrameZoneNames.name(span.mZone) << " took no time at all";
                EXPECT_LT(span.mMs, 1000.0)
                    << sFrameZoneNames.name(span.mZone) << " took a second, which is a clock read wrong";
            }

            // **The containment check, which is what makes these numbers rather than noise.** Every
            // zone was recorded inside the submit `draw` waited out, and the zones do not overlap —
            // so their sum is device work the CPU also sat through, and the CPU also paid for the
            // submit itself.
            EXPECT_LT(totalOf(drawn.mGpu.spans()), drawn.mWallMs)
                << "the passes add up to more device time than the frame that held them took";

            // **A frame that placed the world says so, and one that did not, does not.** The
            // structure builds happen in submits of their own before the frame's, and the whole
            // point of carrying them in the same report is that they are the same frame's cost.
            EXPECT_FALSE(reports(drawn.mGpu.spans(), FrameZone::Tlas)) << "nothing was placed, so nothing was built";
            EXPECT_FALSE(reports(drawn.mGpu.spans(), FrameZone::Puffs))
                << "a frame no puff can be met in composited puffs over every pixel as it found it";

            mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
            const Drawn placed = draw(mRenderer, camera);

            EXPECT_TRUE(reports(placed.mGpu.spans(), FrameZone::Tlas))
                << "the top level was rebuilt and went unmeasured";
            EXPECT_GT(placed.mGpu.spans().size(), drawn.mGpu.spans().size()) << "placing the world added no zone";

            // In the order the work was recorded, which is what lets a reader see the frame rather
            // than a bag of numbers: the tight copy of the wall `setScene` built, whose answer the
            // timeline says is readable by now, and then the top level over it. Nothing before
            // the copy, because a placement is what a frame opens with.
            const std::span<const GpuSpan> zones = placed.mGpu.spans();
            const auto compact = std::ranges::find(zones, FrameZone::Compact, &GpuSpan::mZone);
            const auto tlas = std::ranges::find(zones, FrameZone::Tlas, &GpuSpan::mZone);
            ASSERT_NE(tlas, zones.end());
            EXPECT_TRUE(compact == zones.end() || compact < tlas) << "the top level was built before the copy it names";
            EXPECT_EQ(zones.front().mZone, compact == zones.end() ? FrameZone::Tlas : FrameZone::Compact);

            // And the report does not accumulate: the frame after is its own again. Placed again
            // unchanged into the other frame's copy, which owes the rows the top level was built
            // over the frame before, so there is nothing to build.
            scene.placements().advance();
            mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
            const Drawn after = draw(mRenderer, camera);
            EXPECT_EQ(after.mGpu.spans().size(), drawn.mGpu.spans().size())
                << "last frame's zones were carried into this one";
            EXPECT_FALSE(reports(after.mGpu.spans(), FrameZone::Tlas))
                << "the top level was built again over the rows it was built over the frame before";

            // **A cell arriving says so too, and that is the frame worth having a figure for.** The
            // structures its meshes bring are recorded ahead of the placement and ride its submit,
            // so without a bracket of their own they are device time the frame's fence carries and
            // no zone accounts for — which is exactly the frame a player feels.
            Testing::addQuad(scene, sWallCorners, std::nullopt, osg::Matrixf::translate(0.0f, -50.0f, 0.0f));

            mRenderer.extendScene(Rtx::SceneSlot::world(), scene, {});
            mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
            const Drawn arrived = draw(mRenderer, camera);

            EXPECT_TRUE(reports(arrived.mGpu.spans(), FrameZone::Blas))
                << "a mesh arrived and its structure was built unmeasured";

            // First, because the builds run before the top level that names what they built, and a
            // duration rather than a bracket that closed on itself.
            EXPECT_EQ(arrived.mGpu.spans().front().mZone, FrameZone::Blas);
            EXPECT_GT(arrived.mGpu.spans().front().mMs, 0.0) << "the arrival's builds took no time at all";

            // And only on the frame the arrival landed in.
            const Drawn settled = draw(mRenderer, camera);
            EXPECT_FALSE(reports(settled.mGpu.spans(), FrameZone::Blas)) << "nothing arrived, so nothing was built";

            // **A structure that arrives builds the top level though no row moved**: a slot handed
            // out again can land its new structure at an address a buried one gave back, under rows
            // the same to the byte. A mesh nothing places is that arrival with no row at all.
            scene.placements().advance();
            scene.clearArrivals();
            Testing::addQuadMesh(scene);
            mRenderer.extendScene(Rtx::SceneSlot::world(), scene, {});
            mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
            const Drawn unplaced = draw(mRenderer, camera);
            EXPECT_TRUE(reports(unplaced.mGpu.spans(), FrameZone::Blas));
            EXPECT_TRUE(reports(unplaced.mGpu.spans(), FrameZone::Tlas))
                << "a structure arrived and the top level kept the bounds of what stood before";

            // **A denoised frame runs every pass of the bounce**: the denoisers' stages, the temporal
            // passes and the clamps, and each of the wavelet's levels open a zone, in that order — the
            // shadow filter's levels between the clamps and the wavelet, where a source lights.
            ReconstructionRequest filtered = mRenderer.getProfile().mReconstruction;
            filtered.mDenoise = true;
            const Drawn traced = draw(mRenderer, camera, 0.0, filtered);
            EXPECT_FALSE(reports(traced.mGpu.spans(), FrameZone::Composite))
                << "a denoised frame nothing sums was composed in a pass of its own, after its cascade";
            const std::span<const GpuSpan> stages = traced.mGpu.spans();
            const auto stageAt = [&](FrameZone zone) { return std::ranges::find(stages, zone, &GpuSpan::mZone); };
            for (const FrameZone pass : { FrameZone::Temporal, FrameZone::Clamp, FrameZone::Filter0, FrameZone::Filter1,
                     FrameZone::Filter2, FrameZone::Filter3 })
                EXPECT_NE(stageAt(pass), stages.end()) << "no zone called " << sFrameZoneNames.name(pass);
            EXPECT_LT(stageAt(FrameZone::Temporal), stageAt(FrameZone::Clamp));
            EXPECT_LT(stageAt(FrameZone::Clamp), stageAt(FrameZone::Filter0));
            if (stageAt(FrameZone::Shadow) != stages.end())
            {
                EXPECT_LT(stageAt(FrameZone::Clamp), stageAt(FrameZone::Shadow));
                EXPECT_LT(stageAt(FrameZone::Shadow), stageAt(FrameZone::Filter0));
            }

            // **The ripple field is stood for a scene that holds water and stepped only where the
            // sky's clock has moved and something presses it**, before the sea reads it. The frame
            // the field is stood on steps nothing and reports no zone; a sixtieth on, the footfall
            // the scene holds is pressed and the step comes first of all, ahead of the sea.
            SceneDesc flooding = wallOverWater();
            flooding.addRipple(RippleImpulse{ .mAt = osg::Vec2f(0.0f, 0.0f), .mSize = 12.0f });
            mRenderer.setScene(Rtx::SceneSlot::world(), flooding, {});

            Shaders::VisibilityConstants standing = flooded;
            standing.mWaterLevel = -100.0f;
            const Drawn stood = draw(mRenderer, standing);
            EXPECT_FALSE(reports(stood.mGpu.spans(), FrameZone::Ripples))
                << "a frame with no step due stepped the field";

            // Placed again, as the game places every frame: the stood frame spent what the setting
            // kept, on a clock that had not moved.
            mRenderer.placeScene(Rtx::SceneSlot::world(), flooding);
            const Drawn stepped = draw(mRenderer, standing, 1.0 / 60.0);
            EXPECT_TRUE(reports(stepped.mGpu.spans(), FrameZone::Ripples))
                << "a sixtieth on, the field was not stepped";
            // First of the frame's own work: the placement's structures go ahead of it, and
            // placing the scene again compacts what setting it built.
            constexpr std::array sPlacement{ FrameZone::Blas, FrameZone::Compact, FrameZone::Refit, FrameZone::Tlas,
                FrameZone::Skin, FrameZone::Ground };
            const auto placement
                = [&](const GpuSpan& span) { return std::ranges::find(sPlacement, span.mZone) != sPlacement.end(); };
            const auto first = std::ranges::find_if_not(stepped.mGpu.spans(), placement);
            ASSERT_NE(first, stepped.mGpu.spans().end());
            EXPECT_EQ(first->mZone, FrameZone::Ripples) << "the field was stepped somewhere other than before the sea";

            // **A surface with no level is a sea as much as a level with no surface**, and the
            // trace samples the tiles wherever a ray meets the water: a frame that synthesised them
            // for the level alone left this one reading the tiles of whichever frame last had one.
            Shaders::VisibilityConstants dry = standing;
            dry.mWaterLevel = camera.mWaterLevel;
            const Drawn surfaced = draw(mRenderer, dry, 2.0 / 60.0);
            EXPECT_TRUE(reports(surfaced.mGpu.spans(), FrameZone::Waves))
                << "a water surface with no level synthesised no sea";
            EXPECT_TRUE(reports(surfaced.mGpu.spans(), FrameZone::Ripples))
                << "a water surface with no level stepped no field";
        }

        /// A device alone, because a death test's child stands its fixture again, and a renderer's
        /// is seconds of it.
        using RtxGpuTimerReadTest = Testing::DeviceTest;

        /// **A zone no submit wrote ends the process and names the zone**, where the read waited
        /// for it without end. Its queries reset on the device and the zone recorded into a buffer
        /// that is ended and never submitted: what a zone in a discarded batch leaves.
        TEST_F(RtxGpuTimerReadTest, aZoneNoSubmitWroteEndsTheProcessNamingIt)
        {
            const Device& device = *mHarness.mDevice;
            if (device.getPhysicalDevice().getTimestampBits() == 0)
                GTEST_SKIP() << "this device cannot write timestamps";

            GpuTimer timer(device, true);
            device.getPool().submitAndWait(
                [&](VkCommandBuffer commands) { vkCmdResetQueryPool(commands, timer.getQueryPool(), 0, 2); });

            timer.beginFrame();
            const LentCommands lent = device.getPool().lend(1);
            Recording never = device.getPool().begin(lent[0]);
            {
                const GpuZone timed(&timer, never.get(), FrameZone::Trace);
            }
            std::move(never).end();

            GpuZones zones;
            Testing::expectDies([&] { timer.resolve(zones); },
                "the GPU timer's trace zone was resolved before a submit wrote its timestamps");
        }

        /// **A timer made not to time writes no timestamp and reports no zone**, for a session
        /// nobody reads a report of (`RendererOptions::mTiming`): no query pool, and a zone recorded
        /// and submitted resolves to nothing. The same zone through a timer that times is one span.
        TEST_F(RtxGpuTimerReadTest, aTimerMadeNotToTimeReportsNoZone)
        {
            const Device& device = *mHarness.mDevice;
            const auto zonesOf = [&](bool timing) {
                GpuTimer timer(device, timing);
                timer.beginFrame();
                device.getPool().submitAndWait(
                    [&](VkCommandBuffer commands) { const GpuZone timed(&timer, commands, FrameZone::Trace); });
                GpuZones zones;
                timer.resolve(zones);
                return zones.spans().size();
            };

            EXPECT_EQ(GpuTimer(device, false).getQueryPool(), VK_NULL_HANDLE)
                << "a pool for a timer that never reads it";
            EXPECT_EQ(zonesOf(false), 0u) << "a timer made not to time reported a zone";
            if (device.getPhysicalDevice().getTimestampBits() > 0)
            {
                EXPECT_EQ(zonesOf(true), 1u) << "a timer that times lost its zone";
            }
        }

        /// **A frame that opens more zones than `sMaxGpuZones` counts ends the process**, where the
        /// zones past the pool were left out of its report without a word.
        TEST_F(RtxGpuTimerReadTest, aFrameThatOpensMoreZonesThanCountedDies)
        {
            const Device& device = *mHarness.mDevice;
            Testing::expectAssertDies(
                [&] {
                    GpuTimer timer(device, false);
                    timer.beginFrame();
                    const LentCommands lent = device.getPool().lend(1);
                    Recording recording = device.getPool().begin(lent[0]);
                    for (std::uint32_t zone = 0; zone <= sMaxGpuZones; ++zone)
                        const GpuZone timed(&timer, recording.get(), FrameZone::Trace);
                    std::move(recording).end();
                },
                "a frame opened more zones than `sMaxGpuZones` counts");
        }
    }
}
