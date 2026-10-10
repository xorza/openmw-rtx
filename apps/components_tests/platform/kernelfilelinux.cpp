#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <components/platform/kernelfilelinux.hpp>

namespace
{
    /// `text` as a descriptor reads it: the read end of a pipe whose write end is closed.
    int descriptorOf(const std::string_view text)
    {
        int ends[2];
        EXPECT_EQ(pipe(ends), 0);
        EXPECT_EQ(write(ends[1], text.data(), text.size()), static_cast<ssize_t>(text.size()));
        close(ends[1]);
        return ends[0];
    }

    /// **A text is read to its end, and a failure is its `errno`**: three bytes in a four-byte
    /// buffer are whole, four fill it and are `EFBIG`, since a fifth may follow, and a folder opens
    /// but fails its read with `EISDIR`, which a stream would have thrown.
    TEST(RtxPlatformKernelFileTest, aTextIsReadToItsEndAndAFailureIsItsErrno)
    {
        std::array<char, 4> buffer;
        for (const auto& [text, error] : { std::pair{ std::string_view(""), 0 },
                 std::pair{ std::string_view("abc"), 0 }, std::pair{ std::string_view("abcd"), EFBIG } })
        {
            SCOPED_TRACE(text);
            const int descriptor = descriptorOf(text);
            const Platform::KernelFile::Read read = Platform::KernelFile::readOpened(descriptor, buffer);
            close(descriptor);
            EXPECT_EQ(read.mText, text);
            EXPECT_EQ(read.mError, error);
        }

        const int folder = open(std::filesystem::temp_directory_path().c_str(), O_RDONLY | O_CLOEXEC);
        ASSERT_NE(folder, -1);
        const Platform::KernelFile::Read read = Platform::KernelFile::readOpened(folder, buffer);
        close(folder);
        EXPECT_EQ(read.mText, "");
        EXPECT_EQ(read.mError, EISDIR);
    }

    /// **A path is its whole text, or nothing**: a file that fits, and nothing for one that fills
    /// the buffer, one that is not there, and a folder.
    TEST(RtxPlatformKernelFileTest, aPathIsItsWholeTextOrNothing)
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path()
            / ("openmw-kernelfile-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        std::ofstream(folder / "fits") << "0-7\n";
        std::ofstream(folder / "fills") << "0-15";

        std::array<char, 4> small;
        std::array<char, 8> large;
        EXPECT_EQ(Platform::KernelFile::read((folder / "fits").c_str(), large), "0-7\n");
        EXPECT_EQ(Platform::KernelFile::read((folder / "fills").c_str(), small), std::nullopt);
        EXPECT_EQ(Platform::KernelFile::read((folder / "missing").c_str(), large), std::nullopt);
        EXPECT_EQ(Platform::KernelFile::read(folder.c_str(), large), std::nullopt);

        std::filesystem::remove_all(folder);
    }
}
