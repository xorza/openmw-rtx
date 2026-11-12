#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/rtxtool/harnessfolder.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/blockfile.hpp>
#include <apps/rtxtool/model/viewfile.hpp>
#include <apps/rtxtool/verbs.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        TEST(RtxBenchSuiteTest, aSuiteFileIsSectionsOfViewNames)
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("suite-test.cfg");
            {
                std::ofstream written(file);
                written << "[quick]\n"
                           "note = two of them\n"
                           "views = balmora, vivec\n"
                           "settled = false\n"
                           "\n"
                           "[one]\n"
                           "views = arkngthand\n";
            }

            const std::vector<BenchSuite> suites = loadSuites(file);
            ASSERT_EQ(suites.size(), 2u);

            const BenchSuite* quick = findSuite(suites, "quick");
            ASSERT_NE(quick, nullptr);
            EXPECT_EQ(quick->mNote, "two of them");
            EXPECT_EQ(quick->mViews, (std::vector<std::string>{ "balmora", "vivec" }));
            EXPECT_EQ(quick->mSettled, std::optional(false)) << "a suite that times the streaming path says so";

            const BenchSuite* one = findSuite(suites, "one");
            ASSERT_NE(one, nullptr);
            EXPECT_EQ(one->mViews, (std::vector<std::string>{ "arkngthand" }));
            EXPECT_TRUE(one->mNote.empty()) << "a note is optional";
            EXPECT_EQ(one->mSettled, std::nullopt) << "and so is settled, which the frame clock then decides";

            EXPECT_EQ(findSuite(suites, "nothing"), nullptr);

            std::filesystem::remove(file);
        }

        /// A suite with no views in it, and a field nobody defined.
        ///
        /// **Both throw rather than being skipped.** A profiling run costs minutes, and the two ways
        /// to waste them are a suite that silently runs nothing and a typo that silently drops a
        /// place out of the list.
        TEST(RtxBenchSuiteTest, aMalformedSuiteSaysSoRatherThanRunningNothing)
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("suite-bad.cfg");
            {
                std::ofstream written(file);
                written << "[empty]\nnote = nothing here\n";
            }
            EXPECT_THROW(loadSuites(file), std::runtime_error) << "a suite naming no views";

            {
                std::ofstream written(file);
                written << "[typo]\nveiws = balmora\n";
            }
            EXPECT_THROW(loadSuites(file), std::runtime_error) << "a field nobody defined";

            std::filesystem::remove(file);
            EXPECT_THROW(loadSuites(file), std::runtime_error) << "a file that is not there";
        }

        /// Every place the shipped suites name is a place the shipped views file has.
        ///
        /// **The one way this pair can be wrong that nothing else catches**: a view renamed in `views.cfg`
        /// leaves `benches.cfg` naming something that no longer exists, found by the run somebody started
        /// and walked away from. **Read where the build put them**: the harness's folder, which holds its
        /// scripts and the shaders with their source too, apart from the resources an install carries.
        TEST(RtxBenchSuiteTest, everySuiteNamesViewsThatExist)
        {
            const std::filesystem::path harness = harnessDirectory();
            const std::filesystem::path resources
                = std::filesystem::path(OPENMW_RTX_SHADER_DIR).parent_path().parent_path();
            EXPECT_TRUE(std::filesystem::is_regular_file(keysDirectory() / "rtxtool.omwscripts"));
            // **A verb that measures reads the game's modules**, and every other counts: `bench` and
            // `film` time the game's kernels, and the rest are where a NaN is found.
            const std::array<std::pair<Verbs, bool>, 8> counts{ { { Verbs::Info, true }, { Verbs::Scene, true },
                { Verbs::Shot, true }, { Verbs::View, true }, { Verbs::Bench, false }, { Verbs::Check, true },
                { Verbs::Film, false }, { Verbs::Noise, true } } };
            Verbs covered = Verbs::None;
            for (const auto& [verb, census] : counts)
            {
                covered = covered | verb;
                for (const bool source : { false, true })
                {
                    const Rtx::ShaderSet set = shadersFor(resources, verb, source);
                    EXPECT_TRUE(std::filesystem::is_directory(set.mDirectory)) << set.mDirectory;
                    EXPECT_EQ(set.mCensus, census) << verbName(verb);
                    EXPECT_EQ(set.mDirectory.parent_path() == harness, source || census) << set.mDirectory;
                }
            }
            EXPECT_EQ(covered, Verbs::Every) << "a verb this table has no row for";
            EXPECT_EQ(shadersFor(resources, Verbs::Bench, false).mDirectory, Rtx::shaderDirectory(resources));
            const std::filesystem::path within = harness.lexically_relative(resources);
            EXPECT_TRUE(within.empty() || *within.begin() == "..") << "the harness's folder inside the resources";

            const std::vector<RtxTool::Stop> views = loadViews(viewsFile());
            const std::vector<BenchSuite> suites = loadSuites(suitesFile());

            EXPECT_NE(findSuite(suites, "default"), nullptr) << "`bench` with no arguments runs [default]";

            for (const BenchSuite& suite : suites)
                for (const std::string& name : suite.mViews)
                    EXPECT_NE(findView(views, name), nullptr)
                        << "suite \"" << suite.mName << "\" names \"" << name << "\", which views.cfg has not got";
        }
    }

    namespace
    {
        /// Writes `text` to a scratch view file, reads it back, and removes the file.
        ///
        /// **In this run's own directory, `TestingOpenMW::outputDir`, and never under a fixed name
        /// in the temp directory**: a run sharded with `GTEST_TOTAL_SHARDS` deals a fixture's tests
        /// out to concurrent processes, so a file two tests share by name is a file one shard
        /// removes under the other.
        std::vector<RtxTool::Stop> readViews(std::string_view text)
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("route-test.cfg");
            {
                std::ofstream out(file);
                out << text;
            }

            struct Remove
            {
                std::filesystem::path mFile;
                ~Remove() { std::filesystem::remove(mFile); }
            } removed{ file };

            return loadViews(file);
        }

        /// A route resolves to the coordinates of the view it names, and both halves are required.
        ///
        /// **The destination is copied at load and never looked up again**, so this is the one place
        /// that can get the pairing wrong — and getting it wrong flies the camera somewhere else,
        /// which a benchmark reports as a different number rather than as an error.
        TEST(RtxViewsTest, aRouteTakesItsDestinationFromTheViewItNames)
        {
            const std::vector<RtxTool::Stop> read = readViews(R"(
[start]
cell = -3,-2
pos = 100, 200, 300
look = 100, 300, 300
to = finish
speed = 1500

[finish]
cell = -1,-2
pos = 8292, 200, 700
look = 8292, 300, 700
)");

            ASSERT_EQ(read.size(), std::size_t{ 2 });
            const RtxTool::Stop* start = findView(read, "start");
            ASSERT_NE(start, nullptr);
            ASSERT_TRUE(start->mSchedule.mRoute.has_value());

            // The destination view is resolved when the file is read, so what a route carries is
            // where it ends rather than the name of a place to look up later.
            EXPECT_EQ(start->mSchedule.mRoute->mTo, osg::Vec3f(8292.0f, 200.0f, 700.0f));
            EXPECT_EQ(start->mSchedule.mRoute->mLookTo, osg::Vec3f(8292.0f, 300.0f, 700.0f));
            EXPECT_EQ(start->mSchedule.mRoute->mSpeed, 1500.0f);

            // The destination is an ordinary view and goes nowhere itself.
            const RtxTool::Stop* finish = findView(read, "finish");
            ASSERT_NE(finish, nullptr);
            EXPECT_FALSE(finish->mSchedule.mRoute.has_value());
        }

        /// Every way of half-writing a route is a refusal rather than a camera that stands still.
        TEST(RtxViewsTest, aHalfWrittenRouteIsRefused)
        {
            constexpr std::string_view sEnd = "\n[finish]\ncell = -1,-2\npos = 8292, 0, 0\nlook = 8292, 100, 0\n";

            EXPECT_THROW(
                readViews(std::string("[start]\ncell = -3,-2\nto = finish\n") + std::string(sEnd)), std::runtime_error)
                << "a destination with no speed";

            EXPECT_THROW(readViews("[start]\ncell = -3,-2\nspeed = 1500\n"), std::runtime_error)
                << "a speed with nowhere to go";

            EXPECT_THROW(readViews("[start]\ncell = -3,-2\nto = nowhere\nspeed = 1500\n"), std::runtime_error)
                << "a destination that is not a view";

            EXPECT_THROW(readViews("[start]\ncell = -3,-2\nto = finish\nspeed = 1500\n\n[finish]\ncell = -1,-2\n"),
                std::runtime_error)
                << "a destination with no coordinates of its own to arrive at";

            EXPECT_THROW(readViews(std::string("[start]\ncell = -3,-2\nto = finish\nspeed = -1\n") + std::string(sEnd)),
                std::runtime_error)
                << "a speed that goes backwards";

            EXPECT_THROW(
                readViews(std::string("[start]\ncell = -3,-2\nto = finish\nspeed = quickly\n") + std::string(sEnd)),
                std::runtime_error)
                << "a speed that is not a number";
        }

        /// A place says what it is looked at under, and takes where it stands from another place.
        ///
        /// **The pair is the point.** A dawn row and a noon row of one camera mean something beside
        /// each other only where the camera is identical by construction — coordinates copied by
        /// hand drift the first time either is moved, and the pair then reads two cameras as a
        /// difference the hour made.
        TEST(RtxViewsTest, aPlaceFixesItsConditionsAndTakesItsCameraFromWhatItIsLike)
        {
            const std::vector<RtxTool::Stop> read = readViews(R"(
[ship]
cell = -2,-9
pos = 100, 200, 300
look = 100, 300, 300

[ship-dawn]
like = ship
hour = 6.5

[ship-overcast]
like = ship
weather = Overcast

[ship-dusk-from-the-mast]
like = ship
pos = 100, 200, 900
hour = 19.25

[ship-unlit]
like = ship
lamps = false

[ship-armed]
like = ship
arms = iron longsword
)");

            ASSERT_EQ(read.size(), std::size_t{ 6 });

            // A place that fixes nothing keeps both conditions absent, which is what lets a run name
            // them.
            const RtxTool::Stop* noon = findView(read, "ship");
            ASSERT_NE(noon, nullptr);
            EXPECT_FALSE(noon->mSky.mHour.has_value());
            EXPECT_FALSE(noon->mSky.mWeather.has_value());

            const RtxTool::Stop* dawn = findView(read, "ship-dawn");
            ASSERT_NE(dawn, nullptr);
            ASSERT_TRUE(dawn->mSky.mHour.has_value());
            EXPECT_EQ(*dawn->mSky.mHour, 6.5f);

            // **The two conditions are independent**: an hour fixed leaves the sky free and a sky
            // fixed leaves the hour free, so a place may name either alone.
            EXPECT_FALSE(dawn->mSky.mWeather.has_value());

            const RtxTool::Stop* overcast = findView(read, "ship-overcast");
            ASSERT_NE(overcast, nullptr);
            ASSERT_TRUE(overcast->mSky.mWeather.has_value());
            ASSERT_TRUE(overcast->mStand.mEye.has_value());
            EXPECT_EQ(*overcast->mSky.mWeather, Rtx::Weather::Overcast);
            EXPECT_FALSE(overcast->mSky.mHour.has_value());
            EXPECT_EQ(*overcast->mStand.mEye, osg::Vec3f(100.0f, 200.0f, 300.0f));

            // The whole camera, taken rather than restated.
            EXPECT_EQ(dawn->mStand.mCell.getSpelling(), "-2,-9");
            ASSERT_TRUE(dawn->mStand.mEye.has_value());
            ASSERT_TRUE(dawn->mStand.mLook.has_value());
            EXPECT_EQ(*dawn->mStand.mEye, osg::Vec3f(100.0f, 200.0f, 300.0f));
            EXPECT_EQ(*dawn->mStand.mLook, osg::Vec3f(100.0f, 300.0f, 300.0f));

            // **What a borrower states itself is kept**, so a place may sit somewhere else under
            // the same cell and the same view of it.
            const RtxTool::Stop* mast = findView(read, "ship-dusk-from-the-mast");
            ASSERT_NE(mast, nullptr);
            ASSERT_TRUE(mast->mStand.mEye.has_value());
            EXPECT_EQ(*mast->mStand.mEye, osg::Vec3f(100.0f, 200.0f, 900.0f)) << "its own position was overwritten";
            EXPECT_EQ(*mast->mStand.mLook, osg::Vec3f(100.0f, 300.0f, 300.0f)) << "the look it did not state";
            EXPECT_EQ(mast->mStand.mCell.getSpelling(), "-2,-9");

            // **The lamps light a place unless it says they do not**, and a likeness does not take
            // that from the place it is like.
            EXPECT_TRUE(noon->mStand.mLamps);
            const RtxTool::Stop* unlit = findView(read, "ship-unlit");
            ASSERT_NE(unlit, nullptr);
            EXPECT_FALSE(unlit->mStand.mLamps);
            EXPECT_TRUE(dawn->mStand.mLamps);

            // **A place is seen through the body's own eye only where it says so**, by the weapon's
            // id as written, spaces and all.
            EXPECT_TRUE(noon->mStand.mArms.empty());
            const RtxTool::Stop* armed = findView(read, "ship-armed");
            ASSERT_NE(armed, nullptr);
            EXPECT_EQ(armed->mStand.mArms, "iron longsword");
            EXPECT_EQ(*armed->mStand.mEye, osg::Vec3f(100.0f, 200.0f, 300.0f));
        }

        /// Every way of writing a condition or a likeness wrong is a refusal.
        ///
        /// A view that quietly stood at another hour, under another sky, or in another place would
        /// report a number against a frame nobody asked for — which is the failure this whole file
        /// exists to stop.
        TEST(RtxViewsTest, aConditionOrALikenessThatCannotBeMeantIsRefused)
        {
            constexpr std::string_view sShip = "[ship]\ncell = -2,-9\npos = 1, 2, 3\nlook = 1, 9, 3\n";

            EXPECT_THROW(readViews(std::string(sShip) + "[dawn]\nlike = ship\nhour = dawn\n"), std::runtime_error)
                << "an hour that is not a number";

            // **The whole of the field, so a number with a letter after it is a typo and not an
            // hour.** A view that stood at six because `6h` began with a six would report a figure
            // against a frame nobody asked for.
            EXPECT_THROW(readViews(std::string(sShip) + "[dawn]\nlike = ship\nhour = 6h\n"), std::runtime_error)
                << "an hour with a letter after it";

            EXPECT_THROW(readViews(std::string(sShip) + "[dawn]\nlike = ship\nhour = 24\n"), std::runtime_error)
                << "an hour off the end of the clock";

            EXPECT_THROW(readViews(std::string(sShip) + "[dawn]\nlike = ship\nhour = -1\n"), std::runtime_error)
                << "an hour before the day began";

            // Midnight and a moment before the next one are both hours of the day.
            EXPECT_NO_THROW(readViews(std::string(sShip) + "[dark]\nlike = ship\nhour = 0\n"));
            EXPECT_NO_THROW(readViews(std::string(sShip) + "[late]\nlike = ship\nhour = 23.99\n"));

            // **A weather is one of the ten, in any case, as the game reads a weather's id**, and
            // refused at the file rather than at the frame, when the run has staged a cell for it.
            // It is kept as the content files spell it.
            EXPECT_THROW(readViews(std::string(sShip) + "[grim]\nlike = ship\nweather = Drizzle\n"), std::runtime_error)
                << "a weather that is none of the ten";

            const std::vector<RtxTool::Stop> lowered
                = readViews(std::string(sShip) + "[grim]\nlike = ship\nweather = overcast\n");
            ASSERT_EQ(lowered.size(), 2u);
            EXPECT_EQ(lowered[1].mSky.mWeather, std::optional<Rtx::Weather>(Rtx::Weather::Overcast))
                << "a weather kept as the file spelled it";

            EXPECT_NO_THROW(readViews(std::string(sShip) + "[grim]\nlike = ship\nweather = Thunderstorm\n"));

            EXPECT_THROW(readViews(std::string(sShip) + "[unlit]\nlike = ship\nlamps = off\n"), std::runtime_error)
                << "lamps that are not true or false";
            EXPECT_THROW(readViews(std::string(sShip) + "[armed]\nlike = ship\narms =\n"), std::runtime_error)
                << "arms that name no weapon";
            EXPECT_THROW(readViews("[armed]\ncell = -2,-9\narms = iron longsword\n"), std::runtime_error)
                << "arms with no eye to see them through";

            EXPECT_THROW(readViews(std::string(sShip) + "[dawn]\nlike = nowhere\n"), std::runtime_error)
                << "like a view that is not there";

            // **A camera written badly is refused by the view's name, and an empty one is written
            // badly.** Read as no camera, it stood the view outside its cell looking at the middle.
            const auto refusal = [](std::string_view text) {
                try
                {
                    readViews(text);
                }
                catch (const std::runtime_error& error)
                {
                    return std::string(error.what());
                }
                return std::string("nothing was refused");
            };
            EXPECT_TRUE(refusal("[ship]\ncell = -2,-9\npos = 1,2\n")
                    .ends_with(":3: pos \"1,2\" is not three numbers separated by commas"));
            EXPECT_TRUE(refusal("[ship]\ncell = -2,-9\nlook =\n")
                    .ends_with(":3: look \"\" is not three numbers separated by commas"));

            EXPECT_THROW(readViews("[dawn]\nlike = dawn\n"), std::runtime_error) << "like itself";

            // **"all" means every view, so no view may be called it**, and a list that names nothing
            // is refused rather than read as every view: `--views=,` ran the whole file.
            EXPECT_THROW(readViews("[all]\ncell = -2,-9\n"), std::runtime_error) << "a view called all";
            const std::vector<RtxTool::Stop> two = readViews(std::string(sShip) + "[dawn]\nlike = ship\n");
            EXPECT_THROW(RtxTool::chooseViews(two, {}), std::runtime_error) << "a list that names nothing";
            EXPECT_EQ(RtxTool::chooseViews(two, { "all" }).size(), 2u);
            EXPECT_EQ(RtxTool::chooseViews(two, { "dawn" }).size(), 1u);
            EXPECT_THROW(RtxTool::chooseViews(two, { "dawn", "ship", "dawn" }), std::runtime_error)
                << "a view named twice, whose hashes the run would then refuse to read";

            // **The command line refuses by the same rules**, `checkHour`, `checkDay` and
            // `checkWeather`, which is what these are.
            EXPECT_EQ(RtxTool::checkHour(24.0f).error(), "is not from 0 up to but not including 24");
            EXPECT_FALSE(RtxTool::checkHour(-0.01f).isOk());
            EXPECT_TRUE(RtxTool::checkHour(0.0f).isOk());
            EXPECT_TRUE(RtxTool::checkHour(23.99f).isOk());
            EXPECT_EQ(RtxTool::checkDay(-3).error(), "is before the first day");
            EXPECT_TRUE(RtxTool::checkDay(0).isOk());
            EXPECT_EQ(RtxTool::checkWeather("Rian").error(), "is none of the weathers the content files name");
            EXPECT_TRUE(RtxTool::checkWeather("Rain").isOk());
            EXPECT_EQ(RtxTool::listWeathers(),
                "Clear, Cloudy, Foggy, Overcast, Rain, Thunderstorm, Ashstorm, Blight, Snow, Blizzard");

            EXPECT_THROW(
                readViews(std::string(sShip) + "[dawn]\nlike = ship\n[later]\nlike = dawn\n"), std::runtime_error)
                << "a chain, which would make the order things are read in decide what a view is";
        }

        /// Which condition wins, which is the one rule every command that draws a view reads.
        ///
        /// **A settled place is what a run stands at, so neither condition is optional on it.** The
        /// file's own entry keeps its optionals, because a listing prints only what a view fixes.
        TEST(RtxViewsTest, theConditionOnTheCommandLineBeatsTheOneAPlaceFixes)
        {
            const RtxTool::Stop entry{
                .mName = "dawn-deck",
                .mNote = "a deck at dawn",
                .mStand = { .mCell = CellAddress("Vivec, Foreign Quarter"),
                    .mEye = osg::Vec3f(1.0f, 2.0f, 3.0f),
                    .mLook = osg::Vec3f(4.0f, 5.0f, 6.0f) },
                .mSky = { .mHour = 6.5f,
                    .mWeather = Rtx::Weather::Overcast,
                    .mAir = Rtx::AirClock{ .mSky = { .mSeconds = 100.0 } } },
                .mSchedule = { .mRoute = RtxTool::Route{ .mTo = osg::Vec3f(7.0f, 8.0f, 9.0f),
                                   .mLookTo = osg::Vec3f(),
                                   .mSpeed = 400.0f } },
            };

            const RtxTool::Stop bare{ .mStand = { .mCell = CellAddress("-2,-9") } };

            const StopSky silent{ .mDay = 0 };
            const StopSky rain{ .mHour = 9.0f,
                .mDay = 0,
                .mWeather = Rtx::Weather::Rain,
                .mAir = Rtx::AirClock{ .mSky = { .mSeconds = 200.0 } } };

            // Neither says anything: noon under a clear sky, which is how a picture of a place is
            // taken, and the air wherever the run's frames carry it.
            EXPECT_EQ(stopFor(bare, silent).mSky.mHour, sDefaultHour);
            EXPECT_EQ(stopFor(bare, silent).mSky.mWeather, sDefaultWeather);
            EXPECT_FALSE(stopFor(bare, silent).mSky.mAir.has_value());

            // Only the place: the place decides, which is what makes a view id one frame.
            EXPECT_EQ(stopFor(entry, silent).mSky.mHour, 6.5f);
            EXPECT_EQ(stopFor(entry, silent).mSky.mWeather, Rtx::Weather::Overcast);
            EXPECT_EQ(stopFor(entry, silent).mSky.mAir->mSky.mSeconds, 100.0);

            // The command line, over a place that fixes one and over a place that does not.
            EXPECT_EQ(stopFor(entry, rain).mSky.mHour, 9.0f);
            EXPECT_EQ(stopFor(entry, rain).mSky.mWeather, Rtx::Weather::Rain);
            EXPECT_EQ(stopFor(entry, rain).mSky.mAir->mSky.mSeconds, 200.0);
            EXPECT_EQ(stopFor(bare, rain).mSky.mHour, 9.0f);
            EXPECT_EQ(stopFor(bare, rain).mSky.mWeather, Rtx::Weather::Rain);
            EXPECT_EQ(stopFor(bare, rain).mSky.mAir->mSky.mSeconds, 200.0);

            // And the three answers differ, so the rule is doing something.
            EXPECT_NE(stopFor(entry, silent).mSky.mHour, stopFor(entry, rain).mSky.mHour);
            EXPECT_NE(stopFor(bare, silent).mSky.mHour, stopFor(entry, silent).mSky.mHour);

            // Everything that is not a condition is the entry's, unchanged.
            const RtxTool::Stop settled = stopFor(entry, StopSky{ .mDay = 3 });
            EXPECT_EQ(settled.mName, "dawn-deck");
            EXPECT_EQ(settled.mNote, "a deck at dawn");
            EXPECT_EQ(settled.mStand.mCell.getSpelling(), "Vivec, Foreign Quarter");
            EXPECT_EQ(settled.mStand.mEye, entry.mStand.mEye);
            EXPECT_EQ(settled.mStand.mLook, entry.mStand.mLook);
            EXPECT_EQ(settled.mSky.mDay, 3);
            ASSERT_TRUE(settled.mSchedule.mRoute.has_value());
            EXPECT_EQ(settled.mSchedule.mRoute->mSpeed, 400.0f);

            // **A view with no id of its own is named after its cell**, because a report row and a
            // hash file are keyed on the name and neither can be keyed on nothing: the comma, which a
            // hash row cannot hold, as `_`.
            EXPECT_EQ(stopFor(bare, silent).mName, "-2_-9");
        }
    }
}
