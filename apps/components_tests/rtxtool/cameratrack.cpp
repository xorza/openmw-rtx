#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/rtxtool/model/camerapath.hpp>
#include <apps/rtxtool/model/cameratrack.hpp>
#include <apps/rtxtool/model/cruise.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr float sDegree = std::numbers::pi_v<float> / 180.0f;

        TrackKey keyAt(double frame, float x, float yawDegrees = 0.0f, float hour = 12.0f, std::uint32_t weather = 0,
            bool rests = false)
        {
            return TrackKey{ .mFrame = frame,
                .mEye = osg::Vec3f(x, 0.0f, 0.0f),
                .mRotation = osg::Vec3f(0.0f, 0.0f, yawDegrees * sDegree),
                .mHour = hour,
                .mWeather = weather,
                .mRests = rests };
        }

        CameraTrack trackOf(const std::vector<TrackKey>& keys, const Cruise cruise = {}, SkyRun sky = {})
        {
            return CameraTrack(keys, CameraPath(keys), cruise, std::move(sky));
        }

        /// **Two keys at one speed, eased from and to rest.** With no ease, a hundred units in ten
        /// frames is ten a frame from the first to the last. With an ease of four the flight takes
        /// `100 / v + 4 = 10`, so `v = 100 / 6`, and has covered `v · 4 · (0.5³ − 0.5⁴ / 2) = 6.25`
        /// half way through the first ease and `v · (5 − 2) = 50` half way through. The last key's
        /// pose stands past the end.
        TEST(RtxCameraTrackTest, twoKeysFlyAtOneSpeedEasedFromAndToRest)
        {
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f), keyAt(10, 100.0f) };
            const CameraTrack even = trackOf(keys);

            EXPECT_EQ(even.getFrames(), 11u);
            EXPECT_FLOAT_EQ(even.pose(0).mEye.x(), 0.0f);
            EXPECT_FLOAT_EQ(even.pose(2).mEye.x(), 20.0f);
            EXPECT_FLOAT_EQ(even.pose(5).mEye.x(), 50.0f);
            EXPECT_FLOAT_EQ(even.pose(10).mEye.x(), 100.0f);
            EXPECT_FLOAT_EQ(even.pose(20).mEye.x(), 100.0f);

            const CameraTrack eased = trackOf(keys, Cruise{ .mEase = 4.0 });
            EXPECT_FLOAT_EQ(eased.pose(2).mEye.x(), 6.25f);
            EXPECT_FLOAT_EQ(eased.pose(5).mEye.x(), 50.0f);
            EXPECT_FLOAT_EQ(eased.pose(8).mEye.x(), 93.75f);

            const std::vector<TrackKey> one{ keyAt(0, 7.0f) };
            const CameraTrack still = trackOf(one);
            EXPECT_EQ(still.getFrames(), 1u);
            EXPECT_FLOAT_EQ(still.pose(3).mEye.x(), 7.0f);
        }

        /// **A key the flight passes, it passes at speed.** Three keys ten frames and a hundred
        /// units apart with an ease of four: each leg rests at one end, so takes `100 / v + 2 = 10`,
        /// `v = 12.5` a frame, and a frame either side of the middle key the eye is 12.5 short of it
        /// and 12.5 past, 87.5 and 112.5. A key that rests stops the eye on it: both legs rest at
        /// both ends, `v = 100 / 6`, and a frame out of the stop the eye has covered
        /// `v · 4 · (0.25³ − 0.25⁴ / 2) = 0.911458`.
        TEST(RtxCameraTrackTest, aKeyIsPassedAtOneSpeedUnlessItRests)
        {
            const Cruise cruise{ .mEase = 4.0 };
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f), keyAt(10, 100.0f), keyAt(20, 200.0f) };
            const CameraTrack track = trackOf(keys, cruise);

            EXPECT_FLOAT_EQ(track.pose(10).mEye.x(), 100.0f);
            EXPECT_NEAR(track.pose(9).mEye.x(), 87.5f, 1e-4f);
            EXPECT_NEAR(track.pose(11).mEye.x(), 112.5f, 1e-4f);
            EXPECT_NEAR(track.pose(15).mEye.x(), 162.5f, 1e-4f) << "the cruise, 12.5 a frame";

            const std::vector<TrackKey> resting{ keyAt(0, 0.0f), keyAt(10, 100.0f, 0.0f, 12.0f, 0, true),
                keyAt(20, 200.0f) };
            const CameraTrack held = trackOf(resting, cruise);
            EXPECT_NEAR(held.pose(9).mEye.x(), 100.0f - 0.911458f, 1e-4f);
            EXPECT_NEAR(held.pose(11).mEye.x(), 100.0f + 0.911458f, 1e-4f);
        }

        /// **A key passed between two frames is passed at the flight's speed.** A hundred units in
        /// 7.5 frames and `12.5 · 40 / 3` more in 12.5, with no ease: `40 / 3` a frame on both legs,
        /// so frame 7 is `280 / 3` and frame 8 is a hundred and half a frame on, `320 / 3`.
        TEST(RtxCameraTrackTest, aKeyBetweenTwoFramesIsPassedAtSpeed)
        {
            constexpr float perFrame = 40.0f / 3.0f;
            const std::vector<TrackKey> keys{ keyAt(0.0, 0.0f), keyAt(7.5, 100.0f),
                keyAt(20.0, 100.0f + 12.5f * perFrame) };
            const CameraTrack track = trackOf(keys);

            EXPECT_EQ(track.getFrames(), 21u);
            EXPECT_NEAR(track.pose(7).mEye.x(), 7.0f * perFrame, 1e-3f);
            EXPECT_NEAR(track.pose(8).mEye.x(), 8.0f * perFrame, 1e-3f);
        }

        /// A flight into a pan on the spot: the pan goes nowhere, so the spot is a rest the flight
        /// reaches along its chord and does not pass, and the camera stands exactly at it for the
        /// whole pan while the yaw turns through it.
        TEST(RtxCameraTrackTest, aPanOnTheSpotDoesNotLeaveTheSpot)
        {
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f), keyAt(10, 1000.0f), keyAt(40, 1000.0f, 90.0f) };
            const CameraTrack track = trackOf(keys);

            for (std::uint32_t frame = 10; frame <= 40; ++frame)
                EXPECT_FLOAT_EQ(track.pose(frame).mEye.x(), 1000.0f) << "frame " << frame;

            for (std::uint32_t frame = 0; frame < 10; ++frame)
                EXPECT_LE(track.pose(frame).mEye.x(), 1000.0f) << "frame " << frame;

            EXPECT_NEAR(track.pose(25).mRotation.z(), 45.0f * sDegree, 1e-5f);
        }

        /// From 170° to −170° is twenty degrees east through south, not 340 back through north:
        /// halfway is due south, and no frame faces north of east or west.
        TEST(RtxCameraTrackTest, aTurnGoesTheShortWay)
        {
            EXPECT_NEAR(shortestTurn(170.0f * sDegree, -170.0f * sDegree), 20.0f * sDegree, 1e-6f);
            EXPECT_NEAR(shortestTurn(0.0f, 270.0f * sDegree), -90.0f * sDegree, 1e-6f);
            EXPECT_NEAR(shortestTurn(-10.0f * sDegree, 10.0f * sDegree), 20.0f * sDegree, 1e-6f);

            const std::vector<TrackKey> keys{ keyAt(0, 0.0f, 170.0f), keyAt(10, 0.0f, -170.0f) };
            const CameraTrack track = trackOf(keys);

            EXPECT_NEAR(std::cos(track.pose(5).mRotation.z()), -1.0f, 1e-6f);
            for (std::uint32_t frame = 0; frame <= 10; ++frame)
                EXPECT_LT(std::cos(track.pose(frame).mRotation.z()), -0.98f) << "frame " << frame;
        }

        /// 22:00 to 02:00 is four hours forward through midnight, so halfway is two hours on. A key
        /// at the same hour runs none, and a lesser hour runs to it the next day.
        TEST(RtxCameraTrackTest, theClockRunsForwardThroughMidnight)
        {
            EXPECT_FLOAT_EQ(hoursForward(5.0f, 5.0f), 0.0f);
            EXPECT_FLOAT_EQ(hoursForward(23.5f, 0.5f), 1.0f);
            EXPECT_FLOAT_EQ(hoursForward(1.0f, 23.0f), 22.0f);

            const std::vector<TrackKey> keys{ keyAt(0, 0.0f, 0.0f, 22.0f), keyAt(10, 0.0f, 0.0f, 2.0f) };
            const CameraTrack track = trackOf(keys);
            EXPECT_DOUBLE_EQ(track.pose(0).mHoursOn, 0.0);
            EXPECT_DOUBLE_EQ(track.pose(5).mHoursOn, 2.0);
            EXPECT_DOUBLE_EQ(track.pose(10).mHoursOn, 4.0);
        }

        /// 06:00, 07:00, 18:00, 18:00 at frames 0, 10, 20 and 60. Catmull-Rom's tangent at 07:00 is
        /// 12 / 20 = 0.6 an hour a frame against a secant of 0.1 before it, and the first segment
        /// would then run 4u³ − 3u² hours: a quarter of an hour backwards at u = 0.5. The circle of
        /// radius three scales the tangent by 3 / 6 to 0.3, and the segment runs u³ hours: an
        /// eighth at frame 5. No frame runs the clock backwards.
        TEST(RtxCameraTrackTest, theClockNeverRunsBackwards)
        {
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f, 0.0f, 6.0f), keyAt(10, 0.0f, 0.0f, 7.0f),
                keyAt(20, 0.0f, 0.0f, 18.0f), keyAt(60, 0.0f, 0.0f, 18.0f) };
            const CameraTrack track = trackOf(keys);

            EXPECT_NEAR(track.pose(5).mHoursOn, 0.125, 1e-12);
            for (std::uint32_t frame = 0; frame < 60; ++frame)
                EXPECT_LE(track.pose(frame).mHoursOn, track.pose(frame + 1).mHoursOn) << "frame " << frame;
            EXPECT_DOUBLE_EQ(track.pose(40).mHoursOn, 12.0);
        }

        /// A crossing of the sky eases as the camera does: smoothstep of 0.2 is 0.104. At a key the
        /// crossing into the next weather has not begun, and at the last the sky has arrived.
        TEST(RtxCameraTrackTest, theSkyCrossesBetweenTwoKeysWeathers)
        {
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f, 0.0f, 12.0f, 0), keyAt(10, 0.0f, 0.0f, 12.0f, 5),
                keyAt(20, 0.0f, 0.0f, 12.0f, 5) };
            const CameraTrack track = trackOf(keys);

            const TrackPose start = track.pose(0);
            EXPECT_EQ(start.mWeather, 0u);
            EXPECT_EQ(start.mNextWeather, 5u);
            EXPECT_FLOAT_EQ(start.mCrossed, 0.0f);

            EXPECT_FLOAT_EQ(track.pose(2).mCrossed, 0.104f);
            EXPECT_FLOAT_EQ(track.pose(5).mCrossed, 0.5f);

            const TrackPose steady = track.pose(15);
            EXPECT_EQ(steady.mWeather, 5u);
            EXPECT_EQ(steady.mNextWeather, 5u);
            EXPECT_FLOAT_EQ(steady.mCrossed, 0.0f);

            const TrackPose end = track.pose(20);
            EXPECT_EQ(end.mWeather, 5u);
            EXPECT_EQ(end.mNextWeather, 5u);
        }

        /// **A sky run writes the clock and the weather over the keys', counted in frames of the film,
        /// and leaves the camera alone.** Half an hour a frame; Cloudy, Rain and Snow, each standing
        /// two frames and crossing in four, so a period is six.
        ///
        /// By hand: frame 4 is 2 hours on, and four into the first period, two into the crossing of
        /// Cloudy into Rain: `smoothstep(2 / 4) = 0.5`. Frame 6 stands at Rain. Frame 17 is five
        /// into the third period, Snow crossing back round into Cloudy at `smoothstep(3 / 4)`,
        /// `0.5625 × 1.5 = 0.84375`. The eye halfway between two keys is still halfway. A take whose
        /// first frame is the film's tenth leads its clock by a frame, half an hour, and stands where
        /// the film's tenth does in the turn: four into the second period, Rain into Snow at a half.
        TEST(RtxCameraTrackTest, aSkyRunWritesTheClockAndTheWeatherOverTheKeys)
        {
            constexpr std::uint32_t cloudy = 1;
            constexpr std::uint32_t rain = 4;
            constexpr std::uint32_t snow = 8;
            const std::vector<TrackKey> keys{ keyAt(0, 0.0f, 0.0f, 12.0f, 0), keyAt(10, 100.0f, 0.0f, 18.0f, rain) };
            const SkyRun run{
                .mHoursPerFrame = 0.5, .mWeathers = { cloudy, rain, snow }, .mHoldFrames = 2, .mCrossingFrames = 4
            };
            const CameraTrack track = trackOf(keys, {}, run);

            const TrackPose first = track.pose(0);
            EXPECT_EQ(first.mHoursOn, 0.0);
            EXPECT_EQ(first.mWeather, cloudy);
            EXPECT_EQ(first.mNextWeather, cloudy);
            EXPECT_EQ(first.mCrossed, 0.0f);

            const TrackPose crossing = track.pose(4);
            EXPECT_EQ(crossing.mHoursOn, 2.0);
            EXPECT_EQ(crossing.mWeather, cloudy);
            EXPECT_EQ(crossing.mNextWeather, rain);
            EXPECT_FLOAT_EQ(crossing.mCrossed, 0.5f);
            EXPECT_NE(trackOf(keys).pose(4).mHoursOn, crossing.mHoursOn) << "the run changed no hour";

            EXPECT_EQ(track.pose(6).mWeather, rain);
            EXPECT_EQ(track.pose(6).mNextWeather, rain);

            const TrackPose round = track.pose(17);
            EXPECT_EQ(round.mWeather, snow);
            EXPECT_EQ(round.mNextWeather, cloudy);
            EXPECT_FLOAT_EQ(round.mCrossed, 0.84375f);

            EXPECT_FLOAT_EQ(track.pose(5).mEye.x(), 50.0f);
            EXPECT_EQ(track.pose(5).mEye, trackOf(keys).pose(5).mEye);

            SkyRun later = run;
            later.mFirstFrame = 10;
            const TrackPose cut = trackOf(keys, {}, later).pose(0);
            EXPECT_EQ(cut.mHoursOn, 0.5);
            EXPECT_EQ(cut.mWeather, rain);
            EXPECT_EQ(cut.mNextWeather, snow);
            EXPECT_FLOAT_EQ(cut.mCrossed, 0.5f);

            // Half a run is the keys' other half: the clock alone leaves the keys' weathers.
            const CameraTrack clockOnly = trackOf(keys, {}, SkyRun{ .mHoursPerFrame = 0.5 });
            EXPECT_EQ(clockOnly.pose(5).mWeather, 0u);
            EXPECT_EQ(clockOnly.pose(5).mNextWeather, rain);
            EXPECT_EQ(clockOnly.pose(5).mHoursOn, 2.5);
        }
    }
}
