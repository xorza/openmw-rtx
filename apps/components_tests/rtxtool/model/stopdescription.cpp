#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include <osg/Vec2d>
#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/stopdescription.hpp>
#include <apps/rtxtool/model/viewfile.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/testing/util.hpp>

#ifndef OPENMW_PROJECT_SOURCE_DIR
#define OPENMW_PROJECT_SOURCE_DIR "."
#endif

namespace RtxTool
{
    namespace
    {
        RtxTool::Stop makeSpot()
        {
            return RtxTool::Stop{
                .mName = "balmora-mages-guild",
                .mNote = "a guild interior, dense with clutter",
                .mStand = { .mCell = CellAddress("Balmora, Guild of Mages"),
                    .mEye = osg::Vec3f(-283.29843f, -671.29584f, -580.77014f),
                    .mLook = osg::Vec3f(503.60007f, -1265.436f, -747.46844f) },
                .mSky = { .mHour = 12.0f, .mDay = 0, .mWeather = Rtx::Weather::Clear },
            };
        }

        TEST(RtxViewpointTest, aSpotSaysWhichWayItFaces)
        {
            const auto facing = [](float x, float y, float z) {
                return RtxTool::Stand{ .mEye = osg::Vec3f(), .mLook = osg::Vec3f(x, y, z) };
            };

            // Due north, and the one case the swapped `atan2` also gets right.
            EXPECT_NEAR(bearingOf(facing(0.0f, 100.0f, 0.0f)), 0.0f, 1e-3f);
            EXPECT_NEAR(bearingOf(facing(100.0f, 100.0f, 0.0f)), 45.0f, 1e-3f) << "north-east";
            EXPECT_NEAR(bearingOf(facing(100.0f, 0.0f, 0.0f)), 90.0f, 1e-3f) << "due east";
            // Wrapped rather than negative: a compass has no -90.
            EXPECT_NEAR(bearingOf(facing(-100.0f, 0.0f, 0.0f)), 270.0f, 1e-3f) << "due west";

            // Equal parts along and up is forty-five degrees, and the sign is up rather than down.
            EXPECT_NEAR(climbOf(facing(0.0f, 100.0f, 100.0f)), 45.0f, 1e-3f);
            EXPECT_NEAR(climbOf(facing(0.0f, 100.0f, 0.0f)), 0.0f, 1e-3f);
            EXPECT_NEAR(climbOf(facing(0.0f, 100.0f, -100.0f)), -45.0f, 1e-3f);
            // Straight down, where the horizontal part is zero and `asin` is handed exactly -1.
            EXPECT_NEAR(climbOf(facing(0.0f, 0.0f, -100.0f)), -90.0f, 1e-3f);
        }

        /// The readable line, which is the one nobody parses and everybody reads.
        TEST(RtxViewpointTest, theSpotLineSaysWhereAndWhen)
        {
            const std::string line = describeSpot(makeSpot());
            EXPECT_EQ(line,
                "# Balmora, Guild of Mages at -283, -671, -581 \u2014 bearing 127\u00b0, climb -10\u00b0 \u2014 day 0, "
                "12:00, "
                "Clear\n");

            // A quarter past five in the evening, because a decimal hour is not a time anyone reads.
            RtxTool::Stop evening = makeSpot();
            evening.mSky.mHour = 17.25f;
            evening.mSky.mWeather = Rtx::Weather::Ashstorm;
            EXPECT_NE(describeSpot(evening).find("17:15, Ashstorm"), std::string::npos) << describeSpot(evening);
        }

        /// The command names every condition, the defaults included, and the numbers the block
        /// writes: what it starts is the frame that was looked at, and not one an option was left
        /// to decide.
        TEST(RtxViewpointTest, theCommandLineNamesEveryCondition)
        {
            EXPECT_EQ(describeCommand(makeSpot()),
                "# openmw-rtxtool view --cell=\"Balmora, Guild of Mages\" --pos=-283.29843,-671.29584,-580.77014 "
                "--look=503.60007,-1265.436,-747.46844 --hour=12 --day=0 --weather=Clear\n");

            RtxTool::Stop dawn = makeSpot();
            dawn.mSky.mHour = 6.5f;
            dawn.mSky.mDay = 17;
            dawn.mSky.mWeather = Rtx::Weather::Thunderstorm;
            EXPECT_NE(describeCommand(dawn).find("--hour=6.5 --day=17 --weather=Thunderstorm"), std::string::npos)
                << describeCommand(dawn);

            // An air is named last, where it is known, as the one argument `--air` reads back.
            dawn.mSky.mAir = Rtx::AirClock{ .mSky = { .mSeconds = 0.1, .mCloudScroll = 0.25f },
                .mCarried = osg::Vec2d(-2041.5, 5432.25) };
            EXPECT_TRUE(describeCommand(dawn).ends_with("--weather=Thunderstorm --air=0.1,0.25,-2041.5,5432.25\n"))
                << describeCommand(dawn);
        }

