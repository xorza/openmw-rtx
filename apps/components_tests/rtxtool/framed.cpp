#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <Base64.h>

#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
#include <apps/rtxtool/framed.hpp>
#include <apps/rtxtool/toolline.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/categories/camera.hpp>
#include <components/settings/categories/general.hpp>
#include <components/settings/categories/groundcover.hpp>
#include <components/settings/categories/rtx.hpp>
#include <components/settings/categories/shaders.hpp>
#include <components/settings/categories/terrain.hpp>
#include <components/settings/categories/video.hpp>
#include <components/settings/values.hpp>
#include <components/settings/windowmode.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A watched window keeps the player's frame-rate limit**, and an unwatched one none. **Never the
        /// player's vertical sync**, which held a frame between two refreshes. **The shipped defaults are
        /// read once and asked as the game reads them**: a number through the finite reader, a switch as
        /// `true` in any case, text as it is; a setting the file does not name, or a number that is not
        /// finite, is refused naming the setting. Read when made, so a file gone after says the same.
        TEST(RtxShippedDefaultsTest, theShippedFileIsReadOnceAndAskedAsTheGameReadsIt)
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("shipped-defaults.bin");
            std::ofstream(file, std::ios::binary) << Base64::Base64::Encode(
                "[Camera]\nviewing distance = 7168.5\n[Terrain]\nobject paging = TRUE\n"
                "[General]\nanisotropy = 4\n[Shaders]\nnormal map pattern = _n\n"
                "[RTX]\ndistant land cells = inf\n");
            const ShippedDefaults shipped(file);
            std::filesystem::remove(file);

            EXPECT_EQ(shipped.get(Settings::camera().mViewingDistance), 7168.5f);
            EXPECT_TRUE(shipped.get(Settings::terrain().mObjectPaging));
            EXPECT_EQ(shipped.get(Settings::general().mAnisotropy), 4);
            EXPECT_EQ(shipped.get(Settings::shaders().mNormalMapPattern), "_n");
            EXPECT_THROW(shipped.get(Settings::camera().mFieldOfView), std::runtime_error);
            EXPECT_THROW(shipped.get(Settings::rtx().mDistantLandCells), std::runtime_error);
        }

        /// The shipped file every setting a measured run reads is named in, each a value of its own
        /// so a test tells them apart.
        ShippedDefaults everyShipped()
        {
            const std::filesystem::path file = TestingOpenMW::outputFilePath("every-shipped.bin");
            std::ofstream(file, std::ios::binary) << Base64::Base64::Encode(
                "[RTX]\ndistant land cells = 3.5\n[Camera]\nviewing distance = 7168\n"
                "[Terrain]\nobject paging = false\nobject paging min size = 0.02\n"
                "[Groundcover]\nenabled = true\nrendering distance = 6144\ndensity = 0.5\npoint lighting = false\n"
                "[General]\nanisotropy = 8\n[Video]\ngamma = 1.25\n"
                "[Shaders]\napply lighting to environment maps = true\n");
            ShippedDefaults shipped(file);
            std::filesystem::remove(file);
            return shipped;
        }

        /// **A measured run reads the shipped file, but for its upscaling and its bounces**, which are
        /// the line's whether it names them or not: the harness's own defaults, so a picture does not
        /// change with the card or the settings file it was measured beside.
        TEST(RtxFramedTest, aMeasuredRunReadsTheShippedFileButForItsUpscalingAndBounces)
        {
            ToolLine::Frame line;
            line.mUpscale = { Rtx::Upscale::Balanced, false };
            line.mUpscaler = { Rtx::UpscalerChoice::Fsr, false };
            line.mReconstruction.mSampling.mIndirect = false;
            line.mReconstruction.mSampling.mBounceRays = 2;

            const MWRender::RtxSettingValues measured = measuredSettings(everyShipped(), line);
            EXPECT_EQ(measured.mUpscale, Rtx::Upscale::Balanced);
            EXPECT_EQ(measured.mUpscaler, Rtx::UpscalerChoice::Fsr);
            EXPECT_FALSE(measured.mIndirectLighting);
            EXPECT_EQ(measured.mBounceRays, 2);
            EXPECT_EQ(measured.mDistantLandCells, 3.5f);
            EXPECT_EQ(measured.mViewingDistance, 7168.0f);
            EXPECT_FALSE(measured.mObjectPaging);
            EXPECT_EQ(measured.mObjectPagingMinSize, 0.02f);
            EXPECT_TRUE(measured.mGroundcover);
            EXPECT_EQ(measured.mGroundcoverDistance, 6144.0f);
            EXPECT_EQ(measured.mGroundcoverDensity, 0.5f);
            EXPECT_FALSE(measured.mGroundcoverPointLighting);
            EXPECT_EQ(measured.mAnisotropy, 8);
            EXPECT_EQ(measured.mGamma, 1.25f);
            EXPECT_TRUE(measured.mLitEnvironmentMaps);
        }

        /// **What the line names is the line's, and the rest the source's**: a window that names nothing is
        /// the player's own, the upscaler among it; one that names every option is the line's in each.
        TEST(RtxFramedTest, whatTheLineNamesIsTheLinesAndTheRestTheSources)
        {
            const MWRender::RtxSettingValues player{ .mUpscale = Rtx::Upscale::Performance,
                .mUpscaler = Rtx::UpscalerChoice::Dlss,
                .mIndirectLighting = true,
                .mBounceRays = 1,
                .mDistantLandCells = 4.0f,
                .mObjectPaging = true,
                .mGroundcover = false,
                .mGamma = 1.0f };

            ToolLine::Frame unnamed;
            unnamed.mUpscale = { Rtx::Upscale::Quality, false };
            unnamed.mUpscaler = { Rtx::UpscalerChoice::Fsr, false };
            unnamed.mReconstruction.mSampling.mIndirect = false;
            unnamed.mReconstruction.mSampling.mBounceRays = 3;
            const MWRender::RtxSettingValues kept = overLine(unnamed, player);
            EXPECT_EQ(kept.mUpscale, Rtx::Upscale::Performance);
            EXPECT_EQ(kept.mUpscaler, Rtx::UpscalerChoice::Dlss);
            EXPECT_TRUE(kept.mIndirectLighting);
            EXPECT_EQ(kept.mBounceRays, 1);
            EXPECT_EQ(kept.mDistantLandCells, 4.0f);
            EXPECT_TRUE(kept.mObjectPaging);
            EXPECT_FALSE(kept.mGroundcover);
            EXPECT_EQ(kept.mGamma, 1.0f);

            ToolLine::Frame named = unnamed;
            named.mUpscale.mNamed = true;
            named.mUpscaler.mNamed = true;
            named.mBouncesNamed = { .mIndirect = true, .mBounceRays = true };
            named.mGrass = true;
            named.mDistantCells = 2.5f;
            named.mDistantStatics = false;
            named.mGamma = 1.5f;
            const MWRender::RtxSettingValues taken = overLine(named, player);
            EXPECT_EQ(taken.mUpscale, Rtx::Upscale::Quality);
            EXPECT_EQ(taken.mUpscaler, Rtx::UpscalerChoice::Fsr);
            EXPECT_FALSE(taken.mIndirectLighting);
            EXPECT_EQ(taken.mBounceRays, 3);
            EXPECT_EQ(taken.mDistantLandCells, 2.5f);
            EXPECT_FALSE(taken.mObjectPaging);
            EXPECT_TRUE(taken.mGroundcover);
            EXPECT_EQ(taken.mGamma, 1.5f);
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
