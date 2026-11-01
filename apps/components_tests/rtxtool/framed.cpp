#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <Base64.h>

#include <apps/rtxtool/framed.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/categories/video.hpp>
#include <components/settings/values.hpp>
#include <components/settings/windowmode.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A watched window keeps the player's frame-rate limit**, and an unwatched one none: a
        /// window that dropped it drew as fast as the card could under a player who had asked for 60.
        /// **And never the player's vertical sync**, which held a frame between two refreshes to
        /// one, two or three of them.
        /// **The shipped defaults are read once and asked as the game reads them**: a number through
        /// the finite reader, a switch as `true` in any case, text as it is; a setting the file
        /// names not, and a number that is not finite, are refused naming the setting. Read from
        /// the file when made, so a file gone after says the same.
        TEST(RtxShippedDefaultsTest, theShippedFileIsReadOnceAndAskedAsTheGameReadsIt)
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("shipped-defaults.bin");
            std::ofstream(file, std::ios::binary) << Base64::Base64::Encode(
                "[Camera]\nviewing distance = 7168.5\n[Terrain]\nobject paging = TRUE\n"
                "[General]\nanisotropy = 4\n[Shaders]\nnormal map pattern = _n\n"
                "[RTX]\nbroken = inf\n");
            const ShippedDefaults shipped(file);
            std::filesystem::remove(file);

            EXPECT_EQ(shipped.get<float>("Camera", "viewing distance"), 7168.5f);
            EXPECT_TRUE(shipped.get<bool>("Terrain", "object paging"));
            EXPECT_EQ(shipped.get<int>("General", "anisotropy"), 4);
            EXPECT_EQ(shipped.get<std::string>("Shaders", "normal map pattern"), "_n");
            EXPECT_THROW(shipped.get<float>("Camera", "field of view"), std::runtime_error);
            EXPECT_THROW(shipped.get<float>("RTX", "broken"), std::runtime_error);
        }

        TEST(RtxWindowRequestTest, aWatchedWindowKeepsThePlayersLimitAndNoVerticalSync)
        {
            const WindowRequest unwatched;
            EXPECT_EQ(unwatched.mFramerateLimit, 0.0f);

            Settings::VideoCategory& video = Settings::video();
            const float limit = video.mFramerateLimit;
            const SDLUtil::VSyncMode sync = video.mVsyncMode;
            const int width = video.mResolutionX;
            const int height = video.mResolutionY;
            const Settings::WindowMode mode = video.mWindowMode;
            const float fieldOfView = Settings::camera().mFieldOfView;
            video.mFramerateLimit.set(72.0f);
            video.mVsyncMode.set(SDLUtil::VSyncMode::Enabled);

            WindowRequest watched;
            watched.mWidth = 2560;
            watched.mHeight = 1440;
            watched.mFieldOfView = 75.0f;
            watched.keepPlayersLimit();
            EXPECT_EQ(watched.mFramerateLimit, 72.0f);

            watched.writeSettings();
            EXPECT_EQ(video.mVsyncMode, SDLUtil::VSyncMode::Disabled);
            EXPECT_EQ(video.mFramerateLimit, 72.0f);
            EXPECT_EQ(video.mResolutionX, 2560);
            EXPECT_EQ(video.mResolutionY, 1440);
            EXPECT_EQ(video.mWindowMode.get(), Settings::WindowMode::Windowed);
            EXPECT_EQ(Settings::camera().mFieldOfView, 75.0f);

            video.mFramerateLimit.set(limit);
            video.mVsyncMode.set(sync);
            video.mResolutionX.set(width);
            video.mResolutionY.set(height);
            video.mWindowMode.set(mode);
            Settings::camera().mFieldOfView.set(fieldOfView);
        }
    }
}
