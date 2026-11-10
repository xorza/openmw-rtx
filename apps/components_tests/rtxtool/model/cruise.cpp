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

        /// **A key passed at another speed is reached within an ease of it.** A hundred units from
        /// a rest in twelve frames with an ease of four, the end joined at five a frame: both ends
        /// ease, and the cruise is `(100 − 5 · 4 / 2) / (12 − 2 − 2) = 11.25`. The first ease has
        /// covered `11.25 · 4 · 0.09375 = 4.21875` at its middle and 22.5 at its end, the cruise
        /// `22.5 + 11.25 · 4 = 67.5` where the last ease begins, and two frames out of the key the
        /// eye is short of it by five a frame and `6.25 · 4 · 0.09375`: `100 − 10 − 2.34375`. The
        /// speed at the key is the join's. Joined at its own ten a frame, the leg is the one-speed
        /// leg at every frame. Four frames are too short for two eases of four, which take two
        /// each: `(20 − 2 · 2 / 2) / (4 − 1 − 1) = 9`, nine at the middle.
        TEST(RtxCruiseTest, aKeyPassedAtAnotherSpeedIsReachedWithinAnEaseOfIt)
        {
            const Cruise cruise{ .mEase = 4.0 };
            const CruiseJoins slower{ .mTo = 5.0 };

            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 0.0, slower), 0.0);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 2.0, slower), 4.21875);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 4.0, slower), 22.5);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 8.0, slower), 67.5);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 10.0, slower), 87.65625);
            EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, 12.0, slower), 100.0);

            // The ease's speed is flat at both its ends, so a one-sided difference errs by the
            // square of its step.
            constexpr double step = 1e-6;
            const double arriving = (100.0 - cruise.coveredAt(sFromRest, 12.0, 12.0 - step, slower)) / step;
            EXPECT_NEAR(arriving, 5.0, 1e-6) << "the speed the key is passed at";

            const CruiseJoins own{ .mTo = 10.0 };
            for (double at = 0.0; at <= 12.0; at += 0.5)
                EXPECT_DOUBLE_EQ(cruise.coveredAt(sFromRest, 12.0, at, own), cruise.coveredAt(sFromRest, 12.0, at))
                    << "frame " << at;
            EXPECT_NE(cruise.coveredAt(sFromRest, 12.0, 10.0, slower), cruise.coveredAt(sFromRest, 12.0, 10.0, own));

            const CruiseLeg brief{ .mLength = 20.0, .mFromRest = true };
            EXPECT_DOUBLE_EQ(cruise.coveredAt(brief, 4.0, 2.0, CruiseJoins{ .mTo = 2.0 }), 9.0);
        }
    }
}
