#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/renderer.hpp>

#include <apps/rtxtool/placement.hpp>
#include <apps/rtxtool/view.hpp>
#include <apps/rtxtool/viewpoint.hpp>
#include <apps/rtxtool/views.hpp>

namespace RtxTool
{
    namespace
    {
        ViewRequest makeRequest()
        {
            ViewRequest request;
            request.mCell = "Balmora, Guild of Fighters";
            request.mFieldOfView = 60.0f;
            request.mWeather = "Ashstorm";
            request.mHour = 17.25f;
            request.mFilter = false;
            return request;
        }

        /// A profiling line has to be pasteable and it has to be exact, so both are asserted.
        ///
        /// The position is deliberately one that rounding would lose, and it survives because the
        /// formatting is shortest-round-trip rather than fixed. The hour avoids sunrise and sunset
        /// for the reason `.notes/ISSUES.md` gives, so that this line is one that actually runs.
        TEST(RtxProfileLineTest, everyConditionTheFrameDependsOnIsInTheLine)
        {
            const ViewRequest request = makeRequest();
            const Rtx::ValidationOptions validation{
                .mEnabled = true, .mSynchronization = true, .mGpuAssisted = false
            };
            const osg::Vec3f origin(-19216.5f, -14896.25f, 160.0f);
            const osg::Vec3f target(-19323.0f, -13903.0f, 109.5f);

            EXPECT_EQ(describeProfile(request, validation, origin, target, 2560, 1440),
                "--cell=\"Balmora, Guild of Fighters\" --pos=-19216.5,-14896.25,160 --look=-19323,-13903,109.5"
                " --fov=60 --size=2560x1440 --weather=Ashstorm --hour=17.25 --filter=false --validation=true"
                " --sync-validation=true --gpu-validation=false");
        }

        /// What the line is for: the tool's own parser reading it back to the same floats.
        TEST(RtxProfileLineTest, thePositionItPrintsIsThePositionItParsesBack)
        {
            const osg::Vec3f origin(-19216.5f, -14896.25f, 160.0f);
            const std::string line
                = describeProfile(makeRequest(), Rtx::ValidationOptions{}, origin, osg::Vec3f(1.0f, 2.0f, 3.0f), 8, 8);

            const std::size_t start = line.find("--pos=") + 6;
            const std::string printed = line.substr(start, line.find(' ', start) - start);

            const std::optional<osg::Vec3f> parsed = parseVec3(printed, "--pos");
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(*parsed, origin);
        }

