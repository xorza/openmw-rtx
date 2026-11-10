#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <components/platform/dlss.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **The file the launcher downloads is one the game finds**, and a folder counts as installed
    /// where a regular file there names a feature library, whichever of the folders holds it: not a
    /// folder of another name, not a library NGX would not load, not a folder that is missing or
    /// unnamed.
    TEST(RtxPlatformDlssTest, theDownloadedLibraryIsInstalledWhereverAFolderHoldsIt)
    {
        const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
        const std::filesystem::path empty = root / "empty";
        const std::filesystem::path other = root / "other";
        const std::filesystem::path held = root / "held";
        const std::filesystem::path missing = root / "missing";
        const std::filesystem::path fileName(Platform::Dlss::download().mFileName);
        std::filesystem::create_directories(empty);
        std::filesystem::create_directories(other / fileName);
        std::ofstream(other / "nvngx_dlssg.dll") << "frame generation";
        std::ofstream(other / "libnvidia-ngx-dlssg.so.310.9.16") << "frame generation";
        std::filesystem::create_directories(held);
        std::ofstream(held / fileName) << "library";

        EXPECT_TRUE(Platform::Dlss::isFeatureLibrary(fileName));
        EXPECT_FALSE(Platform::Dlss::isFeatureLibrary("nvngx_dlssg.dll"));
        EXPECT_FALSE(Platform::Dlss::isFeatureLibrary("libnvidia-ngx-dlssg.so.310.9.16"));

        EXPECT_TRUE(Platform::Dlss::installedIn({ .mRuntime = held, .mGame = missing }));
        EXPECT_TRUE(Platform::Dlss::installedIn({ .mRuntime = empty, .mGame = held }));
        EXPECT_FALSE(Platform::Dlss::installedIn({ .mRuntime = empty, .mGame = other }))
            << "a folder named as the library and libraries of another feature are no feature library";
        EXPECT_FALSE(Platform::Dlss::installedIn({ .mRuntime = missing, .mGame = {} }));
        EXPECT_FALSE(Platform::Dlss::installedIn({}));
    }

    /// **The runtime folder is `dlss` in the user's local data**, and none where the configuration
    /// names no local data; the game's folder is handed through as it stands.
    TEST(RtxPlatformDlssTest, theRuntimeFolderIsDlssInTheUsersLocalData)
    {
        const Platform::Dlss::Folders folders = Platform::Dlss::foldersIn("/local", "/game");
        EXPECT_EQ(folders.mRuntime, std::filesystem::path("/local") / "dlss");
        EXPECT_EQ(folders.mGame, std::filesystem::path("/game"));
        EXPECT_EQ(Platform::Dlss::foldersIn({}, "/game").mRuntime, std::filesystem::path());
    }
}
