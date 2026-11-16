#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include <components/files/constrainedfilestream.hpp>
#include <components/files/constrainedfilestreambuf.hpp>
#include <components/testing/util.hpp>

namespace
{
    // Long enough that an entry spans more than one of the stream's 8 KiB buffers.
    constexpr std::size_t sFileSize = 20000;
    constexpr std::size_t sStart = 5000;
    constexpr std::size_t sLength = 10000;

    char byteAt(std::size_t index)
    {
        return static_cast<char>((index * 7 + index / 251) & 0xFF);
    }

    std::filesystem::path writeFile()
    {
        std::string content(sFileSize, '\0');
        for (std::size_t i = 0; i < sFileSize; ++i)
            content[i] = byteAt(i);
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        const std::filesystem::path path
            = TestingOpenMW::outputFilePath(std::string(info->test_suite_name()) + "." + info->name());
        std::ofstream(path, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }

    std::string expected(std::size_t from, std::size_t count)
    {
        std::string bytes(count, '\0');
        for (std::size_t i = 0; i < count; ++i)
            bytes[i] = byteAt(from + i);
        return bytes;
    }

    std::string readRest(std::istream& stream)
    {
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    TEST(FilesConstrainedFileStreamTest, readsItsRangeAndEndsWhereTheRangeDoes)
    {
        const Files::IStreamPtr stream = Files::openConstrainedFileStream(writeFile(), sStart, sLength);
        EXPECT_EQ(readRest(*stream), expected(sStart, sLength));
        EXPECT_EQ(stream->get(), std::char_traits<char>::eof());
    }

    TEST(FilesConstrainedFileStreamTest, aFileOfItsOwnRunsToTheFilesEnd)
    {
        const Files::IStreamPtr stream = Files::openConstrainedFileStream(writeFile(), sStart);
        // 20000 - 5000 bytes, the file's from the start.
        EXPECT_EQ(readRest(*stream), expected(sStart, sFileSize - sStart));
    }

    TEST(FilesConstrainedFileStreamTest, aReadPastTheEndStopsAtTheEnd)
    {
        const Files::IStreamPtr stream = Files::openConstrainedFileStream(writeFile(), sStart, sLength);
        std::string bytes(20, '\0');
        stream->seekg(9990);
        stream->read(bytes.data(), 20);
        // 10 of the 20 asked: 9990 to the range's 10000.
        EXPECT_EQ(stream->gcount(), 10);
        EXPECT_TRUE(stream->eof());
        EXPECT_EQ(bytes.substr(0, 10), expected(sStart + 9990, 10));
    }

    TEST(FilesConstrainedFileStreamTest, seeksAnswerAsAFilesDo)
    {
        const Files::IStreamPtr stream = Files::openConstrainedFileStream(writeFile(), sStart, sLength);

        // The end itself is a position, which reads nothing.
        stream->seekg(10000);
        EXPECT_EQ(stream->tellg(), std::streampos(10000));
        EXPECT_EQ(stream->get(), std::char_traits<char>::eof());

        // Past the end and before the start are refused.
        stream->clear();
        stream->seekg(10001);
        EXPECT_TRUE(stream->fail());
        stream->clear();
        stream->seekg(-1, std::ios::beg);
        EXPECT_TRUE(stream->fail());

        // From the current position, taken back across a buffer's edge: 8193 read, one back is 8192.
        stream->clear();
        stream->seekg(0);
        std::string bytes(8193, '\0');
        stream->read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream->seekg(-1, std::ios::cur);
        EXPECT_EQ(stream->tellg(), std::streampos(8192));
        EXPECT_EQ(stream->get(), std::char_traits<char>::to_int_type(byteAt(sStart + 8192)));

        // From the end.
        stream->seekg(-3, std::ios::end);
        EXPECT_EQ(readRest(*stream), expected(sStart + sLength - 3, 3));
    }

    TEST(FilesConstrainedFileStreamTest, streamsOverOneFileKeepTheirOwnPositionsAndOutliveTheOwner)
    {
        Files::SharedFile file = Files::openSharedFile(writeFile());
        const Files::IStreamPtr first = Files::openConstrainedFileStream(file, 0, sFileSize);
        const Files::IStreamPtr second = Files::openConstrainedFileStream(file, sStart, sLength);
        file.reset();

        std::string bytes(3, '\0');
        first->read(bytes.data(), 3);
        EXPECT_EQ(bytes, expected(0, 3));
        second->read(bytes.data(), 3);
        EXPECT_EQ(bytes, expected(sStart, 3));
        first->read(bytes.data(), 3);
        EXPECT_EQ(bytes, expected(3, 3));
        EXPECT_EQ(readRest(*second), expected(sStart + 3, sLength - 3));
    }
}
