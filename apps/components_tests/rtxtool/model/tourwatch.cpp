#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2i>

#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/expectations.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/findingkind.hpp>
#include <apps/rtxtool/model/measurewindow.hpp>
#include <apps/rtxtool/model/tourwatch.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/residency.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/world/frameworld.hpp>

namespace RtxTool
{
    namespace
    {
        /// A place that is its leg and its frame, and a count of how often one was read.
        struct Places final : FindingPlaces
        {
            std::uint32_t mRead = 0;

            FindingPlace read(const std::string_view leg, const std::uint32_t frame, const Rtx::AirClock&) override
            {
                ++mRead;
                return FindingPlace{ .mLeg = std::string(leg), .mFrame = frame };
            }
        };

        /// What a watched frame reads of the game: a scene and the cells the game holds active.
        struct World
        {
            Rtx::SceneIdentities mIdentities;
            Rtx::SceneDesc mScene{ mIdentities };
            std::array<std::int32_t, 3> mCells{};
            std::vector<const void*> mActive{ mCells.data(), &mCells[1] };

            /// A frame that closed a span of `ms`, most of it the walk's, or one ahead of the measurement.
            WatchFrame frame(const std::optional<double> ms) const
            {
                WatchFrame frame{ .mScene = mScene, .mActiveCells = mActive };
                if (ms.has_value())
                {
                    ClosedSpan span;
                    span.mSpend.at(Rtx::Timing::Frame) = *ms;
                    span.mSpend.at(Rtx::Timing::Walk) = *ms - 1.0;
                    frame.mMeasured = span;
                }
                return frame;
            }
        };

        const Finding* find(const FindingTable& table, const FindingKind kind, const std::string_view detail)
        {
            for (const Finding& finding : table.getFindings())
                if (finding.mKind == kind && finding.mDetail == detail)
                    return &finding;
            return nullptr;
        }

        Stop legOf(const std::string_view name)
        {
            Stop stop;
            stop.mName = name;
            return stop;
        }

        /// **A slow frame is one over the step, and a hitch one over it that took half as long again as the frame
        /// before**: under a step of a sixtieth, 16.7 ms, the measured spans 10, 10, 30, 31, 60 and 10 hold three
        /// slow frames and two hitches, 30 after 10 and 60 after 31, counted as one finding at the worst of them with
        /// the cell the game made active on it; 31 after 30 is slow and no hitch. A frame ahead of the measurement
        /// is judged for neither, and a frame that finds nothing reads no place. The leg's end adds the slow frames
        /// as one finding, and the cell it covers that no frame stood whole in the column of.
        TEST(RtxTourWatchTest, aHitchIsASlowFrameHalfAsLongAgainAsTheFrameBefore)
        {
            World world;
            Places places;
            TourWatch watch(1.0f / 60.0f, false, Expectations{});
            Stop leg = legOf("leg-1");
            leg.mActions.mCovers = { osg::Vec2i(3, 0), osg::Vec2i(4, 0) };
            watch.begin(leg);

            watch.frame(world.frame(std::nullopt), places);
            watch.frame(world.frame(10.0), places);
            WatchFrame whole = world.frame(10.0);
            whole.mWhole = true;
            whole.mStandingColumn = 3;
            watch.frame(whole, places);
            EXPECT_EQ(places.mRead, 0u) << "a frame that found nothing read a place";
            EXPECT_TRUE(watch.getTable().getFindings().empty());

            watch.frame(world.frame(30.0), places);
            watch.frame(world.frame(31.0), places);
            world.mActive.push_back(&world.mCells[2]);
            watch.frame(world.frame(60.0), places);
            watch.frame(world.frame(10.0), places);

            ASSERT_EQ(watch.getTable().getFindings().size(), 1u);
            const Finding* const hitch = find(watch.getTable(), FindingKind::Hitch, "led by walk on leg-1");
            ASSERT_NE(hitch, nullptr);
            EXPECT_EQ(hitch->mCount, 2u);
            EXPECT_DOUBLE_EQ(hitch->mWorstMs, 60.0);
            EXPECT_EQ(hitch->mPlace.mFrame, 6u);
            EXPECT_TRUE(hitch->mNote.starts_with("60.0 ms after 31.0, the game loading 1 cells: 0 meshes arrived, "))
                << hitch->mNote;

            watch.endLeg(leg, BenchPlace{}, {}, places);
            const Finding* const slow = find(watch.getTable(), FindingKind::SlowFrames, "leg-1");
            ASSERT_NE(slow, nullptr);
            EXPECT_EQ(slow->mNote, "3 of 6 frames over the 16.7 ms budget") << "the figures apart from what it is";
            EXPECT_DOUBLE_EQ(slow->mWorstMs, 60.0);
            EXPECT_EQ(slow->mPlace.mFrame, 6u);
            EXPECT_NE(find(watch.getTable(), FindingKind::Outrun,
                          "cell 4,0 never stood whole while the player was in its column"),
                nullptr);
            EXPECT_EQ(watch.getTable().getFindings().size(), 3u) << "the cell the player stood whole in was outrun";

            // **A paced run's slow frames are its limiter's**, and no finding; its hitches are still hitches. The
            // next leg counts from nothing.
            TourWatch paced(1.0f / 60.0f, true, Expectations{});
            paced.begin(legOf("leg-2"));
            paced.frame(world.frame(10.0), places);
            paced.frame(world.frame(40.0), places);
            paced.endLeg(legOf("leg-2"), BenchPlace{}, {}, places);
            ASSERT_EQ(paced.getTable().getFindings().size(), 1u);
            EXPECT_NE(find(paced.getTable(), FindingKind::Hitch, "led by walk on leg-2"), nullptr);
        }

