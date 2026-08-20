#include <cmath>

#include <gtest/gtest.h>

#include <components/esm3/loadligh.hpp>
#include <components/rtxbridge/lightbuilder.hpp>

namespace RtxBridge
{
    namespace
    {
        ESM::Light makeRecord(std::int32_t radius, std::uint32_t colour, std::int32_t flags)
        {
            ESM::Light record;
            record.mData.mRadius = radius;
            record.mData.mColor = colour;
            record.mData.mFlags = flags;
            return record;
        }

        /// The packing is `0xAABBGGRR`: red in the low byte.
        ///
        /// Reading it the other way round turns every candle in the game blue, which is the kind of
        /// wrong that looks deliberate.
        TEST(RtxLightBuilderTest, aColourIsRedFirstAndDecodedOutOfDisplaySpace)
        {
            EXPECT_EQ(decodeColour(0x00FFFFFF), osg::Vec3f(1.0f, 1.0f, 1.0f));
            EXPECT_EQ(decodeColour(0), osg::Vec3f(0.0f, 0.0f, 0.0f));

            const osg::Vec3f candle = decodeColour(0x000080FF);
            EXPECT_FLOAT_EQ(candle.x(), 1.0f) << "red is the low byte";
            EXPECT_EQ(candle.z(), 0.0f) << "and blue the third";

            // Mid grey is where the two spaces diverge most, so it is where skipping the decode is
            // most visible: 128 of 255 is 0.50196 encoded and
            // ((0.50196 + 0.055) / 1.055)^2.4 = 0.21586 linear.
            EXPECT_NEAR(candle.y(), 0.21586f, 1e-5f);
            EXPECT_NEAR(decodeColour(0x00808080).x(), 0.21586f, 1e-5f);
        }

        /// Brightness and reach both come off the one number the record carries, and part company.
        ///
        /// Intensity stays on the recorded radius, because that is what the lamp *is*. Only the
        /// falloff's run is stretched, because Morrowind's radii were tuned for a renderer where an
        /// ambient term lit the room and a lamp only had to light its own post.
        TEST(RtxLightBuilderTest, intensityScalesWithTheRecordedRadiusAndReachIsStretchedPastIt)
        {
            const std::optional<Rtx::Light> light = makeLight(makeRecord(100, 0x00FFFFFF, 0), osg::Vec3f(1, 2, 3));

            ASSERT_TRUE(light.has_value());
            EXPECT_EQ(light->mPosition, osg::Vec3f(1, 2, 3));

            // 100 * 100 * 0.25 * pi = 7853.98, and white decodes to one.
            EXPECT_NEAR(light->mIntensity.x(), 7853.98f, 0.01f);
            EXPECT_NEAR(light->mIntensity.y(), 7853.98f, 0.01f);

            // 100 * 2 + 128.
            EXPECT_FLOAT_EQ(light->mReach, 328.0f);

            // Doubling the radius quadruples the brightness and rather less than doubles the reach:
            // 200 * 200 * 0.25 * pi = 31415.9, and 200 * 2 + 128 = 528.
            const std::optional<Rtx::Light> larger = makeLight(makeRecord(200, 0x00FFFFFF, 0), osg::Vec3f());
            ASSERT_TRUE(larger.has_value());
            EXPECT_NEAR(larger->mIntensity.x(), 31415.9f, 0.1f);
            EXPECT_FLOAT_EQ(larger->mReach, 528.0f);
        }

