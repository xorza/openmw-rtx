#include <filesystem>
#include <fstream>
#include <string_view>

#include <gtest/gtest.h>

#include <components/dlss/dlss.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **The file the launcher downloads is one the game finds**, and a folder counts as installed
    /// where a regular file there names a feature library, whichever of the folders holds it: not a
    /// folder of another name, not a library NGX would not load, not a folder that is missing or
    /// unnamed.
    TEST(DlssLibraryTest, theDownloadedLibraryIsInstalledWhereverAFolderHoldsIt)
    {
        const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
        const std::filesystem::path empty = root / "empty";
        const std::filesystem::path other = root / "other";
        const std::filesystem::path held = root / "held";
        const std::filesystem::path missing = root / "missing";
        const std::filesystem::path fileName(DlssLibrary::download().mFileName);
        std::filesystem::create_directories(empty);
        std::filesystem::create_directories(other / fileName);
        std::ofstream(other / "nvngx_dlssg.dll") << "frame generation";
        std::ofstream(other / "libnvidia-ngx-dlssg.so.310.9.16") << "frame generation";
        std::ofstream(other / "nvngx_dlss.dll") << "super resolution";
        std::ofstream(other / "libnvidia-ngx-dlss.so.310.9.16") << "super resolution";
        std::filesystem::create_directories(held);
        std::ofstream(held / fileName) << "library";

        EXPECT_TRUE(DlssLibrary::isFeatureLibrary(fileName));
        EXPECT_FALSE(DlssLibrary::isFeatureLibrary("nvngx_dlssg.dll"));
        EXPECT_FALSE(DlssLibrary::isFeatureLibrary("libnvidia-ngx-dlssg.so.310.9.16"));
        EXPECT_FALSE(DlssLibrary::isFeatureLibrary("nvngx_dlss.dll"))
            << "Super Resolution's library, which Ray Reconstruction's replaced";
        EXPECT_FALSE(DlssLibrary::isFeatureLibrary("libnvidia-ngx-dlss.so.310.9.16"));
        EXPECT_NE(DlssLibrary::download().mFileName.find("dlssd"), std::string_view::npos)
            << "the download is Ray Reconstruction's library";

        EXPECT_TRUE(DlssLibrary::installedIn({ .mRuntime = held, .mGame = missing }));
        EXPECT_TRUE(DlssLibrary::installedIn({ .mRuntime = empty, .mGame = held }));
        EXPECT_FALSE(DlssLibrary::installedIn({ .mRuntime = empty, .mGame = other }))
            << "a folder named as the library and libraries of another feature, Super Resolution's among them, are no "
               "feature library";
        EXPECT_FALSE(DlssLibrary::installedIn({ .mRuntime = missing, .mGame = {} }));
        EXPECT_FALSE(DlssLibrary::installedIn({}));
    }

    /// **The runtime folder is `dlss` in the user's local data**, and none where the configuration
    /// names no local data; the game's folder is handed through as it stands.
    TEST(DlssLibraryTest, theRuntimeFolderIsDlssInTheUsersLocalData)
    {
        const DlssLibrary::Folders folders = DlssLibrary::foldersIn("/local", "/game");
        EXPECT_EQ(folders.mRuntime, std::filesystem::path("/local") / "dlss");
        EXPECT_EQ(folders.mGame, std::filesystem::path("/game"));
        EXPECT_EQ(DlssLibrary::foldersIn({}, "/game").mRuntime, std::filesystem::path());
    }
}
