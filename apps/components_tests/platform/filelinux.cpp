#include <array>
#include <cstddef>
#include <system_error>

#include <gtest/gtest.h>

#include <components/misc/result.hpp>
#include <components/platform/file.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **A read the system fails is answered with the system's error, and nothing is thrown**: a folder opens for
    /// reading on Linux and reads as `EISDIR`.
    TEST(PlatformFileLinuxTest, aPositionedReadTheSystemFailsAnswersItsError)
    {
        const Platform::File::ScopedHandle folder(Platform::File::open(TestingOpenMW::currentTestDirPath()));
        std::array<char, 8> into{};
        const Result<std::size_t, std::error_code> read = Platform::File::readAt(folder, 0, into.data(), into.size());
        ASSERT_FALSE(read.isOk());
        EXPECT_EQ(read.error(), std::errc::is_a_directory);
    }
}
