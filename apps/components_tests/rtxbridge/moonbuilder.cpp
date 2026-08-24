#include <cmath>

#include <gtest/gtest.h>

#include <components/fallback/fallback.hpp>
#include <components/rtxbridge/moonbuilder.hpp>

namespace RtxBridge
{
    namespace
    {
        /// Morrowind's own `[Moons]`, as the ini ships it.
        ///
        /// **Vanilla's own numbers, and that is what makes the order not matter.**
        /// `Fallback::Map::init` keeps the first value it is given for a key, and a test elsewhere in
        /// this binary opens the real installation and plants Morrowind's — so whichever runs first
        /// wins. Every figure below is the one the shipped ini carries, which leaves the two sources
        /// agreeing and the expectations here true either way.
        void seed()
        {
            Fallback::Map::init({
                { "Moons_Masser_Size", "94" },
                { "Moons_Masser_Fade_In_Start", "14" },
                { "Moons_Masser_Fade_In_Finish", "15" },
                { "Moons_Masser_Fade_Out_Start", "7" },
                { "Moons_Masser_Fade_Out_Finish", "10" },
                { "Moons_Masser_Axis_Offset", "35" },
                { "Moons_Masser_Speed", "0.5" },
                { "Moons_Masser_Daily_Increment", "1" },
                { "Moons_Masser_Fade_Start_Angle", "50" },
                { "Moons_Masser_Fade_End_Angle", "40" },
                { "Moons_Masser_Moon_Shadow_Early_Fade_Angle", "0.5" },

                { "Moons_Secunda_Size", "40" },
                { "Moons_Secunda_Fade_In_Start", "14" },
                { "Moons_Secunda_Fade_In_Finish", "15" },
                { "Moons_Secunda_Fade_Out_Start", "7" },
                { "Moons_Secunda_Fade_Out_Finish", "10" },
                { "Moons_Secunda_Axis_Offset", "50" },
                { "Moons_Secunda_Speed", "0.6" },
                { "Moons_Secunda_Daily_Increment", "1.2" },
                { "Moons_Secunda_Fade_Start_Angle", "50" },
                { "Moons_Secunda_Fade_End_Angle", "40" },
                { "Moons_Secunda_Moon_Shadow_Early_Fade_Angle", "0.5" },
            });
        }

        /// Degrees along its arc that Masser covers in an hour.
        ///
        /// **Not the 0.5 the ini asks for.** Fifteen degrees an hour is one rotation a day and the
        /// speed counts rotations, so 0.5 would leave the moon short of its own horizon in
        /// twenty-four hours; the engine floors every moon at `180 / 23 / 15` and Masser is the one
        /// that hits the floor. 15 * 0.5217391 is 7.826087 degrees an hour.
        constexpr float sMasserPerHour = 15.0f * (180.0f / 23.0f / 15.0f);

        /// A disc nineteen degrees across, which is thirty-five times the real moon.
        ///
        /// `Moons_Masser_Size` of 94 is scaled by 450/125 onto a quad of half-extent 0.5 a thousand
        /// units off, so the radius is `atan(0.5 * 450 * 94 / 125 / 1000)` — `atan(0.1692)`.
        TEST(RtxMoonBuilderTest, aMoonIsAsWideAsTheGameDrawsIt)
        {
            seed();

            EXPECT_NEAR(moonAngularRadius(Moon::Masser), std::atan(0.1692f), 1e-6f);
            EXPECT_NEAR(moonAngularRadius(Moon::Secunda), std::atan(0.072f), 1e-6f);

            // 9.6 degrees against 4.12, which is the pair the two portraits were painted for — 512
            // pixels against 256.
            EXPECT_NEAR(osg::RadiansToDegrees(moonAngularRadius(Moon::Masser)), 9.6035f, 1e-3f);
            EXPECT_NEAR(osg::RadiansToDegrees(moonAngularRadius(Moon::Secunda)), 4.118f, 1e-3f);

            // The sizes are 2.35 apart and the angles less, because the arctangent is already
            // bending: a moon that wide is past where the angle and the size stay proportional.
            EXPECT_LT(moonAngularRadius(Moon::Masser) / moonAngularRadius(Moon::Secunda), 94.0f / 40.0f);
        }

        /// Masser rises at four in the afternoon on the day the game begins, and climbs from there.
        ///
        /// The rise hour is `increment + (day - 1 + 16) * increment mod 24`, and Masser's increment
        /// is one, so day zero gives `1 + 15`. At the horizon the moon's height is nothing; six
        /// hours later it has travelled `6 * 7.826087` degrees and stands at the sine of that.
        ///
        /// **The height is the sine of the arc and the axis offset does not enter it**, which is the
        /// whole reason the offset swings the arc about the zenith rather than tipping it: both
        /// moons climb as high as the sun does and only their rising points differ.
        TEST(RtxMoonBuilderTest, masserRisesAtSixteenHundredOnTheDayTheGameBegins)
        {
            seed();

            const MoonPlacement rising = makeMoon(Moon::Masser, 0, 16.0f);
            EXPECT_NEAR(rising.mDirection.z(), 0.0f, 1e-6f) << "on the horizon at the moment it rises";

            const MoonPlacement up = makeMoon(Moon::Masser, 0, 22.0f);
            const float travelled = osg::DegreesToRadians(6.0f * sMasserPerHour);
            EXPECT_NEAR(up.mDirection.z(), std::sin(travelled), 1e-4f);

            // Due north swung 35 degrees, at the cosine of the arc: `(-cos a sin 35, cos a cos 35)`.
            EXPECT_NEAR(up.mDirection.x(), -std::cos(travelled) * std::sin(osg::DegreesToRadians(35.0f)), 1e-4f);
            EXPECT_NEAR(up.mDirection.y(), std::cos(travelled) * std::cos(osg::DegreesToRadians(35.0f)), 1e-4f);

            // Secunda's arc is swung further and runs faster, so the two rise apart and cross.
            const MoonPlacement other = makeMoon(Moon::Secunda, 0, 22.0f);
            EXPECT_GT(std::abs(other.mDirection.x() - up.mDirection.x()), 0.1f);
        }

