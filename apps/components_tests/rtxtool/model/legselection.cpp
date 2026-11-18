#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2i>

#include <apps/rtxtool/model/legselection.hpp>
#include <apps/rtxtool/model/tour.hpp>

namespace RtxTool
{
    namespace
    {
        /// A plan of three land legs, two rooms and the closing leg: legs 1 to 6 as `--plan` numbers them. The first
        /// two rows of land run on into each other and the third lies a sea away (`RtxTourTest`).
        TourPlan sixLegs()
        {
            WorldMap map;
            for (const osg::Vec2i cell :
                { osg::Vec2i(0, 0), osg::Vec2i(2, 0), osg::Vec2i(0, 3), osg::Vec2i(2, 3), osg::Vec2i(30, 6) })
                map.mLand.push_back(LandCell{ .mCell = cell });
            map.mFiles = { "Morrowind.esm" };
            map.mRooms = { Room{ .mName = "Arrille's Tradehouse", .mFile = 0, .mReferences = 50, .mEye = {} },
                Room{ .mName = "Balmora, Guild of Mages", .mFile = 0, .mReferences = 40, .mEye = {} } };
            TourPlan plan = planTour(map, TourPacing{});
            EXPECT_EQ(plan.mLegs.size(), 6u);
            return plan;
        }

        std::vector<std::size_t> picked(const std::string_view legs, const std::uint32_t every = 1)
        {
            const Result<LegSelection, std::string> selection = LegSelection::parse(legs, every);
            EXPECT_TRUE(selection.isOk()) << legs;
            const Result<std::vector<std::size_t>, std::string> taken = selection.value().pick(sixLegs());
            EXPECT_TRUE(taken.isOk()) << legs;
            return taken.value();
        }

        std::string refusalOf(const std::string_view legs, const std::uint32_t every = 1)
        {
            const Result<LegSelection, std::string> selection = LegSelection::parse(legs, every);
            if (!selection.isOk())
                return selection.error();
            const Result<std::vector<std::size_t>, std::string> taken = selection.value().pick(sixLegs());
            return taken.isOk() ? std::string() : taken.error();
        }

        /// **Kinds, legs and runs of them are taken together, in the plan's order**, each leg once however many items
        /// name it; nothing named is every leg, and a run open at its end goes to the last.
        TEST(RtxLegSelectionTest, kindsLegsAndRunsAreTakenTogetherInThePlansOrder)
        {
            EXPECT_EQ(picked(""), (std::vector<std::size_t>{ 0, 1, 2, 3, 4, 5 }));
            EXPECT_EQ(picked("rooms"), (std::vector<std::size_t>{ 3, 4 }));
            EXPECT_EQ(picked("land"), (std::vector<std::size_t>{ 0, 1, 2 }));
            EXPECT_EQ(picked("closing"), (std::vector<std::size_t>{ 5 }));
            EXPECT_EQ(picked("2"), (std::vector<std::size_t>{ 1 }));
            EXPECT_EQ(picked("2-3"), (std::vector<std::size_t>{ 1, 2 }));
            EXPECT_EQ(picked("5-"), (std::vector<std::size_t>{ 4, 5 })) << "the rest after a crash";
            EXPECT_EQ(picked("closing,rooms,1"), (std::vector<std::size_t>{ 0, 3, 4, 5 })) << "in the plan's order";
            EXPECT_EQ(picked("rooms,4-5"), (std::vector<std::size_t>{ 3, 4 })) << "a leg named twice flies once";
        }

        /// **Every Nth of what is named flies, the first among them**: every second of six is the first, third and
        /// fifth; every second room the first room; an `every` past the count the first alone.
        TEST(RtxLegSelectionTest, everyNthOfWhatIsNamedFliesTheFirstAmongThem)
        {
            EXPECT_EQ(picked("", 2), (std::vector<std::size_t>{ 0, 2, 4 }));
            EXPECT_EQ(picked("", 4), (std::vector<std::size_t>{ 0, 4 }));
            EXPECT_EQ(picked("rooms", 2), (std::vector<std::size_t>{ 3 }));
            EXPECT_EQ(picked("2-", 2), (std::vector<std::size_t>{ 1, 3, 5 }));
            EXPECT_EQ(picked("", 9), (std::vector<std::size_t>{ 0 }));
        }

        /// **What names no leg is refused, saying why**: a word that is no kind, a leg nought, a run backwards, a
        /// leg past the plan's last, an empty item, and every nought.
        TEST(RtxLegSelectionTest, whatNamesNoLegIsRefusedSayingWhy)
        {
            EXPECT_EQ(refusalOf("room"),
                "--legs names \"room\", which is no land, rooms or closing, no leg's number and no run of them");
            EXPECT_EQ(refusalOf("land,,rooms"),
                "--legs names \"\", which is no land, rooms or closing, no leg's number and no run of them");
            EXPECT_EQ(refusalOf("-3"),
                "--legs names \"-3\", which is no land, rooms or closing, no leg's number and no run of them");
            EXPECT_EQ(refusalOf("2-x"),
                "--legs names \"2-x\", which is no land, rooms or closing, no leg's number and no run of them");
            EXPECT_EQ(refusalOf("0"), "--legs names leg 0 in \"0\": a tour's legs are counted from one");
            EXPECT_EQ(refusalOf("4-2"), "--legs names \"4-2\", a run that ends before it begins");
            EXPECT_EQ(refusalOf("7"), "--legs names leg 7, past the tour's last, 6");
            EXPECT_EQ(refusalOf("3-7"), "--legs names leg 7, past the tour's last, 6");
            EXPECT_EQ(refusalOf("7-"), "--legs names leg 7, past the tour's last, 6");
            EXPECT_EQ(refusalOf("", 0), "--every=0 flies no leg: every leg is --every=1");
            EXPECT_EQ(refusalOf("6"), "") << "the last leg is a leg";
        }
    }
}