        /// The sun's arc, which is the engine's own and not an approximation of it.
        ///
        /// `(-400 * orbit, 75, -100)` with `orbit` running from one at sunrise to minus one at
        /// nightfall — so the vector is where the light *goes*, west at dawn and east at dusk, and
        /// the two are mirror images. Its length is 125 at the midpoint, which makes that one exact.
        TEST(RtxLightBuilderTest, theSunCrossesTheSkyTheWayTheEngineSaysItDoes)
        {
            constexpr float sunrise = 6.0f;
            constexpr float nightStart = 20.0f;

            // Halfway through a fourteen-hour day is hour 13, where the orbit is zero and the vector
            // is (0, 75, -100) — length exactly 125, so the direction is exactly (0, 0.6, -0.8).
            const osg::Vec3f noon = sunDirection(13.0f, sunrise, nightStart);
            EXPECT_NEAR(noon.x(), 0.0f, 1e-6f);
            EXPECT_NEAR(noon.y(), 0.6f, 1e-6f);
            EXPECT_NEAR(noon.z(), -0.8f, 1e-6f);

            // At either end the swing is full: (-+400, 75, -100), whose length is 419.077.
            const osg::Vec3f dawn = sunDirection(sunrise, sunrise, nightStart);
            const osg::Vec3f dusk = sunDirection(nightStart, sunrise, nightStart);

            EXPECT_NEAR(dawn.x(), -400.0f / 419.077f, 1e-4f) << "light travels west at dawn";
            EXPECT_NEAR(dusk.x(), 400.0f / 419.077f, 1e-4f) << "and east at dusk";
            EXPECT_NEAR(dawn.x(), -dusk.x(), 1e-6f) << "the two ends mirror";

            // Only the swing changes: the northing and the climb are fixed, so their ratio is the
            // same at every hour of the day and of the night alike. The engine's is too — a night is
            // dark because the sun stops shining, not because it drops below the world.
            for (const float hour : { 0.0f, 6.0f, 13.0f, 20.0f, 23.0f })
            {
                const osg::Vec3f at = sunDirection(hour, sunrise, nightStart);
                EXPECT_NEAR(at.z() / at.y(), -100.0f / 75.0f, 1e-5f) << "at hour " << hour;
                EXPECT_LT(at.z(), 0.0f) << "the light always travels downward, at hour " << hour;
            }
        }

        /// The four phases, and where their boundaries fall.
        TEST(RtxLightBuilderTest, anHourReadsThePhaseItFallsIn)
        {
            constexpr float sunrise = 6.0f;
            constexpr float nightStart = 20.0f;

            EXPECT_EQ(phaseAt(13.0f, sunrise, nightStart), SkyPhase::Day);
            EXPECT_EQ(phaseAt(6.0f, sunrise, nightStart), SkyPhase::Sunrise);
            EXPECT_EQ(phaseAt(20.0f, sunrise, nightStart), SkyPhase::Sunset);
            EXPECT_EQ(phaseAt(2.0f, sunrise, nightStart), SkyPhase::Night);
            EXPECT_EQ(phaseAt(23.0f, sunrise, nightStart), SkyPhase::Night);

            // An hour either side of each boundary, which is the window the game ramps across and
            // this one steps in the middle of.
            EXPECT_EQ(phaseAt(4.9f, sunrise, nightStart), SkyPhase::Night);
            EXPECT_EQ(phaseAt(5.1f, sunrise, nightStart), SkyPhase::Sunrise);
            EXPECT_EQ(phaseAt(7.1f, sunrise, nightStart), SkyPhase::Day);
            EXPECT_EQ(phaseAt(18.9f, sunrise, nightStart), SkyPhase::Day);
            EXPECT_EQ(phaseAt(21.1f, sunrise, nightStart), SkyPhase::Night);
        }

        /// Three kinds of record place a mesh and no light, and one kind is nonsense.
        TEST(RtxLightBuilderTest, carriedNegativeAndUnlitRecordsCastNothing)
        {
            for (const std::int32_t flag : { ESM::Light::Carry, ESM::Light::Negative, ESM::Light::OffDefault })
                EXPECT_FALSE(makeLight(makeRecord(100, 0x00FFFFFF, flag), osg::Vec3f()).has_value()) << "flag " << flag;

            // The flags that only say how a light animates leave it burning.
            for (const std::int32_t flag : { ESM::Light::Flicker, ESM::Light::Fire, ESM::Light::Pulse })
                EXPECT_TRUE(makeLight(makeRecord(100, 0x00FFFFFF, flag), osg::Vec3f()).has_value()) << "flag " << flag;

            // A file on disk that something else wrote, so a radius of nothing is data and not a
            // broken contract.
            EXPECT_FALSE(makeLight(makeRecord(0, 0x00FFFFFF, 0), osg::Vec3f()).has_value());
            EXPECT_FALSE(makeLight(makeRecord(-50, 0x00FFFFFF, 0), osg::Vec3f()).has_value());
        }
    }
}
