#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/cameratrack.hpp>
#include <apps/rtxtool/model/tour.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr float sCell = 8192.0f;

        WorldMap landAt(const std::vector<osg::Vec2i>& cells, const float highest = 0.0f)
        {
            WorldMap map;
            for (const osg::Vec2i& cell : cells)
                map.mLand.push_back(LandCell{ .mCell = cell, .mHighest = highest });
            return map;
        }

        std::vector<const TourLeg*> legsOf(const TourPlan& plan, const LegKind kind)
        {
            std::vector<const TourLeg*> legs;
            for (const TourLeg& leg : plan.mLegs)
                if (leg.mKind == kind)
                    legs.push_back(&leg);
            return legs;
        }

        /// **An empty content gives nothing to fly**, and says so.
        TEST(RtxTourTest, anEmptyMapPlansNoLeg)
        {
            const TourPlan plan = planTour(WorldMap{}, TourPacing{});
            EXPECT_TRUE(plan.mLegs.empty());
            EXPECT_EQ(plan.getFrames(), 0u);
            EXPECT_EQ(describePlan(plan, 7.0f), "the content gives a tour nothing to fly: no land and no room\n");
            EXPECT_TRUE(stopsFor(plan).empty());
        }

        /// **One cell is one leg across it, and the closing leg flies it again.** A row centred a radius above the
        /// land's lowest row, so the active grid's three rows hold it: the eye at y = 1.5 cells, from the cell's
        /// west edge to its east, 2048 over its 500. 8192 units at 200 a frame eased from and to a rest over 60
        /// frames each: `L/v + min(n·E/2, L/v)` = 40.96 + 40.96 = 81.92, so 82 frames from the first key's.
        TEST(RtxTourTest, oneCellIsOneLegAcrossIt)
        {
            const TourPlan plan = planTour(landAt({ osg::Vec2i(0, 0) }, 500.0f), TourPacing{});
            ASSERT_EQ(plan.mLegs.size(), 2u);
            const TourLeg& leg = plan.mLegs[0];
            EXPECT_EQ(leg.mKind, LegKind::Land);
            EXPECT_EQ(leg.mCell, CellAddress::exterior(0, 1));
            EXPECT_EQ(leg.mCells, std::vector<osg::Vec2i>{ osg::Vec2i(0, 0) });
            ASSERT_EQ(leg.mTrack.size(), 2u);
            EXPECT_EQ(leg.mTrack[0].mEye, osg::Vec3f(0.0f, 1.5f * sCell, 2548.0f));
            EXPECT_EQ(leg.mTrack[1].mEye, osg::Vec3f(sCell, 1.5f * sCell, 2548.0f));
            EXPECT_NEAR(leg.mLength, 8192.0, 1e-6);
            EXPECT_EQ(leg.mTrack[1].mFrame, 82.0);
            EXPECT_EQ(leg.getFrames(), 83u);
            EXPECT_FLOAT_EQ(leg.mTrack[0].mRotation.z(), static_cast<float>(osg::PI_2)) << "facing east";
            EXPECT_FLOAT_EQ(leg.mTrack[0].mRotation.x(), 0.35f) << "looking down";

            const TourLeg& closing = plan.mLegs[1];
            EXPECT_EQ(closing.mKind, LegKind::Closing);
            EXPECT_EQ(closing.mFirstFrame, 0u);
            EXPECT_EQ(closing.mHour, leg.mHour);
            EXPECT_EQ(closing.mTrack.back().mFrame, leg.mTrack.back().mFrame);
            EXPECT_EQ(plan.getFrames(), 166u);

            // The first land leg and the closing leg are the soak's two passes, and the closing leg covers its cells.
            const std::vector<Stop> stops = stopsFor(plan);
            EXPECT_EQ(stops[0].mActions.mSoak, Actions::Soak::First);
            EXPECT_EQ(stops[1].mActions.mSoak, Actions::Soak::Again);
            EXPECT_EQ(stops[1].mActions.mCovers, std::vector<osg::Vec2i>{ osg::Vec2i(0, 0) });
            EXPECT_EQ(stopsFor(plan, 1)[0].mActions.mSoak, Actions::Soak::Again)
                << "a tour begun past the first leg flies the closing leg with nothing to hold it against";
        }

        /// **The sea between two runs of a row is a cut where more than a row's spacing of it lies between**: three
        /// columns of sea are flown over, four are not. With a radius of one the rows are three cells apart.
        TEST(RtxTourTest, aRowsSeaIsACutPastTheRowsSpacing)
        {
            const TourPlan flown = planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(4, 0) }), TourPacing{});
            ASSERT_EQ(legsOf(flown, LegKind::Land).size(), 1u);
            EXPECT_EQ(flown.mLegs[0].mCells, (std::vector<osg::Vec2i>{ osg::Vec2i(0, 0), osg::Vec2i(4, 0) }));
            EXPECT_EQ(flown.mLegs[0].mTrack.size(), 6u) << "an edge a column, sea or land, from 0 to 5";

            const TourPlan cut = planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(5, 0) }), TourPacing{});
            const std::vector<const TourLeg*> legs = legsOf(cut, LegKind::Land);
            ASSERT_EQ(legs.size(), 2u);
            EXPECT_FALSE(legs[1]->mContinues);
            EXPECT_EQ(legs[1]->mCell, CellAddress::exterior(5, 1));

            const TourPlan islands = planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(10, 0) }), TourPacing{});
            EXPECT_EQ(legsOf(islands, LegKind::Land).size(), 2u) << "two islands a sea apart";

            TourPacing wide;
            wide.mGridRadius = 2;
            EXPECT_EQ(legsOf(planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(6, 0) }), wide), LegKind::Land).size(), 1u)
                << "the radius sets the spacing: five columns flown over at a radius of two";
        }

        /// **Back and forth, and a row's end flown on into the next**: the second row flies west from where the first
        /// ended, three cells north, its turn the leg's first segment; the third begins far off, so it is a cut.
        TEST(RtxTourTest, theSweepTurnsAtARowsEndAndCutsPastIt)
        {
            const TourPlan plan = planTour(
                landAt({ osg::Vec2i(0, 0), osg::Vec2i(2, 0), osg::Vec2i(0, 3), osg::Vec2i(2, 3), osg::Vec2i(30, 6) }),
                TourPacing{});
            const std::vector<const TourLeg*> legs = legsOf(plan, LegKind::Land);
            ASSERT_EQ(legs.size(), 3u);
            EXPECT_FALSE(legs[0]->mContinues);
            EXPECT_EQ(legs[0]->mTrack.front().mEye.x(), 0.0f);
            EXPECT_EQ(legs[0]->mTrack.back().mEye.x(), 3.0f * sCell);

            EXPECT_TRUE(legs[1]->mContinues);
            EXPECT_EQ(legs[1]->mTrack.front().mEye, legs[0]->mTrack.back().mEye);
            EXPECT_EQ(legs[1]->mTrack[1].mEye, osg::Vec3f(3.0f * sCell, 4.5f * sCell, 2048.0f));
            EXPECT_EQ(legs[1]->mTrack.back().mEye.x(), 0.0f) << "westward";
            EXPECT_FLOAT_EQ(legs[1]->mTrack.back().mRotation.z(), static_cast<float>(-osg::PI_2));

            EXPECT_FALSE(legs[2]->mContinues);
            EXPECT_EQ(legs[2]->mCells, std::vector<osg::Vec2i>{ osg::Vec2i(30, 6) });

            const std::vector<Stop> stops = stopsFor(plan);
            EXPECT_TRUE(stops[1].mSchedule.mContinues);
            EXPECT_EQ(stops[1].mSchedule.mSpec.mWarm.mFrames, 0u);
            EXPECT_FALSE(stops[2].mSchedule.mContinues);
            EXPECT_EQ(stops[2].mSchedule.mSpec.mWarm.mFrames, sHistoryFrames);
        }

        /// **The eye stands a clearance over the highest ground beside it**: a key at an edge reads the two columns
        /// either side and one past each, in the row's band, so the 3000 of column 3 lifts the keys at edges 2 to 5.
        TEST(RtxTourTest, theHeightIsTheLandsOwn)
        {
            WorldMap map = landAt({ osg::Vec2i(0, 0), osg::Vec2i(1, 0), osg::Vec2i(2, 0), osg::Vec2i(4, 0),
                osg::Vec2i(5, 0), osg::Vec2i(6, 0), osg::Vec2i(7, 0) });
            map.mLand.push_back(LandCell{ .mCell = osg::Vec2i(3, 2), .mHighest = 3000.0f });
            const TourPlan plan = planTour(map, TourPacing{});
            const TourLeg& leg = plan.mLegs[0];
            ASSERT_EQ(leg.mTrack.size(), 9u);
            std::vector<float> heights;
            for (const TrackKey& key : leg.mTrack)
                heights.push_back(key.mEye.z());
            EXPECT_EQ(heights, (std::vector<float>{ 2048, 2048, 5048, 5048, 5048, 5048, 2048, 2048, 2048 }));
        }

        /// **The circuit holds every ordered pair of two weathers once**, ninety crossings from `Clear`, the last
        /// back to the first.
        TEST(RtxTourTest, theCircuitOfWeathersCrossesEveryPairOnce)
        {
            const std::vector<Rtx::Weather> circuit = weatherCircuit();
            constexpr auto count = static_cast<std::size_t>(Rtx::Weather::Count);
            ASSERT_EQ(circuit.size(), count * (count - 1));
            EXPECT_EQ(circuit.front(), Rtx::Weather::Clear);

            std::set<std::pair<Rtx::Weather, Rtx::Weather>> pairs;
            for (std::size_t at = 0; at < circuit.size(); ++at)
            {
                const Rtx::Weather from = circuit[at];
                const Rtx::Weather to = circuit[(at + 1) % circuit.size()];
                EXPECT_NE(from, to) << "a crossing into itself at " << at;
                EXPECT_TRUE(pairs.emplace(from, to).second) << "a pair crossed twice at " << at;
            }
            EXPECT_EQ(pairs.size(), count * (count - 1));
        }

        /// **A file's rooms are its share of the sixty, never under eight nor over what it has**: vanilla's three
        /// files come to 51, 8 and 8 of 1,134, 96 and 98 rooms (`60·1134/1328` = 51.2); a file of five gives five.
        TEST(RtxTourTest, aFilesShareKeepsItsLeastAndGivesNoMoreThanItHas)
        {
            EXPECT_EQ(shareRooms({ 1134, 96, 98 }, 60, 8), (std::vector<std::uint32_t>{ 51, 8, 8 }));
            EXPECT_EQ(shareRooms({ 1000, 5 }, 60, 8), (std::vector<std::uint32_t>{ 60, 5 }));
            EXPECT_EQ(shareRooms({ 1000, 10 }, 60, 8), (std::vector<std::uint32_t>{ 59, 8 }));
            EXPECT_EQ(shareRooms({ 0 }, 60, 8), (std::vector<std::uint32_t>{ 0 }));
        }

        /// **Within a file, the rooms with the most references, a few a settlement, and the rooms a person names
        /// besides**: four of "Vivec" by references keep three, "Balmora" takes the fourth place, and a named room
        /// the share left out is visited after.
        TEST(RtxTourTest, theRoomsAreAFilesLargestAFewASettlement)
        {
            WorldMap map;
            map.mFiles = { "Morrowind.esm" };
            for (const auto& [name, references] :
                std::vector<std::pair<const char*, std::uint32_t>>{ { "Vivec, A", 100 }, { "Vivec, B", 90 },
                    { "Vivec, C", 80 }, { "Vivec, D", 70 }, { "Balmora", 10 }, { "Ald-ruhn", 5 } })
                map.mRooms.push_back(Room{ .mName = name, .mFile = 0, .mReferences = references, .mEye = {} });

            TourPacing pacing;
            pacing.mRooms = 4;
            pacing.mRoomsLeast = 0;
            pacing.mNamedRooms = { "Vivec, D", "Nowhere" };
            const TourPlan plan = planTour(map, pacing);

            std::vector<std::string> rooms;
            for (const TourLeg* leg : legsOf(plan, LegKind::Room))
                rooms.push_back(leg->mCell.getSpelling());
            EXPECT_EQ(rooms, (std::vector<std::string>{ "Vivec, A", "Vivec, B", "Vivec, C", "Balmora", "Vivec, D" }));
            ASSERT_EQ(plan.mVisits.size(), 1u);
            EXPECT_EQ(plan.mVisits[0].mVisits, 4u);
            EXPECT_EQ(plan.mVisits[0].mRooms, 6u);
            EXPECT_TRUE(legsOf(plan, LegKind::Closing).empty()) << "no land, no closing leg";
        }

        /// **A room turns once round where the eye stands**, a quarter each sixty frames of its four seconds.
        TEST(RtxTourTest, aRoomTurnsOnceRound)
        {
            WorldMap map;
            map.mFiles = { "Morrowind.esm" };
            map.mRooms.push_back(Room{ .mName = "Balmora, Guild of Mages", .mReferences = 1, .mEye = { 1, 2, 3 } });
            const TourPlan plan = planTour(map, TourPacing{});
            ASSERT_EQ(plan.mLegs.size(), 1u);
            const TourLeg& leg = plan.mLegs[0];
            EXPECT_EQ(leg.mCell, CellAddress::interior("Balmora, Guild of Mages"));
            ASSERT_EQ(leg.mTrack.size(), 5u);
            for (std::size_t at = 0; at < leg.mTrack.size(); ++at)
            {
                EXPECT_EQ(leg.mTrack[at].mFrame, 60.0 * static_cast<double>(at));
                EXPECT_EQ(leg.mTrack[at].mEye, osg::Vec3f(1, 2, 3));
                EXPECT_FLOAT_EQ(leg.mTrack[at].mRotation.z(), static_cast<float>(osg::PI_2 * static_cast<double>(at)));
            }
            EXPECT_EQ(leg.getFrames(), 241u);

            // A tour of rooms alone has no soak: no land leg to fly again.
            const std::vector<Stop> stops = stopsFor(plan);
            ASSERT_EQ(stops.size(), 1u);
            EXPECT_EQ(stops[0].mActions.mSoak, Actions::Soak::None);
            EXPECT_TRUE(stops[0].mActions.mCovers.empty());
        }

        /// **The clock runs through the tour and the closing leg begins it anew**: 24 times the game's own speed at
        /// a timescale of 30 is `24 · 30 / 60 / 3600` = 1/300 of an hour a frame, so the second leg begins at
        /// `9 + 83/300`, and a leg past midnight on the next day.
        TEST(RtxTourTest, theClockRunsThroughTheTour)
        {
            const TourPlan plan = planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(10, 0) }), TourPacing{});
            ASSERT_EQ(plan.mLegs.size(), 3u);
            EXPECT_EQ(plan.mLegs[1].mFirstFrame, 83u);
            EXPECT_FLOAT_EQ(plan.mLegs[1].mHour, 9.0f + 83.0f / 300.0f);
            EXPECT_EQ(plan.mLegs[1].mDay, 0);
            EXPECT_EQ(plan.mLegs[2].mHour, 9.0f) << "the closing leg";

            TourPacing late;
            late.mHour = 23.9f;
            const TourPlan night = planTour(landAt({ osg::Vec2i(0, 0), osg::Vec2i(10, 0) }), late);
            EXPECT_EQ(night.mLegs[1].mDay, 1);
            EXPECT_NEAR(night.mLegs[1].mHour, 23.9 + 83.0 / 300.0 - 24.0, 1e-4);
        }

        /// **A leg begins under the same hour and crossing whichever leg the run began from**, and two plans of one
        /// map agree frame for frame: the tour is the map's and the pacing's and nothing else.
        TEST(RtxTourTest, aLegStandsAsTheWholeTourStoodThere)
        {
            const WorldMap map
                = landAt({ osg::Vec2i(0, 0), osg::Vec2i(2, 0), osg::Vec2i(0, 3), osg::Vec2i(2, 3), osg::Vec2i(30, 6) });
            const TourPlan plan = planTour(map, TourPacing{});
            const std::vector<Stop> whole = stopsFor(plan);
            const std::vector<Stop> later = stopsFor(plan, 2);
            ASSERT_EQ(later.size(), whole.size() - 2);
            EXPECT_EQ(later[0].mName, whole[2].mName);
            EXPECT_EQ(later[0].mSky.mHour, whole[2].mSky.mHour);
            EXPECT_EQ(later[0].mSky.mDay, whole[2].mSky.mDay);
            const TrackPose fromWhole = whole[2].mSchedule.mTrack->pose(10);
            const TrackPose fromLater = later[0].mSchedule.mTrack->pose(10);
            EXPECT_EQ(fromLater.mWeather, fromWhole.mWeather);
            EXPECT_EQ(fromLater.mNextWeather, fromWhole.mNextWeather);
            EXPECT_EQ(fromLater.mCrossed, fromWhole.mCrossed);
            EXPECT_EQ(fromLater.mClockOn, std::nullopt) << "the world runs the clock, and the track sets none";
            EXPECT_EQ(later[0].mSky.mClockTimes, 24.0f) << "at the pacing's rate";

            const std::vector<Stop> fromSecond = stopsFor(plan, 1);
            EXPECT_FALSE(fromSecond[0].mSchedule.mContinues) << "the first leg flown is a cut";
            EXPECT_EQ(fromSecond[0].mSchedule.mSpec.mWarm.mFrames, sHistoryFrames);

            const TourPlan again = planTour(map, TourPacing{});
            const std::vector<Stop> againStops = stopsFor(again);
            ASSERT_EQ(againStops.size(), whole.size());
            for (std::size_t leg = 0; leg < plan.mLegs.size(); ++leg)
            {
                const CameraTrack& first = *whole[leg].mSchedule.mTrack;
                const CameraTrack& second = *againStops[leg].mSchedule.mTrack;
                ASSERT_EQ(first.getFrames(), second.getFrames());
                for (std::uint32_t frame = 0; frame < first.getFrames(); frame += 7)
                {
                    EXPECT_EQ(first.pose(frame).mEye, second.pose(frame).mEye);
                    EXPECT_EQ(first.pose(frame).mWeather, second.pose(frame).mWeather);
                }
            }
        }

        /// **The plan as a person reads it**: a leg a line, the rooms by file, and the minutes at a frame time.
        TEST(RtxTourTest, thePlanSaysWhatItFliesAndWhatItCosts)
        {
            WorldMap map = landAt({ osg::Vec2i(0, 0) }, 500.0f);
            map.mFiles = { "Morrowind.esm" };
            map.mRooms.push_back(Room{ .mName = "Vivec, Arena Pit", .mReferences = 959, .mEye = {} });
            map.mRoomsNoDoorEnters = 13;
            const std::string text = describePlan(planTour(map, TourPacing{}), 6.0f);
            EXPECT_NE(text.find("tour: 1 land cells across 1 x 1, rows every 3 cells at 12000 units a second, 2048 up"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("3 legs: 1 cells flown, 1 rooms"), std::string::npos) << text;
            EXPECT_NE(text.find("room  959 references"), std::string::npos) << text;
            EXPECT_NE(text.find("rooms by file: Morrowind.esm 1 of 1;"), std::string::npos) << text;
            EXPECT_NE(text.find("13 rooms no door leads into left out"), std::string::npos) << text;
            // 83 + 241 + 83 frames at 6 ms: 407 · 6 / 60000 = 0.04 min.
            EXPECT_NE(text.find("407 frames, 0.0 min at 6 ms a frame"), std::string::npos) << text;
        }
    }
}
