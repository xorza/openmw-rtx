#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/digest.hpp>
#include <apps/rtxtool/instruments/drivercache.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// A build's shaders, as the build writes them under `resources/rtx`.
        std::filesystem::path writeShaders(const std::filesystem::path& root, const std::uint8_t last = 3)
        {
            const std::filesystem::path shaders = root / "rtx" / "shaders";
            std::filesystem::create_directories(shaders);
            std::ofstream(shaders / "a.spv", std::ios::binary | std::ios::trunc)
                << "\x01\x02" << static_cast<char>(last);
            std::ofstream(shaders / "b.spv", std::ios::binary | std::ios::trunc) << "\x04";
            return shaders;
        }

        std::string variable(const char* name)
        {
            const char* const value = std::getenv(name);
            return value == nullptr ? std::string("(unset)") : std::string(value);
        }

        TEST(RtxDriverCacheTest, aCacheSitsInTheHarnessFolderInADirectoryNamedByItsShadersDigest)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path shaders = writeShaders(root);

            const DriverCache cache(root / "rtxtool", shaders);

            EXPECT_EQ(cache.getDirectory(),
                root / "rtxtool" / "shaders-driver-cache" / spellHash(Rtx::digestShaders(shaders)));
            EXPECT_TRUE(std::filesystem::is_directory(cache.getDirectory()));
        }

        /// The variables are read back as the driver reads them. Left set: they are the driver's
        /// alone, and this binary makes no device.
        TEST(RtxDriverCacheTest, theDriverIsPointedAtTheCacheAndPrunesNothing)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const DriverCache cache(root / "rtxtool", writeShaders(root));

            cache.applyToDriver();
            EXPECT_EQ(variable("__GL_SHADER_DISK_CACHE"), "1");
            EXPECT_EQ(Platform::Process::environmentPath("__GL_SHADER_DISK_CACHE_PATH"), cache.getDirectory());
            EXPECT_EQ(variable("__GL_SHADER_DISK_CACHE_SIZE"), "8589934592") << "eight gibibytes, 8 << 30";
            EXPECT_EQ(variable("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP"), "1");

            EXPECT_EQ(variable("MESA_SHADER_CACHE_DISABLE"), "false");
            EXPECT_EQ(Platform::Process::environmentPath("MESA_SHADER_CACHE_DIR"), cache.getDirectory());
            EXPECT_EQ(variable("MESA_SHADER_CACHE_MAX_SIZE"), "8G") << "the same eight gibibytes, in Mesa's unit";
        }

        /// **A cache under a folder named outside every code page reaches the driver whole.** Cyrillic
        /// and CJK together, which no single Windows code page holds: a narrowed path is refused or
        /// becomes another folder there, and the variable is read back as the driver reads it.
        TEST(RtxDriverCacheTest, aCacheUnderAFolderNamedOutsideTheCodePageReachesTheDriverWhole)
        {
            const std::filesystem::path root
                = TestingOpenMW::currentTestDirPath() / std::filesystem::path(u8"\u043a\u044d\u0448-\u7f13\u5b58");
            const DriverCache cache(root / "rtxtool", writeShaders(root));

            cache.applyToDriver();
            EXPECT_EQ(Platform::Process::environmentPath("__GL_SHADER_DISK_CACHE_PATH"), cache.getDirectory());
            EXPECT_EQ(Platform::Process::environmentPath("MESA_SHADER_CACHE_DIR"), cache.getDirectory());
            EXPECT_TRUE(std::filesystem::is_directory(cache.getDirectory()));
        }

        /// **A build that changed a shader has one cache, the new one.** The old cache is of modules
        /// that are no longer there, so the sweep takes it and whatever else stands beside the new
        /// one — and leaves the shaders and everything else in the harness's folder alone.
        TEST(RtxDriverCacheTest, aChangedShaderMovesTheCacheAndTheSweepLeavesOnlyTheNewOne)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path shaders = writeShaders(root);
            const std::filesystem::path before = DriverCache(root / "rtxtool", shaders).getDirectory();
            std::ofstream(before / "a driver's entry") << "compiled";
            std::ofstream(before.parent_path() / "stray") << "not a cache";
            std::ofstream(root / "rtxtool" / "views.cfg") << "a neighbour";

            writeShaders(root, 7);
            const DriverCache after(root / "rtxtool", shaders);
            ASSERT_NE(after.getDirectory(), before);

            after.sweep();

            std::vector<std::filesystem::path> left;
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(after.getDirectory().parent_path()))
                left.push_back(entry.path());
            EXPECT_EQ(left, std::vector<std::filesystem::path>{ after.getDirectory() });
            EXPECT_TRUE(std::filesystem::exists(shaders / "a.spv"));
            EXPECT_TRUE(std::filesystem::exists(root / "rtxtool" / "views.cfg"));
        }
    }
}
