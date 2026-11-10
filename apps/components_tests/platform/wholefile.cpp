#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <components/misc/bytebuffer.hpp>
#include <components/platform/wholefile.hpp>
#include <components/testing/util.hpp>

namespace
{
    std::vector<std::filesystem::path> filesIn(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> files;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
            files.push_back(entry.path().filename());
        return files;
    }

    std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span(text));
    }

    /// **Parts written one after another, and read back byte for byte**, under either durability: an
    /// empty part adds nothing, a byte of nought is a byte like any other, and the temporary goes with
    /// the rename, so the folder holds the file alone.
    TEST(PlatformWholeFileTest, aFileIsReadBackAsItsPartsWereWritten)
    {
        const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
        for (const Platform::Durability durability : { Platform::Durability::Whole, Platform::Durability::Synced })
        {
            const std::filesystem::path path = folder / "parts.bin";
            const std::string_view zeroed("c\0d", 3);
            const std::array<std::span<const std::byte>, 3> parts{ bytesOf("ab"), bytesOf(""), bytesOf(zeroed) };
            ASSERT_TRUE(Platform::writeWhole(path, parts, durability).isOk());

            Misc::ByteBuffer read;
            ASSERT_TRUE(Platform::readWhole(path, read, Platform::sUnbounded).isOk());
            const std::string_view expected("abc\0d", 5);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(read.data()), read.size()), expected);

            std::string text;
            ASSERT_TRUE(Platform::readWhole(path, text, Platform::sUnbounded).isOk());
            EXPECT_EQ(text, expected) << "the string reader reads what the byte reader does";

            EXPECT_EQ(filesIn(folder), std::vector<std::filesystem::path>{ "parts.bin" })
                << "a temporary was left beside the file";
        }
    }

    /// **A write over a file replaces it whole**: a shorter file written over a longer one leaves
    /// none of the longer's tail, as a write in place that did not truncate would.
    TEST(PlatformWholeFileTest, aFileWrittenOverAnotherReplacesItWhole)
    {
        const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "settings.cfg";
        ASSERT_TRUE(
            Platform::writeWhole(path, std::string_view("a long first file"), Platform::Durability::Synced).isOk());
        ASSERT_TRUE(Platform::writeWhole(path, std::string_view("short"), Platform::Durability::Synced).isOk());

        std::string text;
        ASSERT_TRUE(Platform::readWhole(path, text, Platform::sUnbounded).isOk());
        EXPECT_EQ(text, "short");
    }

    /// **A write that cannot be made is `NotFound` where a folder is missing, and leaves nothing**:
    /// no file, no temporary. A read of a file that is not there is `NotFound` too, and empties what
    /// it was handed, so nothing of an earlier read stands as this one's.
    TEST(PlatformWholeFileTest, aPathThatIsNotThereIsNotFound)
    {
        const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
        const std::filesystem::path path = folder / "missing" / "file.bin";

        const auto written = Platform::writeWhole(path, std::string_view("x"), Platform::Durability::Whole);
        ASSERT_FALSE(written.isOk());
        EXPECT_EQ(written.error().mFailure, Platform::IoFailure::NotFound) << written.error().mCode.message();
        EXPECT_TRUE(written.error().mCode) << "the system's own error is kept for a log";
        EXPECT_TRUE(filesIn(folder).empty()) << "a failed write left something behind";

        std::string text = "an earlier read";
        const auto read = Platform::readWhole(path, text, Platform::sUnbounded);
        ASSERT_FALSE(read.isOk());
        EXPECT_EQ(read.error().mFailure, Platform::IoFailure::NotFound) << read.error().mCode.message();
        EXPECT_TRUE(text.empty());
    }

    /// **A read is bounded**: a file of exactly `most` bytes reads whole, and one a byte past it is
    /// `TooLarge`, refused on the size the system states before its bytes are read, and leaves what it
    /// was handed empty. Both buffers bound alike.
    TEST(PlatformWholeFileTest, aFilePastTheBoundIsTooLarge)
    {
        const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "four.bin";
        ASSERT_TRUE(Platform::writeWhole(path, std::string_view("1234"), Platform::Durability::Whole).isOk());

        Misc::ByteBuffer bytes;
        ASSERT_TRUE(Platform::readWhole(path, bytes, 4).isOk());
        EXPECT_EQ(bytes.size(), 4u);

        const auto past = Platform::readWhole(path, bytes, 3);
        ASSERT_FALSE(past.isOk());
        EXPECT_EQ(past.error().mFailure, Platform::IoFailure::TooLarge);
        EXPECT_EQ(past.error().mCode, std::make_error_code(std::errc::file_too_large));
        EXPECT_TRUE(bytes.empty());

        std::string text;
        ASSERT_TRUE(Platform::readWhole(path, text, 4).isOk());
        EXPECT_EQ(text, "1234");
        const auto pastText = Platform::readWhole(path, text, 0);
        ASSERT_FALSE(pastText.isOk());
        EXPECT_EQ(pastText.error().mFailure, Platform::IoFailure::TooLarge);
        EXPECT_TRUE(text.empty());
    }

    /// **A read keeps the buffer's room**: a reader that keeps its buffer, as a sampler of a sysfs
    /// file does, allocates nothing after its first read of a file no larger. And an empty file
    /// reads as nothing, which is no failure.
    TEST(PlatformWholeFileTest, aReadKeepsTheRoomItWasHanded)
    {
        const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
        ASSERT_TRUE(
            Platform::writeWhole(folder / "small.txt", std::string_view("123"), Platform::Durability::Whole).isOk());
        ASSERT_TRUE(Platform::writeWhole(folder / "empty.txt", std::string_view(), Platform::Durability::Whole).isOk());

        std::string text;
        text.reserve(8192);
        const std::size_t room = text.capacity();
        ASSERT_TRUE(Platform::readWhole(folder / "small.txt", text, Platform::sUnbounded).isOk());
        EXPECT_EQ(text, "123");
        EXPECT_EQ(text.capacity(), room) << "the read gave back the room it was handed";

        ASSERT_TRUE(Platform::readWhole(folder / "empty.txt", text, Platform::sUnbounded).isOk());
        EXPECT_TRUE(text.empty());
    }

    /// **Two writers of one file, each writing it over and over, both succeed**, and the file is one
    /// of them whole: every write takes a temporary of its own, so no write meets another's (a name
    /// two writes shared would refuse the second, which makes it exclusively).
    TEST(PlatformWholeFileTest, writersOfOneFileTakeTemporariesOfTheirOwn)
    {
        const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
        const std::filesystem::path path = folder / "shared.txt";
        const std::array<std::string, 2> texts{ std::string(1000, 'a'), std::string(2000, 'b') };

        std::array<int, 2> failed{ 0, 0 };
        {
            std::array<std::jthread, 2> writers;
            for (std::size_t at = 0; at < writers.size(); ++at)
                writers[at] = std::jthread([&, at] {
                    for (int write = 0; write < 50; ++write)
                        if (!Platform::writeWhole(path, std::string_view(texts[at]), Platform::Durability::Whole)
                                .isOk())
                            ++failed[at];
                });
        }
        EXPECT_EQ(failed[0], 0);
        EXPECT_EQ(failed[1], 0);

        std::string text;
        ASSERT_TRUE(Platform::readWhole(path, text, Platform::sUnbounded).isOk());
        EXPECT_TRUE(text == texts[0] || text == texts[1]) << "a mixed file of " << text.size() << " bytes";
        EXPECT_EQ(filesIn(folder), std::vector<std::filesystem::path>{ "shared.txt" });
    }
}