        /// **The symmetry, asserted rather than assumed**: what the window prints, the view file
        /// reads, and what comes back is the camera that was standing there.
        ///
        /// **Everything P writes, and not only the block half of it.** The readable line goes in
        /// front of it and would be pasted with it, so if the view file could not take a comment the
        /// output would be one edit away from usable rather than usable.
        ///
        /// Exact equality on the floats and not a tolerance — the block is written shortest
        /// round-trip precisely so that a saved viewpoint is the viewpoint, and a position rounded
        /// to the unit is a different frame when the camera is a hand's width from a wall.
        TEST(RtxViewpointTest, theBlockItPrintsIsTheBlockTheViewFileReads)
        {
            const RtxTool::Stop spot = makeSpot();

            // The whole of what a window prints, because the command line is pasted with the rest
            // and has to read as a comment.
            const std::filesystem::path file = TestingOpenMW::outputFilePath("viewpoint-test.cfg");
            {
                std::ofstream out(file);
                out << describeStanding(spot);
            }

            const std::vector<RtxTool::Stop> read = loadViews(file);
            std::filesystem::remove(file);

            ASSERT_EQ(read.size(), 1u);
            EXPECT_EQ(read.front().mName, spot.mName);
            EXPECT_EQ(read.front().mNote, spot.mNote);
            EXPECT_EQ(read.front().mStand.mCell, spot.mStand.mCell);
            ASSERT_TRUE(read.front().mStand.mEye.has_value());
            ASSERT_TRUE(read.front().mStand.mLook.has_value());
            EXPECT_EQ(*read.front().mStand.mEye, *spot.mStand.mEye);
            EXPECT_EQ(*read.front().mStand.mLook, *spot.mStand.mLook);

            // **Clear noon writes neither condition**, so a view pasted from an ordinary window is
            // still free to be measured under whatever a run names.
            EXPECT_FALSE(read.front().mSky.mHour.has_value()) << describeBlock(spot);
            EXPECT_FALSE(read.front().mSky.mWeather.has_value()) << describeBlock(spot);
            EXPECT_FALSE(read.front().mSky.mAir.has_value()) << "no air was known, so none is fixed";

            // **And anything else writes both**, because the light is most of what the frame is: a
            // block pasted from a window flown at dawn in a storm has to bring both with it.
            //
            // **And the air where one is known, to the bit**: ten hours in, which no float holds, and
            // a drift no float holds either.
            RtxTool::Stop dawn = spot;
            dawn.mSky.mHour = 6.5f;
            dawn.mSky.mWeather = Rtx::Weather::Thunderstorm;
            dawn.mSky.mAir = Rtx::AirClock{ .mSky = { .mSeconds = 36000.123456789, .mCloudScroll = 3.9999998f },
                .mCarried = osg::Vec2d(-123456.78901234, 0.1) };

            const std::filesystem::path second = TestingOpenMW::outputFilePath("viewpoint-dawn.cfg");
            {
                std::ofstream out(second);
                out << describeSpot(dawn) << describeBlock(dawn);
            }

            const std::vector<RtxTool::Stop> back = loadViews(second);
            std::filesystem::remove(second);

            ASSERT_EQ(back.size(), 1u);
            ASSERT_TRUE(back.front().mSky.mHour.has_value());
            EXPECT_EQ(*back.front().mSky.mHour, 6.5f);
            ASSERT_TRUE(back.front().mSky.mWeather.has_value());
            EXPECT_EQ(*back.front().mSky.mWeather, Rtx::Weather::Thunderstorm);
            ASSERT_TRUE(back.front().mSky.mAir.has_value());
            EXPECT_EQ(back.front().mSky.mAir->mSky.mSeconds, dawn.mSky.mAir->mSky.mSeconds);
            EXPECT_EQ(back.front().mSky.mAir->mSky.mCloudScroll, dawn.mSky.mAir->mSky.mCloudScroll);
            EXPECT_EQ(back.front().mSky.mAir->mCarried, dawn.mSky.mAir->mCarried);
            EXPECT_TRUE(read.front().mStand.mLamps) << "a lit place writes no lamps";

            // **A place with its lamps off writes so**, or a window flown without them pastes a
            // view that measures them.
            RtxTool::Stop unlit = spot;
            unlit.mStand.mLamps = false;
            const std::filesystem::path third = TestingOpenMW::outputFilePath("viewpoint-unlit.cfg");
            {
                std::ofstream out(third);
                out << describeBlock(unlit);
            }
            const std::vector<RtxTool::Stop> dark = loadViews(third);
            std::filesystem::remove(third);
            ASSERT_EQ(dark.size(), 1u);
            EXPECT_FALSE(dark.front().mStand.mLamps);
            EXPECT_TRUE(dark.front().mStand.mArms.empty()) << "an unarmed place writes no arms";

            // **An armed place writes its weapon**, or a window flown with it pastes a view that
            // measures the world alone.
            RtxTool::Stop armed = spot;
            armed.mStand.mArms = "iron longsword";
            const std::filesystem::path fourth = TestingOpenMW::outputFilePath("viewpoint-armed.cfg");
            {
                std::ofstream out(fourth);
                out << describeBlock(armed);
            }
            const std::vector<RtxTool::Stop> held = loadViews(fourth);
            std::filesystem::remove(fourth);
            ASSERT_EQ(held.size(), 1u);
            EXPECT_EQ(held.front().mStand.mArms, "iron longsword");
        }

