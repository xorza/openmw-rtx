#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

#include <components/misc/result.hpp>
#include <components/platform/file.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **A positioned read answers the bytes asked, wherever the handle stands, and fewer only at the file's end**:
    /// of a file of ten bytes, all ten from the start; three from the fifth, after a read that left the handle
    /// elsewhere; the last two where five are asked from the ninth; and none at the end and past it.
    TEST(PlatformFileTest, aPositionedReadAnswersTheBytesAskedAndFewerOnlyAtTheFilesEnd)
    {
        const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "ten";
        std::ofstream(path, std::ios::binary) << "0123456789";
        const Platform::File::ScopedHandle file(Platform::File::open(path));

        const auto readAt = [&](const std::size_t offset, const std::size_t size) {
            std::array<char, 16> into{};
            const Result<std::size_t, std::error_code> read = Platform::File::readAt(file, offset, into.data(), size);
            EXPECT_TRUE(read.isOk()) << offset;
            return read.isOk() ? std::string(into.data(), read.value()) : std::string("failed");
        };

        EXPECT_EQ(readAt(0, 10), "0123456789");
        EXPECT_EQ(readAt(4, 3), "456");
        EXPECT_EQ(readAt(0, 2), "01") << "a read at an offset moved the next one";
        EXPECT_EQ(readAt(8, 5), "89") << "the file's end";
        EXPECT_EQ(readAt(10, 5), "");
        EXPECT_EQ(readAt(100, 5), "");
        EXPECT_EQ(readAt(3, 0), "");
    }
}
