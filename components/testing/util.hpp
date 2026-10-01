#ifndef OPENMW_COMPONENTS_TESTING_UTIL_H
#define OPENMW_COMPONENTS_TESTING_UTIL_H

#include <chrono>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <components/misc/strings/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/file.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace TestingOpenMW
{
    /// A directory of this run's own. **The process and not only the clock**: no two processes
    /// running at once share an id, where two copies of a binary started together could share a
    /// tick; the clock keeps apart two runs a reused id would join.
    inline std::filesystem::path outputDir()
    {
        static const std::string run = std::format(
            "{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), Platform::Process::currentId());
        std::filesystem::path dir = std::filesystem::temp_directory_path() / "openmw" / "tests" / run;
        std::filesystem::create_directories(dir);
        return dir;
    }

    inline std::filesystem::path outputFilePath(std::string_view name)
    {
        std::filesystem::path dir = outputDir();
        return dir / Misc::StringUtils::stringToU8String(name);
    }

    inline std::filesystem::path outputDirPath(const std::filesystem::path& subpath)
    {
        std::filesystem::path path = outputDir();
        path /= subpath;
        std::filesystem::create_directories(path);
        return path;
    }

    /// The name of `test`'s directory under this run's own, which `currentTestDirPath` makes and
    /// `FreshTestDirs` empties.
    inline std::string testDirName(const ::testing::TestInfo& test)
    {
        return std::format("{}.{}", test.test_suite_name(), test.name());
    }

    inline std::filesystem::path currentTestDirPath()
    {
        return outputDirPath(testDirName(*::testing::UnitTest::GetInstance()->current_test_info()));
    }

    /// Empties each test's own directory as the test starts, so a test run again in one process —
    /// `--gtest_repeat` — starts from nothing, as its first pass did, and not from what that pass
    /// left. Appended to the listeners by a binary's `main`.
    class FreshTestDirs : public ::testing::EmptyTestEventListener
    {
        void OnTestStart(const ::testing::TestInfo& test) override
        {
            std::filesystem::remove_all(outputDir() / testDirName(test));
        }
    };

    inline std::filesystem::path outputFilePathWithSubDir(const std::filesystem::path& subpath)
    {
        std::filesystem::path path = outputDir();
        path /= subpath;
        std::filesystem::create_directories(path.parent_path());
        return path;
    }

    class VFSTestFile : public VFS::File
    {
    public:
        explicit VFSTestFile(std::string content)
            : mContent(std::move(content))
        {
        }

        Files::IStreamPtr open() override { return std::make_unique<std::stringstream>(mContent, std::ios_base::in); }

        std::filesystem::file_time_type getLastModified() const override { return {}; }

        std::string getStem() const override { return "TestFile"; }

    private:
        const std::string mContent;
    };

    struct VFSTestData : public VFS::Archive
    {
        VFS::FileMap mFiles;

        explicit VFSTestData(VFS::FileMap&& files)
            : mFiles(std::move(files))
        {
        }

        void listResources(VFS::FileMap& out) override { out = mFiles; }

        bool contains(VFS::Path::NormalizedView file) const override { return mFiles.contains(file); }

        std::string getDescription() const override { return "TestData"; }
    };

    inline std::unique_ptr<VFS::Manager> createTestVFS(VFS::FileMap&& files)
    {
        auto vfs = std::make_unique<VFS::Manager>();
        vfs->addArchive(std::make_unique<VFSTestData>(std::move(files)));
        vfs->buildIndex();
        return vfs;
    }

    inline std::unique_ptr<VFS::Manager> createTestVFS(
        std::initializer_list<std::pair<VFS::Path::NormalizedView, VFS::File*>> files)
    {
        return createTestVFS(VFS::FileMap(files.begin(), files.end()));
    }
}

#endif
