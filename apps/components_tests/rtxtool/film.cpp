#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>
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
                                .mSky = { .mHour = sDefaultHour, .mWeather = sDefaultWeather } } };
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
                    .mWeather = Rtx::Weather::Overcast,
                    .mAir = Rtx::AirClock{ .mSky = { .mSeconds = 36000.123456789, .mCloudScroll = 0.5f },
                        .mCarried = osg::Vec2d(-123456.78901234, 0.1) } } };
            RtxTool::Stop noon = dawn;
            noon.mSky = { .mHour = 12.0f, .mDay = 0, .mWeather = Rtx::Weather::Clear };

            const std::vector<FilmKey> keys = read(describeStanding(dawn) + describeStanding(noon) + describeKey(dawn));
            ASSERT_EQ(keys.size(), 3u);

            EXPECT_EQ(keys[0].mStop.mName, "balmora");
            EXPECT_EQ(keys[1].mStop.mName, "balmora");
            EXPECT_EQ(keys[0].getCell(), "-3,-2");
            EXPECT_EQ(keys[0].getEye(), *dawn.mStand.mEye);
            EXPECT_EQ(keys[0].getLook(), *dawn.mStand.mLook);
            EXPECT_EQ(keys[0].getHour(), 6.5f);
            EXPECT_EQ(keys[0].getWeather(), Rtx::Weather::Overcast);
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
            EXPECT_EQ(keys[2].getWeather(), Rtx::Weather::Overcast);

            const std::vector<FilmKey> timed
                = read("[a]\ncell = 0,0\npos = 1,2,3\nlook = 4,5,6\nseconds = 2.5\nat = 7.25\nhold = 1\ncut = false\n");
            EXPECT_EQ(timed[0].mSeconds, 2.5f);
            EXPECT_EQ(timed[0].mAt, 7.25f);
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
            EXPECT_EQ(refusal(place + "day = -1\n"), "tour.keys:5: day \"-1\" is before the first day");
            EXPECT_EQ(refusal("cell = 0,0\n"), "tour.keys:1: a field comes before the first [section]");
            EXPECT_EQ(refusal("# a comment\n\n[a]\ncell = 0,0\npos = 1,2,3\n[b]\n"),
                "tour.keys:3: key \"a\" names no pos and look");
            EXPECT_EQ(refusal(place + "[b\n"), "tour.keys:5: a section's name is not closed by ]");
            EXPECT_EQ(refusal("[a]\ncell = 0,0\nlook = 4,5,6\npos = 1,2\n"),
                "tour.keys:4: pos \"1,2\" is not three numbers separated by commas")
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
                keys[at].mStop.mSky.mWeather = Rtx::Weather::Rain;
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

            // A flight at the film's ten units a frame, a segment that goes nowhere at none, and
            // the key's own 2.5 s over 4000 units at 160.
            EXPECT_DOUBLE_EQ(segments[0].mSpeed, 10.0);
            EXPECT_EQ(segments[1].mSpeed, 0.0);
            EXPECT_DOUBLE_EQ(segments[6].mSpeed, 160.0);

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
            const double perFrame = take.mSegments[0].mSpeed;
            EXPECT_NEAR(perFrame, 10.0, 10.0 * 0.5 / flight);
            EXPECT_EQ(take.mSegments[1].mSpeed, perFrame);
            EXPECT_EQ(take.getFrames(), static_cast<std::uint32_t>(std::lround(flight)) + 1);

            const RtxTool::Stop stop = stopsFor(plan, "film/frames")[0];
            const CameraTrack& track = *stop.mSchedule.mTrack;
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

        /// **A key between a flight and a faster one is passed at the slower speed, and the faster
        /// changes speed within an ease of it.** At a hundred units a second, ten frames a second
        /// and an ease of a second, ten frames: a thousand units from rest are `100 + 10 / 2 = 105`
        /// frames at ten a frame. Five thousand more in the key's own ten seconds, to a rest,
        /// cruise at `5000 / (100 − 5) = 52.6` a frame, 526 a second; and joined at ten, at
        /// `(5000 − 10 · 10 / 2) / (100 − 5 − 5) = 55`. The eye moves ten units into the key and
        /// `10 + 45 · 10 · (0.1³ − 0.1⁴ / 2) = 10.4275` out of it, where a leg at its own speed
        /// would jump to 52.6.
        TEST(RtxFilmTest, aKeyBetweenAFlightAndAFasterOneIsPassedAtTheSlowerSpeed)
        {
            std::vector<FilmKey> keys{ keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("b", "0,0", osg::Vec3f(1000, 0, 0)), keyAt("c", "0,0", osg::Vec3f(6000, 0, 0)) };
            keys[2].mSeconds = 10.0f;
            FilmPacing pacing = pacingForTests();
            pacing.mEase = 1.0f;

            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 1u);
            const FilmTake& take = plan.mTakes[0];
            // The step is a tenth in single precision, so the ease is ten frames less a part in
            // ten million, and each speed off by as much.
            EXPECT_NEAR(take.mSegments[0].mSpeed, 10.0, 1e-7 * 10.0);
            EXPECT_NEAR(take.mSegments[1].mSpeed, 5000.0 / 95.0, 1e-7 * 5000.0 / 95.0);
            EXPECT_EQ(take.mTrack[1].mFrame, 105.0);
            EXPECT_EQ(take.mTrack[1].mSpeed, take.mSegments[0].mSpeed);
            EXPECT_EQ(take.mTrack[2].mSpeed, take.mSegments[1].mSpeed);
            EXPECT_NE(describePlan(plan).find("(as the key says, 5000 units at 526 a second)\n"), std::string::npos)
                << describePlan(plan);

            // The eye is single precision, a sixteenth of a thousandth apart at a thousand units.
            const RtxTool::Stop stop = stopsFor(plan, "film/frames")[0];
            const CameraTrack& track = *stop.mSchedule.mTrack;
            EXPECT_NEAR(track.pose(104).mEye.x(), 990.0f, 1e-3f);
            EXPECT_FLOAT_EQ(track.pose(105).mEye.x(), 1000.0f);
            EXPECT_NEAR(track.pose(106).mEye.x(), 1010.4275f, 1e-3f);
            EXPECT_FLOAT_EQ(track.pose(205).mEye.x(), 6000.0f);
        }

        /// **A key's time closes a span, whose flights share the speed that fills it.** Ten frames a
        /// second: two thousand units to a key at five seconds are fifty frames, forty a frame, the
        /// middle key at 25 and the timed one at 50 exactly; the thousand after it at the film's
        /// ten a frame are a hundred more, 151 frames in all.
        ///
        /// **Across a cut**, a room at twenty seconds closes the span after the key at five: 150
        /// frames, of which the frame the first take ends on is one, so its thousand units take 149,
        /// and the room's take begins at frame 200. A named length of twenty seconds closes the last
        /// span alike, at the film's last frame, 199; a default one stands aside, and a named one of
        /// four seconds, which ends before the key at five, is refused.
        TEST(RtxFilmTest, aKeysTimeClosesASpan)
        {
            std::vector<FilmKey> keys{
                keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("b", "0,0", osg::Vec3f(1000, 0, 0)),
                keyAt("c", "0,0", osg::Vec3f(2000, 0, 0)),
                keyAt("d", "0,0", osg::Vec3f(3000, 0, 0)),
            };
            keys[2].mAt = 5.0f;

            const FilmPlan plan = planFilm(keys, pacingForTests());
            ASSERT_EQ(plan.mTakes.size(), 1u);
            const FilmTake& take = plan.mTakes[0];
            EXPECT_DOUBLE_EQ(take.mSegments[0].mSpeed, 40.0);
            EXPECT_DOUBLE_EQ(take.mSegments[1].mSpeed, 40.0);
            EXPECT_DOUBLE_EQ(take.mSegments[2].mSpeed, 10.0);
            EXPECT_DOUBLE_EQ(take.mTrack[1].mFrame, 25.0);
            EXPECT_EQ(take.mTrack[2].mFrame, 50.0);
            EXPECT_EQ(plan.getFrames(), 151u);
            EXPECT_NE(
                describePlan(plan).find("12:00 Clear, at 5 s, 0,0  (1000 units at 400 a second)\n"), std::string::npos)
                << describePlan(plan);

            std::vector<FilmKey> cut = keys;
            cut.push_back(keyAt("room", "Vivec, Arena", osg::Vec3f(0, 0, 0)));
            cut[4].mAt = 20.0f;
            const FilmPlan two = planFilm(cut, pacingForTests());
            ASSERT_EQ(two.mTakes.size(), 2u);
            EXPECT_DOUBLE_EQ(two.mTakes[0].mSegments[2].mSpeed, 1000.0 / 149.0);
            EXPECT_EQ(two.mTakes[1].mFirstFrame, 200u);

            FilmPacing length = pacingForTests();
            length.mLength = FilmLength{ .mSeconds = 20.0f };
            const FilmPlan filled = planFilm(keys, length);
            EXPECT_EQ(filled.getFrames(), 200u);
            EXPECT_DOUBLE_EQ(filled.mTakes[0].mSegments[2].mSpeed, 1000.0 / 149.0);
            FilmPacing brief = pacingForTests();
            brief.mLength = FilmLength{ .mSeconds = 4.0f };
            try
            {
                planFilm(keys, brief);
                ADD_FAILURE() << "a length that ends before a key's time was taken";
            }
            catch (const std::runtime_error& error)
            {
                EXPECT_STREQ(error.what(), "--length is 4 s, no later than key \"c\" at 5 s");
            }

            length.mLength->mSource = FilmLengthSource::ByDefault;
            const FilmPlan aside = planFilm(keys, length);
            EXPECT_EQ(aside.getFrames(), 151u);
            EXPECT_EQ(aside.mPacing.mLength, std::nullopt);

            // A quarter of a second over the step, a tenth in single precision, is 2.5 exactly, and
            // the key is on frame 3, the frames a length of a quarter of a second takes. In double
            // the quotient is a hair under 2.5, and the key was on frame 2.
            std::vector<FilmKey> quarter{ keyAt("a", "0,0", osg::Vec3f(0, 0, 0)),
                keyAt("b", "0,0", osg::Vec3f(100, 0, 0)) };
            quarter[1].mAt = 0.25f;
            EXPECT_EQ(pacingForTests().framesOf(0.25f), 3u);
            EXPECT_EQ(planFilm(quarter, pacingForTests()).mTakes[0].mTrack.back().mFrame, 3.0);

            const auto refusal = [&](std::vector<FilmKey> film) {
                try
                {
                    planFilm(std::move(film), pacingForTests());
                }
                catch (const std::runtime_error& error)
                {
                    return std::string(error.what());
                }
                return std::string("nothing was refused");
            };
            std::vector<FilmKey> late = keys;
            late[0].mAt = 1.0f;
            EXPECT_EQ(
                refusal(late), "key \"a\" on line 0 is at 1 s, and the first key is where the film starts, at 0 s");
            std::vector<FilmKey> back = keys;
            back[3].mAt = 5.0f;
            EXPECT_EQ(refusal(back), "key \"d\" on line 0 is at 5 s, no later than key \"c\" on line 0 at 5 s");
            std::vector<FilmKey> full = keys;
            full[1].mHold = 6.0f;
            EXPECT_EQ(refusal(full),
                "key \"c\" on line 0 is at 5 s, 5.0 s after key \"a\", and the holds, the stills, what stands on the "
                "spot, the keys' own seconds and each take's first frame between them take 6.0 s of it, leaving less "
                "than a frame for each of the 1 takes that fly");
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
        ///
        /// **A length nobody named stands aside where a named one is refused.** A flight of its own
        /// three seconds leaves the default nothing to fill: thirty frames and the first, 31 and not
        /// 50. A default of one second leaves the five keys no frame to fly, so they fly at the
        /// hundred units a second: 3000 units are 300 frames and the first, and the hold's ten and
        /// 500 units' fifty and the first are 61, 362 in all.
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
            pacing.mLength = FilmLength{ .mSeconds = 5.0f };
            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 2u);

            EXPECT_EQ(plan.getFrames(), 50u);
            EXPECT_EQ(plan.mTakes[0].getFrames(), 34u);
            EXPECT_EQ(plan.mTakes[1].getFrames(), 16u);
            EXPECT_NEAR(plan.mTakes[0].mSegments[0].mSpeed, 3000.0 / 33.0, 1e-9);
            EXPECT_NEAR(plan.mTakes[1].mSegments[0].mSpeed, 100.0, 1e-9);
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
            EXPECT_NEAR(three.mTakes[2].mSegments[0].mSpeed, 5.0, 1e-9);

            const auto refusal = [&](std::vector<FilmKey> film, float length) {
                FilmPacing asked = pacingForTests();
                asked.mLength = FilmLength{ .mSeconds = length };
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
                "--length is 1 s, and the holds, the stills, what stands on the spot, the keys' own seconds and each "
                "take's first frame take 1.2 s of it, leaving less than a frame for each of the 2 takes that fly");

            std::vector<FilmKey> given{ keys[0], keys[1] };
            given[1].mSeconds = 3.0f;
            EXPECT_EQ(
                refusal(given, 5.0f), "--length has nothing to set: no key of the film is flown to, where 5 s are");

            const auto byDefault = [&](std::vector<FilmKey> film, float length) {
                FilmPacing asked = pacingForTests();
                asked.mLength = FilmLength{ .mSeconds = length, .mSource = FilmLengthSource::ByDefault };
                return planFilm(std::move(film), asked);
            };
            const FilmPlan own = byDefault(given, 5.0f);
            EXPECT_EQ(own.getFrames(), 31u);
            EXPECT_EQ(own.mDefaultTooShort, std::nullopt);
            EXPECT_EQ(own.mPacing.mLength, std::nullopt);

            const FilmPlan paced = byDefault(keys, 1.0f);
            EXPECT_EQ(paced.getFrames(), 362u);
            EXPECT_EQ(paced.mDefaultTooShort, 1.0f);
            EXPECT_EQ(paced.mPacing.mLength, std::nullopt);
            EXPECT_EQ(paced.getFrames(), planFilm(keys, pacingForTests()).getFrames()) << "the speed's own film";
            EXPECT_TRUE(describePlan(paced).starts_with(
                "film: 5 keys, 2 takes, 362 frames, 36.2 s at 10 frames a second\n"
                "the 1 s a film is when neither --length nor --speed is named leave no frame to fly after the holds, "
                "the stills, what stands on the spot, the keys' own seconds and each take's first frame, so every "
                "flight is at --speed, 100 units a second\n"))
                << describePlan(paced);

            const FilmPlan filled = byDefault(keys, 5.0f);
            EXPECT_EQ(filled.getFrames(), 50u) << "a default with room is the length";
            EXPECT_EQ(filled.mDefaultTooShort, std::nullopt);
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
            EXPECT_EQ(room.mSchedule.mSpec.getWarmup(0.1f), 128u) << "four accumulators of thirty-two, at any step";
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
            keys[1].mStop.mSky.mWeather = Rtx::Weather::Rain;

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
        /// without the keys' own clock, which a flight at one speed does not wait for. `×512` is 512
        /// seconds of the game's own clock a second, 51.2 a frame, so the second key, at frame 100,
        /// is 5120 of them on from the first key's 12:00, and the room, the film's frame 101, 5171;
        /// which a session at a `timescale` of 30 makes `5120 × 30 / 3600 = 42.67` hours, 06:40. A
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
            keys[1].mStop.mSky.mWeather = Rtx::Weather::Rain;
            keys[2].mStop.mSky.mDay = 5;

            EXPECT_DOUBLE_EQ(planFilm(keys, pacingForTests()).mTakes[0].mSegments[0].mFrames, 100.0)
                << "under the keys' clock";

            FilmPacing pacing = pacingForTests();
            pacing.mClock = 512.0f;
            pacing.mTurn = { Rtx::Weather::Clear, Rtx::Weather::Rain };
            pacing.mWeatherHold = 1.0f;
            const FilmPlan plan = planFilm(keys, pacing);
            ASSERT_EQ(plan.mTakes.size(), 2u);

            const FilmSegment& flown = plan.mTakes[0].mSegments[0];
            EXPECT_DOUBLE_EQ(flown.mFrames, 100.0);
            EXPECT_EQ(flown.mPace, FilmPace::Distance);

            const SkyRun& second = plan.mTakes[1].mSky;
            ASSERT_TRUE(second.mClockPerFrame.has_value());
            EXPECT_NEAR(*second.mClockPerFrame, 51.2, 1e-5);
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
            EXPECT_NE(text.find("the clock at ×512 of the game's own over the whole film, the hours below as the first "
                                "key's and the seconds the clock ran since\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("the weather through Clear, Rain and round again, each standing 1.0 s and crossing "
                                "in 8.0 s\n"),
                std::string::npos)
                << text;
            EXPECT_NE(
                text.find("  -> shore                       10.0 s  12:00 +5120 s Rain → Clear, -2,-9  (1000 units "
                          "at 100 a second)\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("  room                         Vivec, Arena 12:00 +5171 s, Rain → Clear\n"),
                std::string::npos)
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
