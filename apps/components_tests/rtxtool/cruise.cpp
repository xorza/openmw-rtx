#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/cruise.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr CruiseLeg sOpen{ .mLength = 100.0 };
        constexpr CruiseLeg sFromRest{ .mLength = 100.0, .mFromRest = true };
        constexpr CruiseLeg sBetweenRests{ .mLength = 100.0, .mFromRest = true, .mToRest = true };
        constexpr CruiseLeg sShort{ .mLength = 10.0, .mFromRest = true, .mToRest = true };

        /// At ten a frame with an ease of four: a hundred units take ten frames with no rest, twelve
        /// from one, since an ease covers half what cruising would and loses `4 / 2`, and fourteen
        /// between two. Ten units take a frame at ten, too short for two eases of four, which then
        /// meet in its middle: `2 · 10 / 10 = 2`, and from one rest as well. Each speed comes back
        /// out of its time, on either side of where the eases stop fitting.
        TEST(RtxCruiseTest, aLegTakesItsLengthAtTheSpeedAndHalfOfEachEase)
        {
            const Cruise cruise{ .mEase = 4.0 };

            EXPECT_DOUBLE_EQ(cruise.timeFor(sOpen, 10.0), 10.0);
            EXPECT_DOUBLE_EQ(cruise.timeFor(sFromRest, 10.0), 12.0);
            EXPECT_DOUBLE_EQ(cruise.timeFor(sBetweenRests, 10.0), 14.0);
            EXPECT_DOUBLE_EQ(cruise.timeFor(sShort, 10.0), 2.0);
            EXPECT_DOUBLE_EQ(cruise.timeFor(CruiseLeg{ .mLength = 10.0, .mFromRest = true }, 10.0), 2.0);

            for (const CruiseLeg& leg : { sOpen, sFromRest, sBetweenRests, sShort })
                for (const double speed : { 0.5, 10.0, 80.0 })
                    EXPECT_NEAR(cruise.speedFor(leg, cruise.timeFor(leg, speed)), speed, 1e-12 * speed)
                        << leg.mLength << " units from " << leg.getRests() << " rests at " << speed;

            EXPECT_DOUBLE_EQ(Cruise{}.timeFor(sBetweenRests, 10.0), 10.0) << "no ease, no rest to ease from";
        }

        /// **One speed for a flight of three legs, from its total.** At ten a frame the legs take
        /// 12, 10 and 2 frames, 24 between them; at two, `50 + 2`, 50, and `5 + 2`: 109. Each
        /// total gives its speed back, the first with the short leg's eases meeting and the second
        /// with them fitting. With no ease the same 24 frames are `210 / 24 = 8.75` a frame.
        TEST(RtxCruiseTest, aFlightsLegsShareOneSpeedThatFillsTheirTime)
        {
            const Cruise cruise{ .mEase = 4.0 };
            const std::vector<CruiseLeg> legs{ sFromRest, sOpen, CruiseLeg{ .mLength = 10.0, .mToRest = true } };

            EXPECT_NEAR(cruise.speedFor(legs, 24.0), 10.0, 1e-12);
            EXPECT_NEAR(cruise.speedFor(legs, 109.0), 2.0, 1e-12);
            EXPECT_NEAR(Cruise{}.speedFor(legs, 24.0), 8.75, 1e-12);
            EXPECT_NE(cruise.speedFor(legs, 24.0), Cruise{}.speedFor(legs, 24.0)) << "the ease is in the answer";
        }

        /// A hundred units between two rests in fourteen frames: ten a frame, eased over four at
        /// each end. The ease has covered `10 · 4 · (u³ − u⁴/2)`: `40 · 0.09375 = 3.75` at its
        /// middle and 20 at its end, where the cruise takes over, `10 · (7 − 2) = 50` at the
        /// middle of the leg; the end mirrors the start. The speed through the join is the
        /// cruise's. Ten units in two frames ease up and down with no cruise between, the middle at
        /// half the length.
        TEST(RtxCruiseTest, theEyeEasesFromRestCruisesAndEasesToRest)
        {
            const Cruise cruise{ .mEase = 4.0 };

            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 0.0), 0.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 2.0), 3.75);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 4.0), 20.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 7.0), 50.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 10.0), 80.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 12.0), 96.25);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sBetweenRests, 14.0, 14.0), 100.0);

            constexpr double step = 1e-6;
            const double joining = (cruise.coveredAt(sBetweenRests, 14.0, 4.0 + step)
                                       - cruise.coveredAt(sBetweenRests, 14.0, 4.0 - step))
                / (2.0 * step);
            EXPECT_NEAR(joining, 10.0, 1e-6) << "the speed where the ease hands over to the cruise";

            EXPECT_DOUBLE_EQ(cruise.coveredAt(sShort, 2.0, 1.0), 5.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 12.0), 100.0) << "an open end arrives at speed";
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 11.0), 90.0);
        }
    }
}