        /// A window opened by `--cell` has no view to replace: `stopFor` names the stop after the
        /// cell, and the block opens under a slug of that.
        ///
        /// It still has to load: an id the file cannot take, or a missing note line that the parser
        /// treats as a missing field, would make the printed block unpasteable in exactly the case
        /// where there is nothing to paste over.
        TEST(RtxViewpointTest, aWindowOpenedWithoutAViewStillPrintsOne)
        {
            RtxTool::Stop bare = makeSpot();
            bare.mName.clear();
            bare.mNote.clear();
            const RtxTool::Stop spot = stopFor(bare, StopSky{ .mDay = 0 });

            const std::string block = describeBlock(spot);
            EXPECT_EQ(block.find("[balmora-guild-of-mages]"), 0u) << block;

            // **The stop's name is the cell's with nothing a hash row or a file name cannot hold**:
            // `Balmora, Guild of Mages` as it is but the comma, and `-2,-9` keeping its signs, which
            // the slug's `2-9` shares with `2,9`.
            EXPECT_EQ(spot.mName, "Balmora_ Guild of Mages");
            RtxTool::Stop exterior = bare;
            exterior.mStand.mCell = CellAddress("-2,-9");
            EXPECT_EQ(stopFor(exterior, StopSky{ .mDay = 0 }).mName, "-2_-9");
            exterior.mStand.mCell = CellAddress("Sadrith Mora, Wolverine Hall: Mage's Guild");
            EXPECT_EQ(stopFor(exterior, StopSky{ .mDay = 0 }).mName, "Sadrith Mora_ Wolverine Hall_ Mage's Guild");
            EXPECT_EQ(block.find("note ="), std::string::npos) << "an empty note is left out, not written blank";

            const std::filesystem::path file = TestingOpenMW::outputFilePath("viewpoint-unnamed.cfg");
            {
                std::ofstream out(file);
                out << describeSpot(spot) << block;
            }

            const std::vector<RtxTool::Stop> read = loadViews(file);
            std::filesystem::remove(file);

            ASSERT_EQ(read.size(), 1u);
            EXPECT_EQ(read.front().mName, "balmora-guild-of-mages");
            EXPECT_EQ(read.front().mNote, "");
            EXPECT_EQ(read.front().mStand.mCell, spot.mStand.mCell);
        }

