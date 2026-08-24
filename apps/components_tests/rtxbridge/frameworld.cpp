#include <gtest/gtest.h>

#include <components/rtx/shaders/visibility.h>
#include <components/rtxbridge/frameworld.hpp>

namespace RtxBridge
{
    namespace
    {
        /// A world where no two numbers are the same, so a field written from the wrong one shows.
        FrameWorld distinct()
        {
            FrameWorld world{
                .mSunDirection = osg::Vec3f(0.0f, 0.6f, -0.8f),
                .mSunIrradiance = osg::Vec3f(1.5f, 1.25f, 1.0f),
                .mSunVisible = true,
                .mAmbient = osg::Vec3f(0.11f, 0.12f, 0.13f),
                .mSkyHorizon = osg::Vec3f(0.21f, 0.22f, 0.23f),
                .mSkyZenith = osg::Vec3f(0.31f, 0.32f, 0.33f),
                .mAir = { .mColour = osg::Vec3f(0.41f, 0.42f, 0.43f), .mExtinction = 1.5e-4f, .mUniform = 0.75f },
                .mWaterLevel = -37.5f,
                .mSeconds = 12.25f,
                .mWeather = Rtx::Shaders::WEATHER_ASHSTORM,
                .mNextWeather = Rtx::Shaders::WEATHER_BLIGHT,
                .mWeatherBlend = 0.375f,
                .mWindSpeed = 0.8f,
                .mStormDirection = osg::Vec3f(0.6f, 0.8f, 0.0f),
            };

            world.mMoons[0] = MoonPlacement{
                .mDirection = osg::Vec3f(0.0f, 0.0f, 1.0f),
                .mRight = osg::Vec3f(1.0f, 0.0f, 0.0f),
                .mUp = osg::Vec3f(0.0f, 1.0f, 0.0f),
                .mAngularRadius = 0.1676f,
                .mPhaseAngle = 0.25f,
                .mAlpha = 0.5f,
                .mColour = osg::Vec3f(0.18f, 0.054f, 0.067f),
            };
            world.mMoons[1] = MoonPlacement{
                .mDirection = osg::Vec3f(0.0f, 1.0f, 0.0f),
                .mRight = osg::Vec3f(-1.0f, 0.0f, 0.0f),
                .mUp = osg::Vec3f(0.0f, 0.0f, 1.0f),
                .mAngularRadius = 0.0719f,
                .mPhaseAngle = 2.5f,
                .mAlpha = 0.25f,
                .mColour = osg::Vec3f(0.238f, 0.202f, 0.16f),
            };

            return world;
        }

        /// Every number the world decides reaches the constants, and reaches the right one.
        ///
        /// **The test the two paths never had.** The game and the harness each used to write these
        /// twenty-odd fields themselves, which is how the sea's clock came to be filled by one and
        /// left at zero by the other, and how the game's interiors came to run the outdoor fog field.
        /// One conversion is what fixed that; this is what says the conversion is complete.
        ///
        /// Written against a zeroed frame so that a field `applyWorld` forgets stays zero and fails
        /// here, rather than quietly carrying whatever the camera left behind.
        TEST(RtxFrameWorldTest, everyNumberTheWorldDecidesReachesTheFrame)
        {
            const FrameWorld world = distinct();

            Rtx::Shaders::VisibilityConstants constants{};
            applyWorld(world, constants);

            EXPECT_EQ(constants.mSunDirection, world.mSunDirection);
            EXPECT_EQ(constants.mSunIrradiance, world.mSunIrradiance);
            EXPECT_EQ(constants.mSunVisible, 1u);
            EXPECT_EQ(constants.mAmbient, world.mAmbient);
            EXPECT_EQ(constants.mSkyHorizon, world.mSkyHorizon);
            EXPECT_EQ(constants.mSkyZenith, world.mSkyZenith);

            EXPECT_EQ(constants.mFogColour, world.mAir.mColour);
            EXPECT_EQ(constants.mFogExtinction, world.mAir.mExtinction);
            EXPECT_EQ(constants.mFogUniform, world.mAir.mUniform) << "the game wrote this nowhere";

            EXPECT_EQ(constants.mWaterLevel, world.mWaterLevel);
            EXPECT_EQ(constants.mTime, world.mSeconds) << "the game wrote this nowhere either";

            EXPECT_EQ(constants.mWeather, world.mWeather);
            EXPECT_EQ(constants.mNextWeather, world.mNextWeather);
            EXPECT_EQ(constants.mWeatherBlend, world.mWeatherBlend);
            EXPECT_EQ(constants.mWindSpeed, world.mWindSpeed);
            EXPECT_EQ(constants.mStormDirection, world.mStormDirection);

            for (std::size_t moon = 0; moon < world.mMoons.size(); ++moon)
            {
                const MoonPlacement& placed = world.mMoons[moon];
                const Rtx::Shaders::MoonDisc& disc = constants.mMoons[moon];

                EXPECT_EQ(disc.mDirection, placed.mDirection) << "moon " << moon;
                EXPECT_EQ(disc.mRight, placed.mRight) << "moon " << moon;
                EXPECT_EQ(disc.mUp, placed.mUp) << "moon " << moon;
                EXPECT_EQ(disc.mColour, placed.mColour) << "moon " << moon;
                EXPECT_EQ(disc.mAngularRadius, placed.mAngularRadius) << "moon " << moon;
                EXPECT_EQ(disc.mPhaseAngle, placed.mPhaseAngle) << "moon " << moon;
                EXPECT_EQ(disc.mAlpha, placed.mAlpha) << "moon " << moon;
            }

            // **And the two moons are not one moon written twice**, which is what an index carried
            // through the loop by mistake would look like and what every field above would still
            // pass under.
            EXPECT_NE(constants.mMoons[0].mAlpha, constants.mMoons[1].mAlpha);
            EXPECT_NE(constants.mMoons[0].mDirection, constants.mMoons[1].mDirection);
        }