        /// Each of the fields that is a flag rather than a value, since a line that dropped one
        /// would reproduce a different frame — or the same frame at a different price — while
        /// looking correct.
        TEST(RtxProfileLineTest, theAlbedoViewAndTheLayersEachChangeTheLine)
        {
            ViewRequest shaded = makeRequest();
            ViewRequest albedo = makeRequest();
            albedo.mShowAlbedo = true;

            const osg::Vec3f at(0.0f, 0.0f, 0.0f);
            const osg::Vec3f to(0.0f, 1.0f, 0.0f);
            const Rtx::ValidationOptions off{};
            const Rtx::ValidationOptions gpu{ .mEnabled = true, .mGpuAssisted = true };

            ViewRequest denoised = makeRequest();
            denoised.mFilter = true;

            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(albedo, off, at, to, 8, 8));
            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(denoised, off, at, to, 8, 8));
            EXPECT_NE(describeProfile(shaded, off, at, to, 8, 8), describeProfile(shaded, gpu, at, to, 8, 8));
            EXPECT_TRUE(describeProfile(albedo, off, at, to, 8, 8).ends_with(" --albedo"));
        }

        Viewpoint makeSpot()
        {
            return Viewpoint{
                .mView = "balmora-mages-guild",
                .mNote = "a guild interior, dense with clutter",
                .mCell = "Balmora, Guild of Mages",
                .mOrigin = osg::Vec3f(-283.29843f, -671.29584f, -580.77014f),
                .mTarget = osg::Vec3f(503.60007f, -1265.436f, -747.46844f),
                .mWeather = "Clear",
                .mHour = 12.0f,
            };
        }

        /// Where the camera points, in the two numbers a person can hold in their head.
        ///
        /// **North is +Y and east is +X**, which is Morrowind's convention and not a maths library's
        /// — so the arguments to `atan2` come the other way round, and getting them the usual way
        /// round mirrors every bearing about north-east. Each case below is a direction whose answer
        /// is exact rather than approximate, and the four of them disagree under that swap.
        TEST(RtxViewpointTest, aSpotSaysWhichWayItFaces)
        {
            const auto facing = [](float x, float y, float z) {
                Viewpoint spot;
                spot.mOrigin = osg::Vec3f();
                spot.mTarget = osg::Vec3f(x, y, z);
                return spot;
            };

            // Due north, and the one case the swapped `atan2` also gets right.
            EXPECT_NEAR(facing(0.0f, 100.0f, 0.0f).getBearing(), 0.0f, 1e-3f);
            EXPECT_NEAR(facing(100.0f, 100.0f, 0.0f).getBearing(), 45.0f, 1e-3f) << "north-east";
            EXPECT_NEAR(facing(100.0f, 0.0f, 0.0f).getBearing(), 90.0f, 1e-3f) << "due east";
            // Wrapped rather than negative: a compass has no -90.
            EXPECT_NEAR(facing(-100.0f, 0.0f, 0.0f).getBearing(), 270.0f, 1e-3f) << "due west";

            // Equal parts along and up is forty-five degrees, and the sign is up rather than down.
            EXPECT_NEAR(facing(0.0f, 100.0f, 100.0f).getClimb(), 45.0f, 1e-3f);
            EXPECT_NEAR(facing(0.0f, 100.0f, 0.0f).getClimb(), 0.0f, 1e-3f);
            EXPECT_NEAR(facing(0.0f, 100.0f, -100.0f).getClimb(), -45.0f, 1e-3f);
            // Straight down, where the horizontal part is zero and `asin` is handed exactly -1.
            EXPECT_NEAR(facing(0.0f, 0.0f, -100.0f).getClimb(), -90.0f, 1e-3f);
        }

        /// The readable line, which is the one nobody parses and everybody reads.
        TEST(RtxViewpointTest, theSpotLineSaysWhereAndWhen)
        {
            const std::string line = describeSpot(makeSpot());
            EXPECT_EQ(line,
                "# Balmora, Guild of Mages at -283, -671, -581 \u2014 bearing 127\u00b0, climb -10\u00b0 \u2014 12:00, "
                "Clear\n");

            // A quarter past five in the evening, because a decimal hour is not a time anyone reads.
            Viewpoint evening = makeSpot();
            evening.mHour = 17.25f;
            evening.mWeather = "Ashstorm";
            EXPECT_NE(describeSpot(evening).find("17:15, Ashstorm"), std::string::npos) << describeSpot(evening);
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
            const Viewpoint spot = makeSpot();

            const std::filesystem::path file = std::filesystem::temp_directory_path() / "openmw-rtx-viewpoint-test.cfg";
            {
                std::ofstream out(file);
                out << describeSpot(spot) << describeBlock(spot);
            }

            const std::vector<View> read = loadViews(file);
            std::filesystem::remove(file);

            ASSERT_EQ(read.size(), 1u);
            EXPECT_EQ(read.front().mName, spot.mView);
            EXPECT_EQ(read.front().mNote, spot.mNote);
            EXPECT_EQ(read.front().mCell, spot.mCell);
            ASSERT_TRUE(read.front().mOrigin.has_value());
            ASSERT_TRUE(read.front().mTarget.has_value());
            EXPECT_EQ(*read.front().mOrigin, spot.mOrigin);
            EXPECT_EQ(*read.front().mTarget, spot.mTarget);
        }

        /// A window opened by `--cell` has no view to replace, so the block names one after the cell.
        ///
        /// It still has to load: an id the file cannot take, or a missing note line that the parser
        /// treats as a missing field, would make the printed block unpasteable in exactly the case
        /// where there is nothing to paste over.
        TEST(RtxViewpointTest, aWindowOpenedWithoutAViewStillPrintsOne)
        {
            Viewpoint spot = makeSpot();
            spot.mView.clear();
            spot.mNote.clear();

            const std::string block = describeBlock(spot);
            EXPECT_EQ(block.find("[balmora-guild-of-mages]"), 0u) << block;
            EXPECT_EQ(block.find("note ="), std::string::npos) << "an empty note is left out, not written blank";

            const std::filesystem::path file
                = std::filesystem::temp_directory_path() / "openmw-rtx-viewpoint-unnamed.cfg";
            {
                std::ofstream out(file);
                out << describeSpot(spot) << block;
            }

            const std::vector<View> read = loadViews(file);
            std::filesystem::remove(file);

            ASSERT_EQ(read.size(), 1u);
            EXPECT_EQ(read.front().mName, "balmora-guild-of-mages");
            EXPECT_EQ(read.front().mNote, "");
            EXPECT_EQ(read.front().mCell, spot.mCell);
        }
    }
}
