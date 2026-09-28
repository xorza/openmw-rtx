#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec2d>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <apps/rtxtool/film.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/cameratrack.hpp>
#include <apps/rtxtool/run.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/environment/skylight.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        std::vector<FilmKey> read(const std::string& text)
        {
            std::istringstream in(text);
            return readKeys(in, "tour.keys");
        }

        std::string refusal(const std::string& text)
        {
            try
            {
                read(text);
            }
            catch (const std::runtime_error& error)
            {
                return error.what();
            }
            return "nothing was refused";
        }

        FilmKey keyAt(std::string name, std::string cell, osg::Vec3f eye, osg::Vec3f look = osg::Vec3f(0, 1e4f, 0))
        {
            return FilmKey{ .mStop = Stop{ .mName = std::move(name),
                                .mStand = { .mCell = std::move(cell), .mEye = eye, .mLook = eye + look },
                                .mSky = { .mHour = sDefaultHour, .mWeather = std::string(sDefaultWeather) } } };
        }

        /// Ten frames a second, a hundred units a second, and a frame whose horizontal field is
        /// exactly ninety degrees: `2·atan(tan(30°)·√3)` is `2·atan(1)`. A quarter turn is then one
        /// image width, seven seconds, and a pitch of thirty degrees half an image height, 3.5. No
        /// ease, so a flight takes its length at the speed and nothing else; the test of the ease
        /// names one.
        FilmPacing pacingForTests()
        {
            FilmPacing pacing;
            pacing.mStep = 0.1f;
            pacing.mSpeed = 100.0f;
            pacing.mEase = 0.0f;
            pacing.mAspect = std::sqrt(3.0f);
            return pacing;
        }

        /// What Home prints, pasted whole, reads back as the place it printed: the comment lines go,
        /// the hour and the weather the block leaves out are the file's own, and two keys may share
        /// a name. What `view --keys` appends reads back with its day as well.
        TEST(RtxFilmTest, homeOutputPastedIsKeys)
        {
            RtxTool::Stop dawn{ .mName = "balmora",
                .mStand = { .mCell = "-3,-2",
                    .mEye = osg::Vec3f(-18075.145f, -17586.46f, 638.41016f),
                    .mLook = osg::Vec3f(-18625.098f, -16765.438f, 485.20374f) },
                .mSky = { .mHour = 6.5f,
                    .mDay = 2,
                    .mWeather = "Overcast",
                    .mAir = Rtx::AirClock{ .mSky = { .mSeconds = 36000.123456789, .mCloudScroll = 0.5f },
                        .mCarried = osg::Vec2d(-123456.78901234, 0.1) } } };
            RtxTool::Stop noon = dawn;
            noon.mSky = { .mHour = 12.0f, .mDay = 0, .mWeather = "Clear" };

            const std::vector<FilmKey> keys = read(describeStanding(dawn) + describeStanding(noon) + describeKey(dawn));
            ASSERT_EQ(keys.size(), 3u);

            EXPECT_EQ(keys[0].mStop.mName, "balmora");
            EXPECT_EQ(keys[1].mStop.mName, "balmora");
            EXPECT_EQ(keys[0].getCell(), "-3,-2");
            EXPECT_EQ(keys[0].getEye(), *dawn.mStand.mEye);
            EXPECT_EQ(keys[0].getLook(), *dawn.mStand.mLook);
            EXPECT_EQ(keys[0].getHour(), 6.5f);
            EXPECT_EQ(keys[0].getWeather(), "Overcast");
            EXPECT_FALSE(keys[0].mStop.mSky.mDay.has_value())
                << "the block has no day; the command line beside it is a comment";

            ASSERT_TRUE(keys[0].mStop.mSky.mAir.has_value()) << "a take begins in the air its first key saw";
            EXPECT_EQ(keys[0].mStop.mSky.mAir->mSky.mSeconds, dawn.mSky.mAir->mSky.mSeconds);
            EXPECT_EQ(keys[0].mStop.mSky.mAir->mSky.mCloudScroll, dawn.mSky.mAir->mSky.mCloudScroll);
            EXPECT_EQ(keys[0].mStop.mSky.mAir->mCarried, dawn.mSky.mAir->mCarried);
            EXPECT_FALSE(keys[1].mStop.mSky.mAir.has_value());

            EXPECT_EQ(keys[1].getHour(), sDefaultHour);
            EXPECT_EQ(keys[1].getWeather(), sDefaultWeather);

            EXPECT_EQ(keys[2].getEye(), *dawn.mStand.mEye);
            EXPECT_EQ(keys[2].getHour(), 6.5f);
            EXPECT_EQ(keys[2].mStop.mSky.mDay, 2);
            EXPECT_EQ(keys[2].getWeather(), "Overcast");

            const std::vector<FilmKey> timed
                = read("[a]\ncell = 0,0\npos = 1,2,3\nlook = 4,5,6\nseconds = 2.5\nhold = 1\ncut = false\n");
            EXPECT_EQ(timed[0].mSeconds, 2.5f);
            EXPECT_EQ(timed[0].mHold, 1.0f);
            EXPECT_EQ(timed[0].mCut, false);
        }

        /// A key misread is a film of somewhere else, so every malformed line is refused by its
        /// number.
        TEST(RtxFilmTest, aMalformedKeyIsRefusedByItsLine)
        {
            const std::string place = "[a]\ncell = 0,0\npos = 1,2,3\nlook = 4,5,6\n";
            EXPECT_EQ(refusal(place + "speed = 3\n"), "tour.keys:5: a key has no field called \"speed\"");
            EXPECT_EQ(
                refusal(place + "hour = 24\n"), "tour.keys:5: hour \"24\" is not from 0 up to but not including 24");
            EXPECT_EQ(refusal(place + "weather = Drizzle\n"),
                "tour.keys:5: weather \"Drizzle\" is none of the weathers the content files name");
            EXPECT_EQ(refusal(place + "seconds = 0\n"), "tour.keys:5: seconds \"0\" is not a length of time");
            EXPECT_EQ(refusal(place + "cut = yes\n"), "tour.keys:5: cut \"yes\" is not true or false");
            EXPECT_EQ(
                refusal(place + "day = -1\n"), "tour.keys:5: day \"-1\" is not a whole number of days from nought");
            EXPECT_EQ(refusal("cell = 0,0\n"), "tour.keys:1: a field comes before the first [section]");
            EXPECT_EQ(refusal("# a comment\n\n[a]\ncell = 0,0\npos = 1,2,3\n[b]\n"),
                "tour.keys:3: key \"a\" names no pos and look");
            EXPECT_EQ(refusal(place + "[b\n"), "tour.keys:5: a section's name is not closed by ]");
            EXPECT_EQ(
                refusal(place + "pos = 1,2\n"), "tour.keys:5: pos \"1,2\" is not three numbers separated by commas")
                << "quoted whole, and not from where the reading stopped";
            EXPECT_EQ(refusal("[a]\ncell = 0,0\npos =\nlook = 1,2,3\n"),
                "tour.keys:3: pos \"\" is not three numbers separated by commas")
                << "an empty point is no point, and not one left unsaid";
            EXPECT_EQ(refusal("# nothing\n"), "tour.keys states no keys");
        }

        TEST(RtxFilmTest, anExteriorIsAPairOfIntegers)
        {
            EXPECT_TRUE(isExteriorCell("-3,-2"));
            EXPECT_TRUE(isExteriorCell(" 2 , -10"));
            EXPECT_FALSE(isExteriorCell("Balmora, Guild of Mages"));
            EXPECT_FALSE(isExteriorCell("-3"));
            EXPECT_FALSE(isExteriorCell("1,2,3"));
            EXPECT_FALSE(isExteriorCell(","));
        }

        /// A key flies from the one before it where both are in one space and near, and cuts
        /// otherwise; a key says so itself where it wants the other.
        TEST(RtxFilmTest, aKeyTooFarOrElsewhereIsACut)
        {
            std::vector<FilmKey> keys{
                keyAt("start", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("near", "0,0", osg::Vec3f(1000, 0, 0)),
                keyAt("inside", "Balmora, Guild of Mages", osg::Vec3f(0, 0, 0)),
                keyAt("same-room", "Balmora, Guild of Mages", osg::Vec3f(500, 0, 0)),
                keyAt("other-room", "Vivec, Arena", osg::Vec3f(500, 0, 0)),
                keyAt("outside", "2,2", osg::Vec3f(0, 0, 0)),
                keyAt("far", "4,2", osg::Vec3f(20000, 0, 0)),
                keyAt("asked", "4,2", osg::Vec3f(20100, 0, 0)),
                keyAt("forbidden", "9,9", osg::Vec3f(90000, 0, 0)),
            };
            keys[7].mCut = true;
            keys[8].mCut = false;

            const FilmPlan plan = planFilm(keys, pacingForTests());
            ASSERT_EQ(plan.mTakes.size(), 6u);

            const auto take = [&](std::size_t at) { return std::pair(plan.mTakes[at].mFirst, plan.mTakes[at].mEnd); };
            EXPECT_EQ(take(0), (std::pair<std::size_t, std::size_t>(0, 2)));
            EXPECT_EQ(take(1), (std::pair<std::size_t, std::size_t>(2, 4)));
            EXPECT_EQ(take(2), (std::pair<std::size_t, std::size_t>(4, 5)));
            EXPECT_EQ(take(3), (std::pair<std::size_t, std::size_t>(5, 6)));
            EXPECT_EQ(take(4), (std::pair<std::size_t, std::size_t>(6, 7)));
            EXPECT_EQ(take(5), (std::pair<std::size_t, std::size_t>(7, 9)));

            EXPECT_EQ(plan.mTakes[0].mCut, FilmCut::First);
            EXPECT_EQ(plan.mTakes[1].mCut, FilmCut::Indoors);
            EXPECT_EQ(plan.mTakes[2].mCut, FilmCut::Interior);
            EXPECT_EQ(plan.mTakes[3].mCut, FilmCut::Outdoors);
            EXPECT_EQ(plan.mTakes[4].mCut, FilmCut::Distance);
            EXPECT_FLOAT_EQ(plan.mTakes[4].mJump, 20000.0f);
            EXPECT_EQ(plan.mTakes[5].mCut, FilmCut::Asked);
        }

        /// A flight takes its length at the speed, and a segment that goes nowhere the longest of
        /// what else it changes, at ten frames a second: a thousand units at a hundred a second is
        /// ten seconds; a quarter turn one image width, seven; a pitch of thirty degrees half an
        /// image height, 3.5; three hours at two seconds each, six; a weather crossing, eight;
        /// nothing at all, a still of four; and a key's own seconds over any of them.
        TEST(RtxFilmTest, aFlightTakesItsLengthAndASegmentThatGoesNowhereTheLongestOfItsChanges)
        {
            const osg::Vec3f spot(0, 0, 0);
            const osg::Vec3f north(0, 1e4f, 0);
            const osg::Vec3f east(1e4f, 0, 0);
            const float climb = 1e4f * std::tan(osg::DegreesToRadians(30.0f));
            const osg::Vec3f up(0, 1e4f, climb);
            const osg::Vec3f eastUp(1e4f, 0, climb);
            const osg::Vec3f there(1000, 0, 0);

            std::vector<FilmKey> keys{
                keyAt("a", "0,0", spot, north),
                keyAt("flown", "0,0", there, north),
                keyAt("turned", "0,0", there, east),
                keyAt("tilted", "0,0", there, eastUp),
                keyAt("later", "0,0", there, eastUp),
                keyAt("rain", "0,0", there, eastUp),
                keyAt("same", "0,0", there, eastUp),
                keyAt("given", "0,0", osg::Vec3f(5000, 0, 0), up),
            };
            keys[3].mStop.mSky.mHour = 12.0f;
            for (std::size_t at = 4; at < keys.size(); ++at)
                keys[at].mStop.mSky.mHour = 15.0f;
            for (std::size_t at = 5; at < keys.size(); ++at)
                keys[at].mStop.mSky.mWeather = "Rain";
            keys[7].mSeconds = 2.5f;

            const FilmPlan plan = planFilm(keys, pacingForTests());
            ASSERT_EQ(plan.mTakes.size(), 1u);
            const std::vector<FilmSegment>& segments = plan.mTakes[0].mSegments;
            ASSERT_EQ(segments.size(), 7u);

            EXPECT_DOUBLE_EQ(segments[0].mFrames, 100.0);
            EXPECT_EQ(segments[0].mPace, FilmPace::Distance);
            EXPECT_DOUBLE_EQ(segments[1].mFrames, 70.0);
            EXPECT_EQ(segments[1].mPace, FilmPace::Turn);
            EXPECT_DOUBLE_EQ(segments[2].mFrames, 35.0);
            EXPECT_EQ(segments[2].mPace, FilmPace::Turn);
            EXPECT_DOUBLE_EQ(segments[3].mFrames, 60.0);
            EXPECT_EQ(segments[3].mPace, FilmPace::Clock);
            EXPECT_DOUBLE_EQ(segments[4].mFrames, 80.0);
            EXPECT_EQ(segments[4].mPace, FilmPace::Weather);
            EXPECT_DOUBLE_EQ(segments[5].mFrames, 40.0);
            EXPECT_EQ(segments[5].mPace, FilmPace::Still);
            EXPECT_DOUBLE_EQ(segments[6].mFrames, 25.0);
            EXPECT_EQ(segments[6].mPace, FilmPace::Given);

            // Forty units and three hours: the flight's 0.4 s, and the clock's six seconds are what
            // it asks and does not get.
            std::vector<FilmKey> both{ keyAt("a", "0,0", spot), keyAt("b", "0,0", osg::Vec3f(40, 0, 0)) };
            both[1].mStop.mSky.mHour = 15.0f;
            const FilmSegment hurried = planFilm(both, pacingForTests()).mTakes[0].mSegments[0];
            EXPECT_DOUBLE_EQ(hurried.mFrames, 4.0);
            EXPECT_EQ(hurried.mPace, FilmPace::Distance);
            EXPECT_FLOAT_EQ(hurried.mAsked, 6.0f);
            EXPECT_EQ(hurried.mAsker, FilmPace::Clock);
        }

        /// **The eye crosses a take at one speed, keys and bends included**, eased from rest over a
        /// second at each end. Round a right angle at a hundred units a second, ten frames a second:
        /// between the two eases the eye has covered `v · (f − 5)` of the path at frame `f`, the
        /// ease having covered half what cruising would, and stands where the take's own path puts
        /// that length, on either side of the corner's key. The take ends on a whole frame, so its
        /// speed is off the hundred by at most half a frame of its own flight.
        TEST(RtxFilmTest, theEyeCrossesATakeAtOneSpeed)
        {
            const std::vector<FilmKey> keys{ keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("corner", "0,0", osg::Vec3f(1000, 0, 0)), keyAt("b", "0,0", osg::Vec3f(1000, 1000, 0)) };
            FilmPacing pacing = pacingForTests();
            pacing.mEase = 1.0f;

            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 1u);
            const FilmTake& take = plan.mTakes[0];
            const double flight = take.mSegments[0].mFrames + take.mSegments[1].mFrames;
            EXPECT_NEAR(take.mSpeed, 100.0, 100.0 * 0.5 / flight);
            EXPECT_EQ(take.getFrames(), static_cast<std::uint32_t>(std::lround(flight)) + 1);

            const RtxTool::Stop stop = stopsFor(plan, "film/frames")[0];
            const CameraTrack& track = *stop.mSchedule.mTrack;
            const double perFrame = take.mSpeed * double{ pacing.mStep };
            const double corner = take.mPath.getLength(0);
            const std::uint32_t last = take.getFrames() - 1;
            std::uint32_t checked = 0;
            for (std::uint32_t frame = 10; frame + 10 <= last; ++frame, ++checked)
            {
                const double along = perFrame * (static_cast<double>(frame) - 5.0);
                const osg::Vec3d expected = along < corner ? take.mPath.at(0, along) : take.mPath.at(1, along - corner);
                EXPECT_NEAR((osg::Vec3d(track.pose(frame).mEye) - expected).length(), 0.0, 1e-3) << "frame " << frame;
            }
            EXPECT_GT(checked, 190u) << "the cruise, both legs of it";

            EXPECT_LT((track.pose(1).mEye - track.pose(0).mEye).length(), perFrame / 10.0) << "setting off from rest";
        }

        /// **A length sets the speed: the path's length over the frames the rest leaves.** Five
        /// seconds at ten frames a second is fifty frames. The second take holds its first key a
        /// second, and each take has its first frame, so twelve are spoken for and 38 are flown:
        /// 3000 units in the first take and 500 in the second, `3500 / 38 = 92.1` a frame, which
        /// is `32.57` frames and `5.43`. Rounded to whole frames that still add to 38, the larger
        /// remainder takes the frame: 33 and 5, so the first take flies `3000 / 33 = 90.9` a frame
        /// and passes its middle key at frame `1000 / 90.9 = 11`, and the second `500 / 5 = 100`.
        ///
        /// **A flight too short for a frame at that speed is a frame, taken from the longest.** A
        /// third take of five units makes 37 flown frames of 3505 units, `94.7` a frame: `31.67`,
        /// `5.28` and `0.05`. The whole parts are 31, 5 and nought, the frame left over goes to the
        /// largest remainder, the first's, and the third's frame comes back out of it: 31, 5 and 1.
        TEST(RtxFilmTest, aLengthSetsTheSpeed)
        {
            std::vector<FilmKey> keys{
                keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("b", "0,0", osg::Vec3f(1000, 0, 0)),
                keyAt("c", "0,0", osg::Vec3f(3000, 0, 0)),
                keyAt("door", "Vivec, Arena", osg::Vec3f(0, 0, 0)),
                keyAt("hall", "Vivec, Arena", osg::Vec3f(500, 0, 0)),
            };
            keys[3].mHold = 1.0f;

            FilmPacing pacing = pacingForTests();
            pacing.mLength = 5.0f;
            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 2u);

            EXPECT_EQ(plan.getFrames(), 50u);
            EXPECT_EQ(plan.mTakes[0].getFrames(), 34u);
            EXPECT_EQ(plan.mTakes[1].getFrames(), 16u);
            const double step = double{ pacing.mStep };
            EXPECT_NEAR(plan.mTakes[0].mSpeed, 3000.0 / 33.0 / step, 1e-9);
            EXPECT_NEAR(plan.mTakes[1].mSpeed, 100.0 / step, 1e-9);
            EXPECT_NEAR(plan.mTakes[0].mTrack[1].mFrame, 11.0, 1e-9);
            EXPECT_EQ(plan.mTakes[1].mTrack[1].mFrame, 10.0) << "the hold";
            EXPECT_EQ(plan.mTakes[1].mTrack[2].mFrame, 15.0);

            std::vector<FilmKey> hop = keys;
            hop.push_back(keyAt("stair", "Balmora, Guild of Mages", osg::Vec3f(0, 0, 0)));
            hop.push_back(keyAt("step", "Balmora, Guild of Mages", osg::Vec3f(5, 0, 0)));
            const FilmPlan three = planFilm(hop, pacing);
            ASSERT_EQ(three.mTakes.size(), 3u);
            EXPECT_EQ(three.getFrames(), 50u);
            EXPECT_EQ(three.mTakes[0].getFrames(), 32u);
            EXPECT_EQ(three.mTakes[1].getFrames(), 16u);
            EXPECT_EQ(three.mTakes[2].getFrames(), 2u);
            EXPECT_NEAR(three.mTakes[2].mSpeed, 5.0 / step, 1e-9);

            const auto refusal = [&](std::vector<FilmKey> film, float length) {
                FilmPacing asked = pacingForTests();
                asked.mLength = length;
                try
                {
                    planFilm(std::move(film), asked);
                }
                catch (const std::runtime_error& error)
                {
                    return std::string(error.what());
                }
                return std::string("nothing was refused");
            };
            EXPECT_EQ(refusal({ keys[0] }, 5.0f),
                "--length has nothing to set: no key of the film is flown to, where 5 s are");
            EXPECT_EQ(refusal(keys, 1.0f),
                "--length is 1 s, and the holds, the stills, what stands on the spot and each take's first frame take "
                "1.2 s of it, leaving less than a frame for each of the 2 takes that fly");
        }

        /// A hold is its key twice, both resting; a take of one key holds it for a still; each take
        /// is numbered on from the one before; and a stop carries the take whole.
        TEST(RtxFilmTest, takesAreLaidEndToEndAndBecomeStops)
        {
            std::vector<FilmKey> keys{
                keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("b", "0,0", osg::Vec3f(1000, 0, 0)),
                keyAt("c", "0,0", osg::Vec3f(2000, 0, 0)),
                keyAt("room", "Vivec, Arena", osg::Vec3f(0, 0, 0)),
            };
            keys[1].mHold = 1.5f;
            keys[3].mStop.mNote = "the arena";
            keys[3].mStop.mSky.mDay = 5;

            const FilmPlan plan = planFilm(keys, pacingForTests());
            ASSERT_EQ(plan.mTakes.size(), 2u);

            const std::vector<RtxTool::TrackKey>& track = plan.mTakes[0].mTrack;
            ASSERT_EQ(track.size(), 4u);
            EXPECT_EQ(track[0].mFrame, 0.0);
            EXPECT_EQ(track[1].mFrame, 100.0);
            EXPECT_TRUE(track[1].mRests);
            EXPECT_EQ(track[2].mFrame, 115.0);
            EXPECT_TRUE(track[2].mRests);
            EXPECT_EQ(track[2].mEye, track[1].mEye);
            EXPECT_EQ(track[3].mFrame, 215.0);
            EXPECT_EQ(plan.mTakes[0].getFrames(), 216u);

            const std::vector<RtxTool::TrackKey>& still = plan.mTakes[1].mTrack;
            ASSERT_EQ(still.size(), 2u);
            EXPECT_EQ(still[1].mFrame, 40.0) << "the still's four seconds";
            EXPECT_EQ(plan.mTakes[1].mFirstFrame, 216u);
            EXPECT_EQ(plan.getFrames(), 216u + 41u);

            const std::vector<RtxTool::Stop> stops = stopsFor(plan, "film/frames");
            ASSERT_EQ(stops.size(), 2u);

            const RtxTool::Stop& room = stops[1];
            EXPECT_EQ(room.mName, "take-2-room");
            EXPECT_EQ(room.mNote, "the arena");
            EXPECT_EQ(room.mStand.mCell, "Vivec, Arena");
            EXPECT_EQ(room.mStand.mEye, keys[3].getEye());
            EXPECT_EQ(room.mSky.mHour, sDefaultHour);
            EXPECT_EQ(room.mSky.mDay, 5);
            EXPECT_EQ(room.mSky.mWeather, sDefaultWeather);
            EXPECT_EQ(room.mSchedule.mSpec.getWarmup(0.1f), 20u) << "two seconds at ten frames a second";
            EXPECT_EQ(room.mSchedule.mSpec.getMeasured(0.1f), 41u);
            ASSERT_TRUE(room.mSchedule.mTrack.has_value());
            EXPECT_EQ(room.mSchedule.mTrack->getFrames(), 41u);
            ASSERT_TRUE(room.mActions.mFilm.has_value());
            EXPECT_EQ(room.mActions.mFilm->mFirst, 216u);
            EXPECT_EQ(room.mActions.mFilm->mDirectory, std::filesystem::path("film/frames"));

            EXPECT_EQ(stops[0].mSky.mDay, 0) << "the command line's day where a key names none";
            EXPECT_EQ(stops[0].mSchedule.mTrack->pose(100).mEye, keys[1].getEye());
            EXPECT_EQ(stops[0].mSchedule.mTrack->pose(110).mEye, keys[1].getEye()) << "held";
        }

        /// The plan a person reads before an hour of rendering.
        TEST(RtxFilmTest, thePlanSaysEveryLengthAndWhy)
        {
            std::vector<FilmKey> keys{
                keyAt("dock", "-2,-9", osg::Vec3f(0, 0, 0)),
                keyAt("shore", "-2,-9", osg::Vec3f(1000, 0, 0)),
                keyAt("room", "Vivec, Arena", osg::Vec3f(0, 0, 0)),
            };
            keys[1].mStop.mSky.mHour = 18.0f;
            keys[1].mStop.mSky.mWeather = "Rain";

            EXPECT_EQ(describePlan(planFilm(keys, pacingForTests())),
                "film: 3 keys, 2 takes, 142 frames, 14.2 s at 10 frames a second\n"
                "\n"
                "take 1, from frame 0: 10.1 s, cut in: the first key\n"
                "  dock                         -2,-9 12:00, Clear\n"
                "  -> shore                       10.0 s  18:00 Rain, -2,-9  (1000 units at 100 a second; 6.00 hours "
                "of clock asks 12.0 s)\n"
                "\n"
                "take 2, from frame 101: 4.1 s, cut in: outside to inside\n"
                "  room                         Vivec, Arena 12:00, Clear\n");
        }

        /// **A film under its own sky flies at the camera's pace**: the clock at `×512` and the
        /// weather turned through Clear and Rain over the whole film, across its cut, whatever the
        /// keys name after the first.
        ///
        /// By hand: a thousand units at a hundred a second is ten seconds, a hundred frames, with or
        /// without the keys' own clock, which a flight at one speed does not wait for. `×512` is
        /// `512 × 30 / 3600 = 4.27` game hours a second, 0.4267 a frame, so the second key, at frame
        /// 100, is `12 + 42.67 = 54.67` hours, 06:40, and the room, the film's frame 101, 07:06. A
        /// weather stands a second, ten frames, and crosses in eight, eighty: frame 100 is ten into
        /// the second period of ninety, where Rain begins to cross back into Clear.
        TEST(RtxFilmTest, aFilmUnderItsOwnSkyFliesAtTheCamerasPace)
        {
            std::vector<FilmKey> keys{
                keyAt("dock", "-2,-9", osg::Vec3f(0, 0, 0)),
                keyAt("shore", "-2,-9", osg::Vec3f(1000, 0, 0)),
                keyAt("room", "Vivec, Arena", osg::Vec3f(0, 0, 0)),
            };
            keys[1].mStop.mSky.mHour = 18.0f;
            keys[1].mStop.mSky.mWeather = "Rain";
            keys[2].mStop.mSky.mDay = 5;

            EXPECT_DOUBLE_EQ(planFilm(keys, pacingForTests()).mTakes[0].mSegments[0].mFrames, 100.0)
                << "under the keys' clock";

            FilmPacing pacing = pacingForTests();
            pacing.mClock = 512.0f;
            pacing.mTurn = { 0, 4 };
            pacing.mWeatherHold = 1.0f;
            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 2u);

            const FilmSegment& flown = plan.mTakes[0].mSegments[0];
            EXPECT_DOUBLE_EQ(flown.mFrames, 100.0);
            EXPECT_EQ(flown.mPace, FilmPace::Distance);

            const SkyRun& second = plan.mTakes[1].mSky;
            ASSERT_TRUE(second.mHoursPerFrame.has_value());
            EXPECT_NEAR(*second.mHoursPerFrame, 512.0 * 30.0 / 3600.0 * 0.1, 1e-7);
            EXPECT_EQ(second.mHoldFrames, 10u);
            EXPECT_EQ(second.mCrossingFrames, 80u);
            EXPECT_EQ(second.mFirstFrame, plan.mTakes[1].mFirstFrame);
            EXPECT_EQ(second.mFirstFrame, 101u);

            // The first take sets the clock and every take after takes it up, and no take settles a
            // weather the turn holds over.
            const std::vector<RtxTool::Stop> stops = stopsFor(plan, "film/frames");
            ASSERT_EQ(stops.size(), 2u);
            EXPECT_EQ(stops[0].mSky.mHour, sDefaultHour);
            EXPECT_EQ(stops[0].mSky.mDay, 0);
            EXPECT_FALSE(stops[1].mSky.mHour.has_value());
            EXPECT_FALSE(stops[1].mSky.mDay.has_value()) << "the key's day set against the running clock";
            EXPECT_FALSE(stops[0].mSky.mWeather.has_value());
            EXPECT_FALSE(stops[1].mSky.mWeather.has_value());

            const std::string text = describePlan(plan);
            EXPECT_NE(text.find("the clock at ×512 of the game's own over the whole film, 4.27 hours a second\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("the weather through Clear, Rain and round again, each standing 1.0 s and crossing "
                                "in 8.0 s\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("  -> shore                       10.0 s  06:40 Rain → Clear, -2,-9  (1000 units at "
                                "100 a second)\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("  room                         Vivec, Arena 07:06, Rain → Clear\n"), std::string::npos)
                << text;
        }

        /// Only what the film writes goes: six digits and `.png`.
        TEST(RtxFilmTest, clearingTheFramesLeavesEverythingElse)
        {
            const std::filesystem::path frames = TestingOpenMW::outputDirPath("film-frames");
            std::filesystem::remove_all(frames);
            std::filesystem::create_directories(frames);
            for (const char* name :
                { "000000.png", "000001.png", "notes.txt", "12345.png", "0000001.png", "00000a.png" })
                std::ofstream(frames / name) << "x";

            EXPECT_EQ(clearFrames(frames), 2u);
            EXPECT_FALSE(std::filesystem::exists(frames / "000000.png"));
            EXPECT_FALSE(std::filesystem::exists(frames / "000001.png"));
            EXPECT_TRUE(std::filesystem::exists(frames / "notes.txt"));
            EXPECT_TRUE(std::filesystem::exists(frames / "12345.png"));
            EXPECT_TRUE(std::filesystem::exists(frames / "0000001.png"));
            EXPECT_TRUE(std::filesystem::exists(frames / "00000a.png"));

            EXPECT_EQ(clearFrames(frames / "nowhere"), 0u);
            EXPECT_EQ(frameName(42), "000042.png");
        }

        /// Each path is one word to the shell, as `Platform::Process::shellWord` makes one: the frames'
        /// pattern under their folder, and the video, a quote in its name included.
        TEST(RtxFilmTest, theEncoderIsHandedEachPathAsOneWord)
        {
            const auto word = [](const std::filesystem::path& path) {
                return Platform::Process::shellWord(Files::pathToUnicodeString(path));
            };
            EXPECT_EQ(encodeCommand("/tmp/film/frames", "/tmp/it's here/tour.mp4", 60.0f),
                "ffmpeg -hide_banner -loglevel warning -y -framerate 60 -i "
                    + word(std::filesystem::path("/tmp/film/frames") / "%06d.png")
                    + " -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p "
                      "-movflags +faststart "
                    + word("/tmp/it's here/tour.mp4"));
        }
    }
}