        /// The face is a frame, not a billboard: three unit vectors at right angles to each other.
        TEST(RtxMoonBuilderTest, theFaceStandsSquareToWhereTheMoonIs)
        {
            seed();

            for (const float hour : { 17.0f, 20.0f, 23.0f })
            {
                const MoonPlacement at = makeMoon(Moon::Masser, 0, hour);
                EXPECT_NEAR(at.mDirection.length(), 1.0f, 1e-5f) << "at hour " << hour;
                EXPECT_NEAR(at.mRight.length(), 1.0f, 1e-5f) << "at hour " << hour;
                EXPECT_NEAR(at.mUp.length(), 1.0f, 1e-5f) << "at hour " << hour;

                EXPECT_NEAR(at.mRight * at.mUp, 0.0f, 1e-5f) << "at hour " << hour;
                EXPECT_NEAR(at.mRight * at.mDirection, 0.0f, 1e-5f) << "at hour " << hour;
                EXPECT_NEAR(at.mUp * at.mDirection, 0.0f, 1e-5f) << "at hour " << hour;
            }

            // **And it turns against the horizon as the moon crosses**, which is what a locked moon
            // does and what a billboard does not: the face's up is not the world's.
            const osg::Vec3f early = makeMoon(Moon::Masser, 0, 17.0f).mUp;
            const osg::Vec3f late = makeMoon(Moon::Masser, 0, 23.0f).mUp;
            EXPECT_LT(early * late, 0.99f) << "the portrait would be pinned to the horizon";
        }

        /// A moon arrives and leaves twice over: once by the hour, once by where it is on its arc.
        ///
        /// Masser passes the early-fade angle of 39.5 degrees at `16 + 39.5 / 7.826087` — five hours
        /// and three minutes after it rises — so at eight in the evening it is above the horizon and
        /// still not drawn.
        TEST(RtxMoonBuilderTest, aMoonIsUpBeforeItIsVisible)
        {
            seed();

            EXPECT_EQ(makeMoon(Moon::Masser, 0, 20.0f).mAlpha, 0.0f) << "risen, inside the early shadow";
            EXPECT_GT(makeMoon(Moon::Masser, 0, 20.0f).mDirection.z(), 0.0f) << "and above the horizon";

            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 0, 22.0f).mAlpha, 1.0f) << "clear of it";

            // **And the hour fades it independently of the arc.** Day nine is where Masser rises at
            // one in the morning — `1 + (9 - 1 + 16) mod 24` — so half past two in the afternoon
            // finds it a hundred and six degrees along, clear of both shadow angles, and inside the
            // hour-long fade in that runs from fourteen to fifteen. Half an hour of one hour is
            // half the moon.
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 9, 14.5f).mAlpha, 0.5f);

            // And between the fade out finishing and the fade in starting there is no moon at all,
            // whatever its arc says.
            EXPECT_EQ(makeMoon(Moon::Masser, 9, 12.0f).mAlpha, 0.0f);
        }

        /// The game begins under a full moon, and it wanes from there on a three-day cycle.
        ///
        /// `(day + 1) / 3 mod 8` counts the eight painted phases from full once the moon has risen,
        /// so days zero through one are full, two through four the first step off it, and the eighth
        /// step comes back round. Zero radians is full and pi is new, which puts new — the fifth of
        /// the eight — at days twelve to fourteen.
        TEST(RtxMoonBuilderTest, theGameBeginsFullAndWanesOnAThreeDayCycle)
        {
            seed();

            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 0, 22.0f).mPhaseAngle, 0.0f) << "16 Last Seed";
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 1, 22.0f).mPhaseAngle, 0.0f);

            // A quarter turn of the cycle is one of the eight steps, and the steps go by threes.
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 2, 22.0f).mPhaseAngle, 0.25f * osg::PIf);
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 5, 22.0f).mPhaseAngle, 0.5f * osg::PIf);

            // Halfway round is new, twelve days in — a moon that is up and unlit.
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 11, 22.0f).mPhaseAngle, osg::PIf);

            // And a full cycle is twenty-four days, which is the loop the rise hour runs on too. The
            // count is of tomorrow rather than today, so the last of the eight steps is days twenty
            // to twenty-two and the twenty-third is already back at full.
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 21, 22.0f).mPhaseAngle, 1.75f * osg::PIf);
            EXPECT_FLOAT_EQ(makeMoon(Moon::Masser, 23, 22.0f).mPhaseAngle, 0.0f) << "back to full";

            // **The lit share is the cosine, and it is what the shader carves the terminator with.**
            // Full is all of it, the two quarters are half, and new is none.
            const auto lit = [](float phaseAngle) { return 0.5f * (1.0f + std::cos(phaseAngle)); };
            EXPECT_FLOAT_EQ(lit(makeMoon(Moon::Masser, 0, 22.0f).mPhaseAngle), 1.0f);
            EXPECT_NEAR(lit(makeMoon(Moon::Masser, 5, 22.0f).mPhaseAngle), 0.5f, 1e-6f);
            EXPECT_NEAR(lit(makeMoon(Moon::Masser, 11, 22.0f).mPhaseAngle), 0.0f, 1e-6f);
        }
    }
}