        /// The title's note: the weather and the clock, and the weather crossing in while one is,
        /// so a key that asked for a change is answered in the second and not a minute later.
        TEST(RtxViewpointTest, theTitleNoteSaysWhatIsCrossingIn)
        {
            std::array<char, 48> room{};

            // 14.75 is exact in a float: three quarters past two.
            EXPECT_EQ(writeSkyNote(room, { .mWeather = "Thunderstorm", .mHour = 14.75f }), "Thunderstorm, 14:45");

            // Cut down rather than rounded, so a hundred means arrived: 0.375 is 37.
            EXPECT_EQ(writeSkyNote(
                          room, { .mWeather = "Clear", .mArriving = "Overcast", .mCrossed = 0.375f, .mHour = 14.75f }),
                "Clear \u2192 Overcast 37%, 14:45");

            // The same minute the block spells: 17.2499 is a hair before quarter past, which the
            // block rounds to 17:15 and a clock cut down would have read as 17:14.
            EXPECT_EQ(writeSkyNote(room, { .mWeather = "Ashstorm", .mHour = 17.2499f }), "Ashstorm, 17:15");
            EXPECT_EQ(RtxTool::describeHour(17.2499f), "17:15");

            // The longest note there is fits the room a session keeps, with room to spare.
            const std::string_view longest = writeSkyNote(
                room, { .mWeather = "Thunderstorm", .mArriving = "Blizzard", .mCrossed = 1.0f, .mHour = 23.99f });
            EXPECT_EQ(longest, "Thunderstorm \u2192 Blizzard 100%, 23:59");
            EXPECT_EQ(longest.size(), 37u);
        }

        /// **The keys spell an hour as the harness does.** `sky.lua` answers a key with the hour it
        /// wrote, and the window's title spells the same hour, so the two are held to one answer
        /// where they could part: at each half minute of the day, as a float the world's
        /// `gamehour` holds, and a float either side of it. The Lua reads the float widened, as
        /// the game hands a script its globals.
        TEST(RtxViewpointTest, theKeysSpellAnHourAsTheHarnessDoes)
        {
            sol::state lua;
            lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
            const sol::table spelling
                = lua.script_file(Files::pathToUnicodeString(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
                    / "files" / "rtx" / "vfs" / "scripts" / "rtx" / "hour.lua"));
            const sol::function describe = spelling["describe"];

            std::size_t parted = 0;
            for (int minute = 0; minute < 24 * 60; ++minute)
            {
                const float half = (static_cast<float>(minute) + 0.5f) / 60.0f;
                for (const float hour : { std::nextafter(half, 0.0f), half, std::nextafter(half, 24.0f) })
                {
                    const std::string fromLua = describe(static_cast<double>(hour));
                    if (fromLua != RtxTool::describeHour(hour) && parted++ < 4)
                        ADD_FAILURE() << "at " << hour << ": " << fromLua << " against " << RtxTool::describeHour(hour);
                }
            }
            EXPECT_EQ(parted, 0u);
            EXPECT_EQ(describe(17.2499).get<std::string>(), "17:15");
        }

        /// **The title names the upscaler a frame passes through**: none where nothing upscales,
        /// whichever upscaler the choice came to, and the one it came to at every mode that upscales,
        /// `native` among them.
        TEST(RtxViewpointTest, theTitleNamesTheUpscalerAFramePassesThrough)
        {
            const auto shown = [](const Rtx::Upscale mode, const Rtx::UpscalerChoice upscaler) {
                return RtxTool::describeUpscaler(Rtx::RenderProfile{ .mUpscale = mode, .mUpscaler = upscaler });
            };
            EXPECT_EQ(shown(Rtx::Upscale::Off, Rtx::UpscalerChoice::Fsr), "no");
            EXPECT_EQ(shown(Rtx::Upscale::Off, Rtx::UpscalerChoice::Dlss), "no");
            EXPECT_EQ(shown(Rtx::Upscale::Quality, Rtx::UpscalerChoice::Fsr), "fsr");
            EXPECT_EQ(shown(Rtx::Upscale::Quality, Rtx::UpscalerChoice::Dlss), "dlss");
            EXPECT_EQ(shown(Rtx::Upscale::Native, Rtx::UpscalerChoice::Dlss), "dlss");
        }

    }
}