        /// **A leg's residency is judged as it was measured**: of five measured frames under the step, two drew a
        /// stand-in, three at the most, a finding placed at the first; a demanded texture below its want for
        /// `sBelowWantFramesMost` measured frames on end is none, and one frame more is one, placed where the run
        /// passed the bound; a frame ahead of the measurement counts for neither. The leg's most starved two textures
        /// and a mesh. Another leg counts from nothing.
        TEST(RtxTourWatchTest, aLegsResidencyIsJudgedAsItWasMeasured)
        {
            World world;
            Places places;
            TourWatch watch(1.0f / 60.0f, false, Expectations{});
            const Stop leg = legOf("leg-1");
            watch.begin(leg);

            WatchFrame ahead = world.frame(std::nullopt);
            ahead.mStandIns = 9;
            ahead.mBelowWant = 1;
            watch.frame(ahead, places);
            for (const std::uint32_t standIns : { 0u, 3u, 0u, 1u, 0u })
            {
                WatchFrame frame = world.frame(10.0);
                frame.mStandIns = standIns;
                watch.frame(frame, places);
            }
            for (std::uint32_t at = 0; at < sBelowWantFramesMost; ++at)
            {
                WatchFrame frame = world.frame(10.0);
                frame.mBelowWant = 2;
                watch.frame(frame, places);
            }
            BenchPlace place;
            Rtx::ResidencyReport starved;
            starved.mStarvedTextures = 2;
            starved.mStarvedStructures = 1;
            place.mResidency.take(starved);
            watch.endLeg(leg, place, {}, places);
            const Finding* const standIn = find(watch.getTable(), FindingKind::StandInDrawn, "leg-1");
            ASSERT_NE(standIn, nullptr);
            EXPECT_EQ(standIn->mNote, "2 of 125 measured frames drew a stand-in, 3 textures at the most");
            EXPECT_EQ(standIn->mPlace.mFrame, 3u);
            EXPECT_EQ(find(watch.getTable(), FindingKind::BelowWant, "leg-1"), nullptr) << "a run at the bound";
            EXPECT_NE(find(watch.getTable(), FindingKind::Starved,
                          "leg-1: 2 textures and 1 meshes fitted nowhere at their floors, at the most"),
                nullptr);

            const Stop next = legOf("leg-2");
            watch.begin(next);
            for (std::uint32_t at = 0; at <= sBelowWantFramesMost; ++at)
            {
                WatchFrame frame = world.frame(10.0);
                frame.mBelowWant = 1;
                watch.frame(frame, places);
            }
            watch.endLeg(next, BenchPlace{}, {}, places);
            const Finding* const below = find(watch.getTable(), FindingKind::BelowWant, "leg-2");
            ASSERT_NE(below, nullptr);
            EXPECT_EQ(below->mNote, "a demanded texture below its want for 121 measured frames on end");
            EXPECT_EQ(below->mPlace.mFrame, 121u);
            EXPECT_EQ(find(watch.getTable(), FindingKind::StandInDrawn, "leg-2"), nullptr) << "counted from nothing";
        }

