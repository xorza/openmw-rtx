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
            EXPECT_FALSE(table.holds(FindingKind::NotFinite, "bounce"));
            table.add(FindingKind::NotFinite, "bounce", placeAt("leg-1", 10));
            EXPECT_TRUE(table.holds(FindingKind::NotFinite, "bounce"));
            EXPECT_FALSE(table.holds(FindingKind::NotFinite, "visibility"));
            EXPECT_FALSE(table.holds(FindingKind::Refused, "bounce")) << "a kind apart";
            table.add(FindingKind::NotFinite, "bounce", placeAt("leg-3", 20), 4.0);
            table.again(FindingKind::NotFinite, "bounce", 2.0);
            table.add(FindingKind::NotFinite, "visibility", placeAt("leg-2", 5));

            ASSERT_EQ(table.getFindings().size(), 2u);
            const Finding& bounce = table.getFindings()[0];
            EXPECT_EQ(bounce.mCount, 3u);
            EXPECT_EQ(bounce.mFirst.mLeg, "leg-1");
            EXPECT_EQ(bounce.mFirst.mFrame, 10u);
            EXPECT_EQ(bounce.mWorstMs, 4.0);
            EXPECT_EQ(table.getDropped(), 0u);
        }

        /// **Past its bound a finding is counted as dropped**, so the run's memory holds whatever the content does.
        TEST(RtxFindingTest, pastItsBoundAFindingIsCountedAsDropped)
        {
            FindingTable table;
            for (std::size_t at = 0; at < FindingTable::sMost + 3; ++at)
                table.add(FindingKind::Hitch, std::to_string(at), placeAt("leg-1", 0));
            EXPECT_EQ(table.getFindings().size(), FindingTable::sMost);
            EXPECT_EQ(table.getDropped(), 3u);
            table.add(FindingKind::Hitch, "0", placeAt("leg-2", 0));
            EXPECT_EQ(table.getFindings()[0].mCount, 2u) << "one held is counted however full the table";
        }

        /// **A fault fails the run, a cost only where the line names it.**
        TEST(RtxFindingTest, aFaultFailsTheRunAndACostWhereNamed)
        {
            FindingTable costs;
            costs.add(FindingKind::Hitch, "120 ms", placeAt("leg-1", 0), 120.0);
            costs.add(FindingKind::Mark, "here", placeAt("leg-1", 0));
            EXPECT_EQ(costs.statusUnder({}), ExitStatus::Passed);
            const std::vector<FindingKind> hitches{ FindingKind::Hitch };
            EXPECT_EQ(costs.statusUnder(hitches), ExitStatus::Failed);

            FindingTable fault;
            fault.add(FindingKind::Outrun, "cell 3,1", placeAt("leg-1", 0));
            EXPECT_EQ(fault.statusUnder({}), ExitStatus::Failed);
            EXPECT_EQ(FindingTable{}.statusUnder({}), ExitStatus::Passed);
        }

        /// **The report puts the faults first and the costs worst first**, each once with its count and where it
        /// was first met, under its hour and the sky as it crossed, and the block to stand there again.
        TEST(RtxFindingTest, theReportPutsTheFaultsFirst)
        {
            FindingTable table;
            FindingPlace hitch = placeAt("leg-2-land-0,1", 40);
            hitch.mStood.mStand.mCell = CellAddress::exterior(0, 1);
            hitch.mStood.mStand.mEye = osg::Vec3f(1, 2, 3);
            hitch.mStood.mStand.mLook = osg::Vec3f(1, 3, 3);
            hitch.mStood.mSky.mHour = 18.5f;
            hitch.mStood.mSky.mDay = 2;
            hitch.mLeft = Rtx::Weather::Rain;
            hitch.mArriving = Rtx::Weather::Snow;
            hitch.mCrossed = 0.25f;
            table.add(FindingKind::Hitch, "30 ms", placeAt("leg-1", 1), 30.0);
            table.add(FindingKind::Hitch, "90 ms", hitch, 90.0);
            table.add(FindingKind::LegFailed, "leg-3: stalled", placeAt("leg-3", 0));

            const std::string text = describeFindings(table);
            const std::size_t fault = text.find("FAULT leg-failed: leg-3: stalled");
            const std::size_t worse = text.find("cost  hitch: 90 ms, worst 90.0 ms");
            const std::size_t better = text.find("cost  hitch: 30 ms, worst 30.0 ms");
            ASSERT_NE(fault, std::string::npos) << text;
            ASSERT_NE(worse, std::string::npos) << text;
            ASSERT_NE(better, std::string::npos) << text;
            EXPECT_LT(fault, worse);
            EXPECT_LT(worse, better);
            EXPECT_NE(text.find("tour findings: 1 faults, 2 costs and marks"), std::string::npos) << text;
            EXPECT_NE(
                text.find("first at leg-2-land-0,1 frame 40, day 3 18:30 under Rain to Snow 25%"), std::string::npos)
                << text;
            EXPECT_NE(text.find("cell = 0,1"), std::string::npos) << "the block to stand there again\n" << text;
        }

        /// **The closing pass is growth where a figure came back past a tenth above the first's**: 1100 MiB against
        /// 1000 is a tenth and no growth, 1101 is; and the same for the vertices and the median frame.
        TEST(RtxFindingTest, theClosingPassGrowsPastATenth)
        {
            constexpr std::uint64_t mebibyte = 1024 * 1024;
            const SoakFigures first{ .mDeviceLive = 1000 * mebibyte, .mVerticesUsed = 1000, .mMedianMs = 6.0 };
            EXPECT_EQ(describeGrowth(first, first), std::nullopt);
            EXPECT_EQ(describeGrowth(first, SoakFigures{ 1100 * mebibyte, 1100, 6.6 }), std::nullopt) << "a tenth";
            EXPECT_EQ(describeGrowth(first, SoakFigures{ 1101 * mebibyte, 1000, 6.0 }),
                "the first leg again came to 1101.0 MiB of live device memory against 1000.0, 1000 vertices in use "
                "against 1000, and a 6.00 ms median frame against 6.00");
            EXPECT_NE(describeGrowth(first, SoakFigures{ 1000 * mebibyte, 1101, 6.0 }), std::nullopt);
            EXPECT_NE(describeGrowth(first, SoakFigures{ 1000 * mebibyte, 1000, 6.7 }), std::nullopt);
            EXPECT_EQ(describeGrowth(first, SoakFigures{ 500 * mebibyte, 500, 3.0 }), std::nullopt) << "a shrink";

            BenchPlace place;
            place.mMemory.mHeapCount = 2;
            place.mMemory.mHeaps[0] = Rtx::HeapUse{ .mLive = 7, .mDeviceLocal = true };
            place.mMemory.mHeaps[1] = Rtx::HeapUse{ .mLive = 100, .mDeviceLocal = false };
            place.mContent.mVerticesUsed = 42;
            place.at(Rtx::Timing::Frame).mMedian = 5.5;
            const SoakFigures read = SoakFigures::of(place);
            EXPECT_EQ(read.mDeviceLive, 7u) << "video memory alone";
            EXPECT_EQ(read.mVerticesUsed, 42u);
            EXPECT_EQ(read.mMedianMs, 5.5);
        }
    }
}