        /// The camera's half is left exactly as it was found.
        ///
        /// **The two halves of a frame meet in one struct and neither may write the other's.** The
        /// camera is built first — `mOrigin` is what the storm's direction is asked of — so a world
        /// that reset it would aim the ashstorm from wherever the last frame stood.
        TEST(RtxFrameWorldTest, theWorldLeavesTheCameraAlone)
        {
            Rtx::Shaders::VisibilityConstants constants{};
            constants.mOrigin = osg::Vec3f(1.0f, 2.0f, 3.0f);
            constants.mForward = osg::Vec3f(0.0f, 1.0f, 0.0f);
            constants.mRight = osg::Vec3f(1.0f, 0.0f, 0.0f);
            constants.mUp = osg::Vec3f(0.0f, 0.0f, 1.0f);
            constants.mWidth = 1280;
            constants.mHeight = 720;
            constants.mNear = 1.0f;
            constants.mFar = 12000.0f;
            constants.mSpreadAngle = 0.001f;
            constants.mFrame = 42;
            constants.mDelight = 0.5f;
            constants.mShowAlbedo = 1;
            constants.mTransparentBackground = 1;
            constants.mEmitterCount = 7;

            applyWorld(distinct(), constants);

            EXPECT_EQ(constants.mOrigin, osg::Vec3f(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(constants.mForward, osg::Vec3f(0.0f, 1.0f, 0.0f));
            EXPECT_EQ(constants.mRight, osg::Vec3f(1.0f, 0.0f, 0.0f));
            EXPECT_EQ(constants.mUp, osg::Vec3f(0.0f, 0.0f, 1.0f));
            EXPECT_EQ(constants.mWidth, 1280u);
            EXPECT_EQ(constants.mHeight, 720u);
            EXPECT_EQ(constants.mNear, 1.0f);
            EXPECT_EQ(constants.mFar, 12000.0f);
            EXPECT_EQ(constants.mSpreadAngle, 0.001f);
            EXPECT_EQ(constants.mFrame, 42u);
            EXPECT_EQ(constants.mDelight, 0.5f);
            EXPECT_EQ(constants.mShowAlbedo, 1u);
            EXPECT_EQ(constants.mTransparentBackground, 1u);
            EXPECT_EQ(constants.mEmitterCount, 7u);
        }

        /// A default world is a frame with no sky in it, which is what an interface trace wants.
        TEST(RtxFrameWorldTest, aWorldNobodyFilledDrawsNoSunAndNoMoons)
        {
            Rtx::Shaders::VisibilityConstants constants{};
            applyWorld(FrameWorld{}, constants);

            EXPECT_EQ(constants.mSunIrradiance, osg::Vec3f()) << "no sun";
            EXPECT_EQ(constants.mSunVisible, 0u) << "and no disc of one either";
            EXPECT_EQ(constants.mMoons[0].mAlpha, 0.0f) << "and no moons";
            EXPECT_EQ(constants.mMoons[1].mAlpha, 0.0f);
            EXPECT_EQ(constants.mFogExtinction, 0.0f) << "and air that costs nothing";

            // Minus infinity and not zero: zero is sea level, and a frame with no water has to
            // answer "how deep is this point" with never.
            EXPECT_LT(constants.mWaterLevel, -1.0e30f);

            // The blend is still readable at either end, which is what lets the shader mix without
            // testing for a transition.
            EXPECT_EQ(constants.mWeather, constants.mNextWeather);
            EXPECT_EQ(constants.mWeatherBlend, 0.0f);
        }
    }
}