        /// **What the content gave that the renderer refused is a finding a refusal, by its kind, its name and why**,
        /// met once however many frames follow it, and so is a kernel that stored a value not finite, a leg that failed
        /// and the watcher's mark, each at the frame of its leg it was met on.
        TEST(RtxTourWatchTest, aRefusalAKernelAFailedLegAndAMarkAreFindingsWhereTheyWereMet)
        {
            World world;
            Places places;
            TourWatch watch(1.0f / 60.0f, false, Expectations{});
            watch.begin(legOf("leg-1"));

            world.mScene.refusals().refuse(Rtx::Refused::Texture, "textures/tx_a.dds", "no file");
            watch.frame(world.frame(std::nullopt), places);
            watch.frame(world.frame(std::nullopt), places);
            world.mScene.refusals().refuse(Rtx::Refused::Mesh, "", "past a block");
            const std::array<Rtx::NotFiniteStores, 1> stored{ Rtx::NotFiniteStores{
                .mKernel = "trace.rgen", .mStores = 3, .mFirst = std::nullopt } };
            WatchFrame third = world.frame(std::nullopt);
            third.mNotFinite = stored;
            watch.frame(third, places);

            const Finding* const texture
                = find(watch.getTable(), FindingKind::Refused, "a texture \"textures/tx_a.dds\": no file");
            ASSERT_NE(texture, nullptr);
            EXPECT_EQ(texture->mCount, 1u);
            EXPECT_EQ(texture->mPlace.mFrame, 1u);
            const Finding* const mesh = find(watch.getTable(), FindingKind::Refused, "a mesh: past a block");
            ASSERT_NE(mesh, nullptr);
            EXPECT_EQ(mesh->mPlace.mFrame, 3u);
            const Finding* const kernel = find(watch.getTable(), FindingKind::NotFinite, "trace.rgen");
            ASSERT_NE(kernel, nullptr);
            EXPECT_EQ(kernel->mPlace.mLeg, "leg-1");

            watch.mark(places);
            watch.failLeg(legOf("leg-1"), "the world stalled", places);
            EXPECT_NE(find(watch.getTable(), FindingKind::Mark, "marked on frame 3 of leg-1"), nullptr);
            EXPECT_NE(find(watch.getTable(), FindingKind::LegFailed, "leg-1: the world stalled"), nullptr);
            EXPECT_EQ(watch.getTable().getFindings().size(), 5u);

            // The same texture refused for another reason is another finding.
            world.mScene.refusals().refuse(Rtx::Refused::Texture, "textures/tx_a.dds", "its side is past the device's");
            watch.frame(world.frame(std::nullopt), places);
            EXPECT_NE(find(watch.getTable(), FindingKind::Refused,
                          "a texture \"textures/tx_a.dds\": its side is past the device's"),
                nullptr);
            EXPECT_EQ(watch.getTable().getFindings().size(), 6u);
        }
    }
}
