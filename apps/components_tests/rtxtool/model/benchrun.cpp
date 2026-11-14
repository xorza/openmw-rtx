#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/camerapath.hpp>
#include <apps/rtxtool/model/cameratrack.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A pair of integers is an exterior, spelt again as the world reads it, and anything else
        /// an interior's name**: `Balmora` is read indoors, as the film plans it, and not found out
        /// of doors as `coc` would. A cell the game stands in is spelt as `--cell` takes it.
        TEST(RtxBenchRunTest, aPairOfIntegersIsAnExteriorAndAnythingElseAnInteriorsName)
        {
            const CellAddress spaced(" 2 , -10");
            EXPECT_TRUE(spaced.isExterior());
            EXPECT_EQ(spaced.getSpelling(), "2,-10") << "the world reads no space around an integer";
            EXPECT_EQ(CellAddress("-3,-2"), CellAddress::exterior(-3, -2));

            for (const std::string_view name :
                { "Balmora", "Balmora, Guild of Mages", "-3", "1,2,3", ",", "1.5,2", "99999999999,0" })
            {
                const CellAddress cell(name);
                EXPECT_FALSE(cell.isExterior()) << name;
                EXPECT_EQ(cell.getSpelling(), name);
                EXPECT_EQ(cell, CellAddress::interior(name));
            }

            EXPECT_TRUE(CellAddress().empty());
            EXPECT_TRUE(CellAddress("").empty());
            EXPECT_FALSE(CellAddress::interior("1,2").isExterior()) << "the cell said it is an interior";
        }

        /// The rotation a stand hands the game's body, in the game's own angles.
        TEST(RtxBenchRunTest, aStandsRotationIsTheBodysYawClockwiseFromNorthAndPitchNegativeUp)
        {
            const osg::Vec3f eye(10.0f, 20.0f, 30.0f);

            const Stand north{ .mEye = eye, .mLook = eye + osg::Vec3f(0.0f, 100.0f, 0.0f) };
            EXPECT_EQ(north.getRotation(), osg::Vec3f(0.0f, 0.0f, 0.0f));

            const Stand east{ .mEye = eye, .mLook = eye + osg::Vec3f(100.0f, 0.0f, 0.0f) };
            EXPECT_NEAR(east.getRotation().z(), osg::PI_2, 1e-6);
            EXPECT_NEAR(east.getRotation().x(), 0.0f, 1e-6f);

            const Stand up{ .mEye = eye, .mLook = eye + osg::Vec3f(0.0f, 0.0f, 100.0f) };
            EXPECT_NEAR(up.getRotation().x(), -osg::PI_2, 1e-6);

            // A stand with no look faces north, as `getLook` says.
            Stand bare{ .mEye = eye };
            EXPECT_EQ(bare.getRotation(), osg::Vec3f(0.0f, 0.0f, 0.0f));

            // A look is taken over an eye and refused without one, where it would be dropped.
            EXPECT_TRUE(bare.lookAt(eye + osg::Vec3f(100.0f, 0.0f, 0.0f)));
            EXPECT_NEAR(bare.getRotation().z(), osg::PI_2, 1e-6);
            Stand blind{ .mCell = CellAddress("-2,-9") };
            EXPECT_FALSE(blind.lookAt(eye));
            EXPECT_FALSE(blind.mLook.has_value());
        }

        /// The ship at Seyda Neen, by hand: forward (-4411, 2767, -120) is 5208.4 long, so the yaw
        /// is atan2(-4411, 2767) = -1.01055 rad (302.1° from north) and the pitch is
        /// -asin(-120 / 5208.4) = +0.02304 rad, a climb of -1.32°.
        TEST(RtxBenchRunTest, theShipsStandFacesTheTownAndDipsALittle)
        {
            const Stand ship{
                .mEye = osg::Vec3f(-8292.0f, -73376.0f, 320.0f),
                .mLook = osg::Vec3f(-12703.0f, -70609.0f, 200.0f),
            };
            const osg::Vec3f rotation = ship.getRotation();

            EXPECT_NEAR(rotation.z(), -1.01055f, 1e-4f);
            EXPECT_NEAR(rotation.x(), 0.02304f, 1e-4f);
            EXPECT_EQ(rotation.y(), 0.0f);
        }

        /// What `CameraDriver::standWhereThePlayerIs` rebuilds from a body's rotation is the
        /// stand that rotated it. The ship again, so the two are checked on the same numbers:
        /// forward is (-4411, 2767, -120) / 5208.4 = (-0.8469, 0.5313, -0.0230).
        TEST(RtxBenchRunTest, aBodysRotationReadsBackAsTheStandThatMadeIt)
        {
            const Stand stand{
                .mEye = osg::Vec3f(-8292.0f, -73376.0f, 320.0f),
                .mLook = osg::Vec3f(-12703.0f, -70609.0f, 200.0f),
            };
            const osg::Vec3f forward = Stand::forwardOf(stand.getRotation());

            EXPECT_NEAR(forward.x(), -0.8469f, 1e-4f);
            EXPECT_NEAR(forward.y(), 0.5313f, 1e-4f);
            EXPECT_NEAR(forward.z(), -0.0230f, 1e-4f);
            EXPECT_NEAR(forward.length(), 1.0f, 1e-6f);

            EXPECT_EQ(Stand::forwardOf(osg::Vec3f()), osg::Vec3f(0.0f, 1.0f, 0.0f));
        }

        /// **A flight comes in from the stand's left and from behind it, and arrives on the last
        /// frame.** 150 units over thirty frames of a sixtieth of a second: twenty-nine steps, so
        /// 150 · 60 / 29 = 310.345 units a second. Facing north, the left is west and behind is
        /// south; facing east, the left is north. A negative walk starts in front, and a strafe and
        /// a walk of 150 each are one flight of 150 √2 = 212.132 units, at 438.894 a second.
        TEST(RtxBenchRunTest, aFlightComesInFromTheStandsLeftAndFromBehindAndArrivesOnTheLastFrame)
        {
            const Stand north{ .mCell = CellAddress("Balmora"),
                .mEye = osg::Vec3f(100.0f, 200.0f, 300.0f),
                .mLook = osg::Vec3f(100.0f, 1200.0f, 300.0f) };
            const Approach fromWest = north.approachFrom(150.0f, 0.0f, 1.0f / 60.0f, 30);
            EXPECT_EQ(fromWest.mFrom.mCell.getSpelling(), "Balmora");
            EXPECT_NEAR(fromWest.mFrom.mEye->x(), -50.0f, 1e-4f);
            EXPECT_NEAR(fromWest.mFrom.mEye->y(), 200.0f, 1e-4f);
            EXPECT_NEAR(fromWest.mFrom.mEye->z(), 300.0f, 1e-4f);
            EXPECT_EQ(fromWest.mFrom.mLook, north.mLook) << "it faces the stand's own point all the way";
            EXPECT_EQ(fromWest.mRoute.mTo, *north.mEye);
            EXPECT_EQ(fromWest.mRoute.mLookTo, *north.mLook);
            EXPECT_NEAR(fromWest.mRoute.mSpeed, 310.345f, 1e-3f);
            EXPECT_TRUE(fromWest.mRoute.mWorldHeld) << "the world the reference stands in, and not a walk";
            EXPECT_TRUE(fromWest.mFrom.mLamps);

            // **And under the stand's lamps**, or a place judged with its lamps off is flown into
            // with them on and held against a reference without them.
            Stand unlit = north;
            unlit.mLamps = false;
            EXPECT_FALSE(unlit.approachFrom(150.0f, 0.0f, 1.0f / 60.0f, 30).mFrom.mLamps);

            // **And through the same eye**: an armed stand is flown into armed.
            EXPECT_TRUE(fromWest.mFrom.mArms.empty());
            Stand armed = north;
            armed.mArms = "iron longsword";
            EXPECT_EQ(armed.approachFrom(150.0f, 0.0f, 1.0f / 60.0f, 30).mFrom.mArms, "iron longsword");

            const Stand east{ .mEye = osg::Vec3f(100.0f, 200.0f, 300.0f),
                .mLook = osg::Vec3f(1100.0f, 200.0f, 300.0f) };
            const osg::Vec3f fromNorth = *east.approachFrom(150.0f, 0.0f, 1.0f / 60.0f, 30).mFrom.mEye;
            EXPECT_NEAR(fromNorth.x(), 100.0f, 1e-4f);
            EXPECT_NEAR(fromNorth.y(), 350.0f, 1e-4f);

            // Along the level of the yaw, whatever the pitch: a stand looking down walks as far.
            EXPECT_NEAR((north.getLevelAhead() - osg::Vec3f(0.0f, 1.0f, 0.0f)).length(), 0.0f, 1e-6f);
            const Stand down{ .mEye = osg::Vec3f(100.0f, 200.0f, 300.0f),
                .mLook = osg::Vec3f(100.0f, 1200.0f, -700.0f) };
            const osg::Vec3f fromSouth = *down.approachFrom(0.0f, 150.0f, 1.0f / 60.0f, 30).mFrom.mEye;
            EXPECT_NEAR(fromSouth.x(), 100.0f, 1e-4f);
            EXPECT_NEAR(fromSouth.y(), 50.0f, 1e-4f);
            EXPECT_NEAR(fromSouth.z(), 300.0f, 1e-4f);
            EXPECT_NEAR(north.approachFrom(0.0f, -150.0f, 1.0f / 60.0f, 30).mFrom.mEye->y(), 350.0f, 1e-4f)
                << "a negative walk starts in front and walks back";

            // Twice as far in the same frames is twice as fast: the distance matters, and a strafe
            // and a walk add as the sides of a triangle.
            EXPECT_NEAR(north.approachFrom(300.0f, 0.0f, 1.0f / 60.0f, 30).mRoute.mSpeed, 620.690f, 1e-3f);
            EXPECT_NEAR(north.approachFrom(150.0f, 150.0f, 1.0f / 60.0f, 30).mRoute.mSpeed, 438.894f, 1e-3f);
        }

        /// **Every check has a row, and the row says when it may be asked.** A check with no name
        /// would print empty in the report and be unreachable from the command line; the four that
        /// depend on the stop's shape answer no where the stop cannot answer them.
        TEST(RtxBenchRunTest, everyCheckIsNamedAndSaysWhenItMayBeAsked)
        {
            const std::span<const Check> every = everyCheck();
            EXPECT_EQ(every.size(), 16u);
            for (const Check check : every)
                EXPECT_FALSE(checkName(check).empty()) << static_cast<int>(check);

            Stop still;
            still.mStand.mEye = osg::Vec3f(1.0f, 2.0f, 3.0f);
            Rtx::RenderProfile unheld;
            Rtx::RenderProfile held = unheld;
            held.mStressOverlapMs = 8.0;

            EXPECT_TRUE(canAsk(Check::WalkTwice, still, unheld));
            EXPECT_TRUE(canAsk(Check::CameraStands, still, unheld)) << "a still stop names its eye";
            EXPECT_FALSE(canAsk(Check::CrossingsAppend, still, unheld)) << "nothing to cross without a route";
            EXPECT_TRUE(canAsk(Check::FramesOverlap, still, unheld));
            EXPECT_FALSE(canAsk(Check::QueueHeld, still, unheld)) << "a hold nobody asked for";
            EXPECT_TRUE(canAsk(Check::QueueHeld, still, held));
            EXPECT_TRUE(canAsk(Check::Finite, still, unheld)) << "every frame counts what it wrote";
            EXPECT_TRUE(canAsk(Check::GatesAgree, still, unheld)) << "a place holds gated cells or holds none";
            EXPECT_TRUE(canAsk(Check::RoomsHeld, still, unheld)) << "every walk fills the mirror's maps";
            EXPECT_FALSE(canAsk(Check::SkyNamed, still, unheld)) << "no weather named, the world's own stands";

            // A take's flight leaves the camera where its track ends, with cells arriving under it.
            Stop tracked = still;
            const std::vector<TrackKey> keys{ TrackKey{ .mFrame = 0.0, .mEye = osg::Vec3f(1, 2, 3), .mRests = true },
                TrackKey{ .mFrame = 10.0, .mEye = osg::Vec3f(100, 2, 3), .mRests = true } };
            tracked.mSchedule.mTrack.emplace(keys, CameraPath(keys));
            EXPECT_FALSE(canAsk(Check::CameraStands, tracked, unheld)) << "the eye ends where the track does";
            EXPECT_FALSE(canAsk(Check::FramesOverlap, tracked, unheld)) << "an arrival drains the ring";
            EXPECT_TRUE(canAsk(Check::Finite, tracked, unheld));

            Stop stormy = still;
            stormy.mSky.mWeather = Rtx::Weather::Thunderstorm;
            EXPECT_TRUE(canAsk(Check::SkyNamed, stormy, unheld));
            stormy.mSky.mTurnThrough = { Rtx::Weather::Clear };
            EXPECT_FALSE(canAsk(Check::SkyNamed, stormy, unheld)) << "a turn stands under each weather in its time";

            Stop routed = still;
            routed.mSchedule.mRoute = Route{ .mTo = osg::Vec3f(100.0f, 0.0f, 0.0f), .mSpeed = 10.0f };
            EXPECT_TRUE(canAsk(Check::CrossingsAppend, routed, unheld));
            EXPECT_FALSE(canAsk(Check::FramesOverlap, routed, unheld)) << "an arrival drains the ring";
            EXPECT_FALSE(canAsk(Check::CameraStands, routed, unheld)) << "a route leaves the camera elsewhere";
            EXPECT_TRUE(canAsk(Check::Finite, routed, unheld));

            Stop flown = still;
            flown.mSchedule.mFreeCamera = true;
            EXPECT_FALSE(canAsk(Check::CameraStands, flown, unheld));

            Stop unplaced;
            EXPECT_FALSE(canAsk(Check::CameraStands, unplaced, unheld)) << "no eye named, nothing to stand at";

            // The check that asks what a second walk added is what makes one: a stop that asks it
            // and not the report still walks twice.
            Actions actions;
            EXPECT_FALSE(actions.walksTwice());
            // Pushed and not assigned a braced list: GCC 13 at -O2 under the sanitizers reads the
            // one-element copy as an overread (-Wstringop-overread), a false warning the build stops on.
            actions.mChecks.push_back(Check::Finite);
            EXPECT_FALSE(actions.walksTwice());
            actions.mChecks.push_back(Check::WalkTwice);
            EXPECT_TRUE(actions.walksTwice());
            actions.mChecks.clear();
            actions.mWalkTwice = true;
            EXPECT_TRUE(actions.walksTwice());
        }

        /// **A played run stops at the first frame that stored a value that was not finite**, with
        /// the frame and each module's count, and no other run does: a measured stop says it at its
        /// end, where `Check::Finite` is asked. A frame that stored none goes on either way.
        TEST(RtxBenchRunTest, aPlayedRunStopsAtTheFirstFrameThatStoredANaN)
        {
            Rtx::FrameResult clean;
            clean.mFrame = 41;

            Rtx::FrameResult stored;
            stored.mFrame = 42;
            stored.mNotFinite.add("shadowtiles.comp", 3);
            stored.mNotFinite.add("shadowfilter.comp", 7, Rtx::NotFiniteAt{ .mX = 812, .mY = 403 });

            SessionRequest played;
            played.mPlayed = true;
            EXPECT_EQ(played.stopAt(clean), std::nullopt);
            EXPECT_EQ(played.stopAt(stored),
                std::optional<std::string>("frame 42: 10 stores not finite: 3 in shadowtiles.comp, 7 in "
                                           "shadowfilter.comp (first at 812, 403)"));

            const SessionRequest measured;
            EXPECT_EQ(measured.stopAt(clean), std::nullopt);
            EXPECT_EQ(measured.stopAt(stored), std::nullopt);
        }

        /// **A kept frame reads as its images' firsts and their windows**: a row a line from the centre's
        /// radius above and left of it, a dash past the frame's edge, a texel's components as floats, a NaN
        /// as `nan`, or as words for an image of words; and a frame no image held one of as its images'
        /// lines alone.
        TEST(RtxBenchRunTest, aKeptFrameReadsAsItsImagesFirstsAndWindows)
        {
            constexpr std::uint32_t nan = 0x7fc00000u;
            Rtx::NotFiniteImage indirect{
                .mName = "g-indirect", .mWords = false, .mFirst = Rtx::NotFiniteAt{ .mX = 1, .mY = 0 }, .mTexels = {}
            };
            // 4, 4 of the window is its centre, 1, 0; 3, 4 of it is 0, 0, the frame's other pixel.
            indirect.mTexels[4 * 9 + 4]
                = { nan, std::bit_cast<std::uint32_t>(1.0f), 0, std::bit_cast<std::uint32_t>(1.0f) };
            indirect.mTexels[4 * 9 + 3] = { std::bit_cast<std::uint32_t>(0.5f), std::bit_cast<std::uint32_t>(1.0f),
                std::bit_cast<std::uint32_t>(2.0f), std::bit_cast<std::uint32_t>(3.0f) };
            Rtx::NotFiniteImage fast{
                .mName = "accumulate-fast", .mWords = true, .mFirst = std::nullopt, .mTexels = {}
            };
            fast.mTexels[4 * 9 + 4] = { nan + 920, 0, 0, 1 };
            Rtx::NotFiniteImage albedo{ .mName = "g-albedo", .mWords = false, .mFirst = std::nullopt, .mTexels = {} };

            Rtx::NotFiniteFrame kept{ .mFrame = 42,
                .mWidth = 2,
                .mHeight = 1,
                .mCentre = Rtx::NotFiniteAt{ .mX = 1, .mY = 0 },
                .mImages = { indirect, fast } };

            const std::string outside = ": - | - | - | - | - | - | - | - | -\n";
            std::string above;
            for (const int y : { -4, -3, -2, -1 })
                above += std::format("  y {}{}", y, outside);
            const std::string below = "  y 1" + outside + "  y 2" + outside + "  y 3" + outside + "  y 4" + outside;
            EXPECT_EQ(describeNotFiniteFrame(kept),
                "frame 42, 2 by 1, kept around 1, 0\n"
                "g-indirect: first not finite at 1, 0\n"
                "  from x -3:\n"
                    + above + "  y 0: - | - | - | 0.5 1 2 3 | nan 1 0 1 | - | - | - | -\n" + below
                    + "accumulate-fast: words, not searched\n"
                      "  from x -3:\n"
                    + above
                    + "  y 0: - | - | - | 0x00000000 0x00000000 0x00000000 0x00000000 | 0x7fc00398 0x00000000 "
                      "0x00000000 0x00000001 | - | - | - | -\n"
                    + below);

            kept.mCentre = std::nullopt;
            kept.mImages = { albedo, fast };
            EXPECT_EQ(describeNotFiniteFrame(kept),
                "frame 42, 2 by 1: no image held a value not finite, so no window was kept\n"
                "g-albedo: none not finite\n"
                "accumulate-fast: words, not searched\n");
        }

        /// **A run reserves room for its own upscaling mode and for every one a stop takes**, each
        /// once and in the enum's order: a run of the quality mode whose stops take the mode off
        /// twice and the native mode once reserves off, quality and native. A run whose stops take
        /// none reserves its own alone. The rest of the setup is the request's.
        TEST(RtxBenchRunTest, aRunReservesItsOwnUpscaleAndEveryOneAStopTakes)
        {
            SessionRequest request;
            request.mSetup.mRun.mProfile.mUpscale = Rtx::Upscale::Quality;
            request.mSetup.mRun.mMemoryBudget = 6004ull << 20;
            request.mStops.resize(5);
            EXPECT_EQ(request.rendererSetup().mRun.mReservedUpscales, std::vector{ Rtx::Upscale::Quality });

            request.mStops[0].mSchedule.mUpscale = Rtx::Upscale::Off;
            request.mStops[2].mSchedule.mUpscale = Rtx::Upscale::Native;
            request.mStops[4].mSchedule.mUpscale = Rtx::Upscale::Off;
            const MWRender::RunSetup setup = request.rendererSetup();
            EXPECT_EQ(setup.mRun.mReservedUpscales,
                (std::vector{ Rtx::Upscale::Off, Rtx::Upscale::Quality, Rtx::Upscale::Native }));
            EXPECT_EQ(setup.mRun.mProfile.mUpscale, Rtx::Upscale::Quality);
            EXPECT_EQ(setup.mRun.mMemoryBudget, std::optional<std::uint64_t>(6004ull << 20));
            EXPECT_TRUE(request.mSetup.mRun.mReservedUpscales.empty()) << "the request's own setup was changed";
        }

        /// **A run is refused under an upscaler that filters the light itself only where a stop asks
        /// for the light unfiltered at a mode that upscales**: the profile's request at the profile's
        /// mode, or a stop's own of either. Unfiltered with nothing upscaling, as `noise` traces its
        /// truth, stands under every upscaler, and nothing is refused under one that takes a filtered
        /// frame.
        TEST(RtxBenchRunTest, aRunAskingUnfilteredLightOfADenoisingUpscalerIsRefused)
        {
            SessionRequest request;
            request.mSetup.mRun.mProfile.mUpscale = Rtx::Upscale::Quality;
            request.mStops.resize(2);
            request.mStops[0].mName = "pier";
            request.mStops[1].mName = "pier-reference";
            request.mStops[1].mSchedule.mUpscale = Rtx::Upscale::Off;
            request.mStops[1].mSchedule.mReconstruction = Rtx::ReconstructionRequest{ .mDenoise = false };
            EXPECT_EQ(request.refusalUnder(Rtx::UpscalerInput::Noisy), std::nullopt)
                << "an unfiltered truth with nothing upscaling";

            request.mSetup.mRun.mProfile.mReconstruction.mDenoise = false;
            EXPECT_EQ(request.refusalUnder(Rtx::UpscalerInput::Filtered), std::nullopt);
            EXPECT_EQ(request.refusalUnder(Rtx::UpscalerInput::Noisy),
                std::optional<std::string>("pier asks for the light unfiltered at the quality mode, and the upscaler "
                                           "filters the light itself there: name --filter=true, --upscale=off or "
                                           "another --upscaler"));

            request.mSetup.mRun.mProfile.mReconstruction.mDenoise = true;
            request.mStops[1].mSchedule.mUpscale = Rtx::Upscale::Native;
            EXPECT_NE(request.refusalUnder(Rtx::UpscalerInput::Noisy), std::nullopt)
                << "a stop's own unfiltered request at a mode of its own that upscales";
            EXPECT_EQ(request.refusalUnder(Rtx::UpscalerInput::Filtered), std::nullopt);
        }

        /// **A stop's length in samples a shown pixel settles at the extents the renderer made, and every other
        /// stop keeps its own.** Nine frames of history at 2560 by 1440: NGX's quality traces 1707 by 960, so
        /// `9 * 1707 * 960 / (2560 * 1440)` = 4.0008 holds 4, where FSR's 1706 by 960 is 3.998 and holds 3;
        /// the 130 of a standing frame, held to at most 16, holds 16. Each held stop is then summed over what
        /// it came to, no longer held.
        TEST(RtxBenchRunTest, aHeldLengthSettlesAtTheExtentsTheRendererMade)
        {
            const auto settled = [](const std::uint32_t renderWidth) {
                SessionRequest request;
                request.mStops.resize(3);
                request.mStops[0].mSchedule.mHeld = HeldFrames{ .mAfter = 9, .mMost = 16 };
                request.mStops[1].mSchedule.mHeld = HeldFrames{ .mAfter = 130, .mMost = 16 };
                measureFrames(request.mStops[2], 30);
                const Rtx::FrameExtents made{
                    .mRenderWidth = renderWidth, .mRenderHeight = 960, .mOutputWidth = 2560, .mOutputHeight = 1440
                };
                request.settleUnder(made);
                for (const Stop& stop : request.mStops)
                    EXPECT_FALSE(stop.mSchedule.mHeld.has_value());
                EXPECT_EQ(request.mStops[1].mSchedule.mSpec.mRun.mFrames, 16u);
                EXPECT_EQ(request.mStops[1].mSchedule.mAccumulate, 16u);
                EXPECT_EQ(request.mStops[2].mSchedule.mSpec.mRun.mFrames, 30u) << "a stop of frames kept its own";
                EXPECT_EQ(request.mStops[2].mSchedule.mAccumulate, 0u);
                EXPECT_EQ(request.mStops[0].mSchedule.mAccumulate, request.mStops[0].mSchedule.mSpec.mRun.mFrames);
                return request.mStops[0].mSchedule.mSpec.mRun.mFrames;
            };

            EXPECT_EQ(settled(1707), 4u) << "NGX's quality";
            EXPECT_EQ(settled(1706), 3u) << "FSR's quality";
        }
    }
}
