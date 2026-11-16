#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/exitstatus.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/measurewindow.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    namespace
    {
        FindingPlace placeAt(const std::string& leg, const std::uint32_t frame)
        {
            FindingPlace place{ .mLeg = leg, .mFrame = frame };
            place.mStood.mName = leg;
            return place;
        }

        /// **Each kind has one name, a fault fails on its own and a cost does not.**
        TEST(RtxFindingTest, eachKindHasOneNameAndAVerdict)
        {
            for (std::size_t at = 0; at < static_cast<std::size_t>(FindingKind::Count); ++at)
            {
                const auto kind = static_cast<FindingKind>(at);
                EXPECT_EQ(findingNamed(findingName(kind)), kind) << findingName(kind);
            }
            EXPECT_EQ(findingNamed("nonesuch"), std::nullopt);
            EXPECT_TRUE(isFault(FindingKind::NotFinite));
            EXPECT_TRUE(isFault(FindingKind::Outrun));
            EXPECT_FALSE(isFault(FindingKind::Hitch));
            EXPECT_FALSE(isFault(FindingKind::Mark));
        }

        /// **The same kind and detail is counted once, with its first place and its worst figure**, and two details
        /// of one kind are two findings.
        TEST(RtxFindingTest, aFindingIsCountedOnceWithItsFirstPlace)
        {
            FindingTable table;
            EXPECT_TRUE(table.takesPlace(FindingKind::NotFinite, "bounce"));
            table.add(FindingKind::NotFinite, "bounce", placeAt("leg-1", 10));
            EXPECT_FALSE(table.takesPlace(FindingKind::NotFinite, "bounce", 9.0)) << "placed at its first";
            EXPECT_TRUE(table.takesPlace(FindingKind::NotFinite, "visibility"));
            EXPECT_TRUE(table.takesPlace(FindingKind::Refused, "bounce")) << "a kind apart";
            table.add(FindingKind::NotFinite, "bounce", placeAt("leg-3", 20), FindingMet{ .mMs = 4.0 });
            table.count(FindingKind::NotFinite, "bounce", 2.0);
            table.add(FindingKind::NotFinite, "visibility", placeAt("leg-2", 5));

            ASSERT_EQ(table.getFindings().size(), 2u);
            const Finding& bounce = table.getFindings()[0];
            EXPECT_EQ(bounce.mCount, 3u);
            EXPECT_EQ(bounce.mPlace.mLeg, "leg-1");
            EXPECT_EQ(bounce.mPlace.mFrame, 10u);
            EXPECT_EQ(bounce.mWorstMs, 4.0);
            EXPECT_EQ(table.getDropped(FindingKind::NotFinite), 0u);
        }

        /// **A hitch is placed at its worst, with what it came to there**: a worse figure moves the place and the
        /// note, a better one counts and moves nothing, and one as bad as the worst is not worse.
        TEST(RtxFindingTest, aHitchIsPlacedAtItsWorst)
        {
            FindingTable table;
            const std::string_view detail = "led by update on leg-1";
            table.add(FindingKind::Hitch, detail, placeAt("leg-1", 10), FindingMet{ .mMs = 40.0, .mNote = "40" });
            EXPECT_FALSE(table.takesPlace(FindingKind::Hitch, detail, 30.0));
            EXPECT_FALSE(table.takesPlace(FindingKind::Hitch, detail, 40.0)) << "as bad is not worse";
            EXPECT_TRUE(table.takesPlace(FindingKind::Hitch, detail, 41.0));
            table.count(FindingKind::Hitch, detail, 30.0);
            table.add(FindingKind::Hitch, detail, placeAt("leg-1", 30), FindingMet{ .mMs = 90.0, .mNote = "90" });

            ASSERT_EQ(table.getFindings().size(), 1u);
            const Finding& hitch = table.getFindings()[0];
            EXPECT_EQ(hitch.mCount, 3u);
            EXPECT_EQ(hitch.mPlace.mFrame, 30u);
            EXPECT_EQ(hitch.mNote, "90");
            EXPECT_EQ(hitch.mWorstMs, 90.0);
        }

        /// **A hitch is counted under its leg and the stretch it spent most in, and says what it came to**: the key
        /// refills its buffer, a share of a stretch never leads, and the cells the game loaded are said only where
        /// it loaded some.
        TEST(RtxFindingTest, aHitchIsKeyedByItsLegAndItsLeadingStretch)
        {
            Rtx::FrameSpend spend;
            spend.at(Rtx::Timing::Frame) = 77.4;
            spend.at(Rtx::Timing::Update) = 52.4;
            spend.at(Rtx::Timing::Finish) = 10.3;
            spend.at(Rtx::Timing::Wait) = 10.0;
            spend.at(Rtx::Timing::Walk) = 4.5;
            spend.at(Rtx::Timing::Sleep) = 60.0;
            std::string key = "left over";
            describeHitchKey(key, "leg-93-closing--1,-15", spend);
            EXPECT_EQ(key, "led by update on leg-93-closing--1,-15") << "Sleep is a share of Update, not a stretch";
            spend.at(Rtx::Timing::Walk) = 60.0;
            describeHitchKey(key, "leg-2", spend);
            EXPECT_EQ(key, "led by walk on leg-2");

            const ClosedSpan span{ .mSpend = spend, .mArrived = 3 };
            EXPECT_EQ(describeHitch(span, 24.4, 0),
                "77.4 ms after 24.4: 3 meshes arrived, walk 60.0 update 52.4 finish 10.3");
            EXPECT_EQ(describeHitch(span, 24.4, 2),
                "77.4 ms after 24.4, the game loading 2 cells: 3 meshes arrived, walk 60.0 update 52.4 finish 10.3");
        }

        /// **Past its bound a finding is counted as dropped, by kind**, so the run's memory holds whatever the content
        /// does; a cost never takes a fault's room, and a fault dropped fails the run.
        TEST(RtxFindingTest, pastItsBoundAFindingIsCountedAsDropped)
        {
            FindingTable table;
            for (std::size_t at = 0; at < FindingTable::sMost + 3; ++at)
                table.add(FindingKind::Hitch, std::to_string(at), placeAt("leg-1", 0));
            EXPECT_EQ(table.getFindings().size(), FindingTable::sMost);
            EXPECT_EQ(table.getDropped(FindingKind::Hitch), 3u);
            EXPECT_FALSE(table.takesPlace(FindingKind::Hitch, "a hitch past the bound", 500.0));
            table.count(FindingKind::Hitch, "a hitch past the bound", 500.0);
            EXPECT_EQ(table.getDropped(FindingKind::Hitch), 4u);
            table.add(FindingKind::Hitch, "0", placeAt("leg-2", 0));
            EXPECT_EQ(table.getFindings()[0].mCount, 2u) << "one held is counted however full the table";
            EXPECT_EQ(table.statusUnder({}), ExitStatus::Passed) << "costs alone";

            EXPECT_TRUE(table.takesPlace(FindingKind::Refused, "a texture")) << "the costs took no fault's room";
            table.add(FindingKind::Refused, "a texture", placeAt("leg-9", 0));
            EXPECT_EQ(table.getFindings().size(), FindingTable::sMost + 1);

            FindingTable faults;
            for (std::size_t at = 0; at < FindingTable::sMost + 2; ++at)
                faults.add(FindingKind::Refused, std::to_string(at), placeAt("leg-1", 0));
            faults.add(FindingKind::NotFinite, "bounce", placeAt("leg-2", 0));
            EXPECT_EQ(faults.getDropped(FindingKind::Refused), 2u);
            EXPECT_EQ(faults.getDropped(FindingKind::NotFinite), 1u) << "dropped by its own kind";
            EXPECT_EQ(faults.getDropped(FindingKind::LogError), 0u);
            EXPECT_EQ(faults.getFindings().size(), FindingTable::sMost) << "nothing held for what was dropped";
            EXPECT_TRUE(faults.takesPlace(FindingKind::Mark, "here")) << "the faults took no cost's room";
        }

        /// **A fault fails the run, a cost only where the line names it.**
        TEST(RtxFindingTest, aFaultFailsTheRunAndACostWhereNamed)
        {
            FindingTable costs;
            costs.add(FindingKind::Hitch, "120 ms", placeAt("leg-1", 0), FindingMet{ .mMs = 120.0 });
            costs.add(FindingKind::Mark, "here", placeAt("leg-1", 0));
            EXPECT_EQ(costs.statusUnder({}), ExitStatus::Passed);
            const std::vector<FindingKind> hitches{ FindingKind::Hitch };
            EXPECT_EQ(costs.statusUnder(hitches), ExitStatus::Failed);

            FindingTable fault;
            fault.add(FindingKind::Outrun, "cell 3,1", placeAt("leg-1", 0));
            EXPECT_EQ(fault.statusUnder({}), ExitStatus::Failed);
            EXPECT_EQ(FindingTable{}.statusUnder({}), ExitStatus::Passed);

            // A cost the line names fails where it was only counted past the bound.
            FindingTable full;
            for (std::size_t at = 0; at <= FindingTable::sMost; ++at)
                full.add(FindingKind::Mark, std::to_string(at), placeAt("leg-1", 0));
            full.add(FindingKind::Hitch, "past the bound", placeAt("leg-2", 0), FindingMet{ .mMs = 120.0 });
            EXPECT_EQ(full.getDropped(FindingKind::Hitch), 1u);
            EXPECT_EQ(full.statusUnder({}), ExitStatus::Passed);
            EXPECT_EQ(full.statusUnder(hitches), ExitStatus::Failed);
        }

        /// **The report puts the faults first and the costs worst first**, each once with its count and where it is
        /// placed, under its hour and the sky as it crossed where they are known, what it came to there, the block
        /// to stand there again, and what fell past the bound by kind.
        TEST(RtxFindingTest, theReportPutsTheFaultsFirst)
        {
            FindingTable table;
            FindingPlace hitch = placeAt("leg-2-land-0,1", 40);
            hitch.mStood.mStand.mCell = CellAddress::exterior(0, 1);
            hitch.mStood.mStand.mEye = osg::Vec3f(1, 2, 3);
            hitch.mStood.mStand.mLook = osg::Vec3f(1, 3, 3);
            hitch.mStood.mSky.mHour = 18.5f;
            hitch.mStood.mSky.mDay = 2;
            hitch.mSky = FindingSky{ .mLeft = Rtx::Weather::Rain, .mArriving = Rtx::Weather::Snow, .mCrossed = 0.25f };
            table.add(FindingKind::Hitch, "led by walk on leg-1", placeAt("leg-1", 1), FindingMet{ .mMs = 30.0 });
            table.add(FindingKind::Hitch, "led by update on leg-2-land-0,1", hitch,
                FindingMet{ .mMs = 90.0, .mNote = "90.0 ms after 20.0: 3 meshes arrived, update 80.0" });
            table.add(FindingKind::LegFailed, "leg-3: stalled", placeAt("leg-3", 0));
            for (std::size_t at = 0; at < FindingTable::sMost; ++at)
                table.add(FindingKind::LogWarning, std::to_string(at), placeAt("leg-1", 0));
            table.add(FindingKind::LogWarning, "past", placeAt("leg-1", 0));

            const std::string text = describeFindings(table);
            const std::size_t fault = text.find("FAULT leg-failed: leg-3: stalled\n  first at leg-3 frame 0\n");
            const std::size_t worse = text.find("cost  hitch: led by update on leg-2-land-0,1, worst 90.0 ms");
            const std::size_t better = text.find("cost  hitch: led by walk on leg-1, worst 30.0 ms");
            ASSERT_NE(fault, std::string::npos) << text;
            ASSERT_NE(worse, std::string::npos) << text;
            ASSERT_NE(better, std::string::npos) << text;
            EXPECT_LT(fault, worse);
            EXPECT_LT(worse, better);
            EXPECT_NE(
                text.find("tour findings: 1 faults, 2048 costs and marks, past the table's bound: 3 log-warning\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("  worst at leg-2-land-0,1 frame 40, day 3 18:30 under Rain to Snow 25%\n"
                                "  90.0 ms after 20.0: 3 meshes arrived, update 80.0\n"),
                std::string::npos)
                << text;
            EXPECT_NE(text.find("cell = 0,1"), std::string::npos) << "the block to stand there again\n" << text;
        }

        /// **The closing pass is growth where a figure of use came back past a tenth above the first's**: 1100
        /// vertices against 1000 is a tenth and no growth, 1101 is; and the same for each figure. The room the
        /// renderer holds for the flight's peak is no figure of it, nor the textures that rest.
        TEST(RtxFindingTest, theClosingPassGrowsPastATenth)
        {
            constexpr std::uint64_t mebibyte = 1024 * 1024;
            const SoakFigures first{ .mVerticesUsed = 1000,
                .mIndicesUsed = 3000,
                .mInstances = 100,
                .mStructureLiveBytes = 200 * mebibyte,
                .mTextures = 400,
                .mMedianMs = 6.0 };
            EXPECT_EQ(describeGrowth(first, first), std::nullopt);
            EXPECT_EQ(describeGrowth(first, SoakFigures{ 1100, 3300, 110, 220 * mebibyte, 440, 6.6 }), std::nullopt)
                << "a tenth";

            // Each figure past a tenth alone is growth: 1101 of 1000, 3301 of 3000, 111 of 100, 221 of 200 MiB,
            // 441 of 400 textures, 6.7 of 6.0.
            const auto with = [&]<class Figure>(Figure SoakFigures::* figure, const Figure value) {
                SoakFigures again = first;
                again.*figure = value;
                return describeGrowth(first, again);
            };
            EXPECT_EQ(with(&SoakFigures::mVerticesUsed, 1101u),
                "the first leg again came to 1101 vertices in use against 1000, 3000 indices against 3000, 100 "
                "instances against 100, 200.0 MiB of live structures against 200.0, 400 textures named against 400, "
                "and a 6.00 ms median frame against 6.00");
            EXPECT_NE(with(&SoakFigures::mIndicesUsed, 3301u), std::nullopt);
            EXPECT_NE(with(&SoakFigures::mInstances, 111u), std::nullopt);
            EXPECT_NE(with(&SoakFigures::mStructureLiveBytes, 221 * mebibyte), std::nullopt);
            EXPECT_NE(with(&SoakFigures::mTextures, 441u), std::nullopt);
            EXPECT_NE(with(&SoakFigures::mMedianMs, 6.7), std::nullopt);
            EXPECT_EQ(describeGrowth(first, SoakFigures{ 500, 1500, 50, 100 * mebibyte, 200, 3.0 }), std::nullopt)
                << "a shrink";

            // What the place has in use, and none of the room held for the flight's peak.
            BenchPlace place;
            place.mMemory.mHeapCount = 1;
            place.mMemory.mHeaps[0] = Rtx::HeapUse{ .mLive = 7 * mebibyte, .mDeviceLocal = true };
            place.mContent.mVerticesUsed = 42;
            place.mContent.mVertexEnd = 4200;
            place.mContent.mIndicesUsed = 126;
            place.mScene.mInstances.mPlaced = 9;
            place.mScene.mInstanceSlots = 900;
            place.mScene.mStructureLiveBytes = 5;
            place.mScene.mStructureBytes = 50;
            place.mScene.mTextureCount = 11;
            place.mContent.mRestingTextures = 4;
            place.at(Rtx::Timing::Frame).mMedian = 5.5;
            const SoakFigures read = SoakFigures::of(place);
            EXPECT_EQ(read.mVerticesUsed, 42u);
            EXPECT_EQ(read.mIndicesUsed, 126u);
            EXPECT_EQ(read.mInstances, 9u);
            EXPECT_EQ(read.mStructureLiveBytes, 5u);
            EXPECT_EQ(read.mTextures, 7u) << "11 standing, 4 of them resting";
            EXPECT_EQ(read.mMedianMs, 5.5);
        }
    }
}
