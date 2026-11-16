
#include <components/bsa/bsafile.hpp>
#include <components/files/memorystream.hpp>
#include <components/testing/util.hpp>

#include "operators.hpp" // IWYU pragma: keep: the operators a test's matchers and messages call

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <latch>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace Bsa
{
    namespace
    {
        using namespace ::testing;

        struct Free
        {
            void operator()(void* ptr) const { std::free(ptr); }
        };

        struct Buffer
        {
            std::unique_ptr<char, Free> mData;
            std::size_t mCapacity;
        };

        struct Header
        {
            uint32_t mFormat;
            uint32_t mDirSize;
            uint32_t mFileCount;
        };

        struct Archive
        {
            Header mHeader;
            std::vector<std::uint32_t> mOffsets;
            std::vector<char> mStringBuffer;
            std::vector<BSAFile::Hash> mHashes;
            std::size_t mTailSize;
        };

        struct TestBSAFile final : public BSAFile
        {
            void readHeader(std::istream& input) override { BSAFile::readHeader(input); }

            void writeHeader() override { throw std::logic_error("TestBSAFile::writeHeader is not implemented"); }
        };

        void writeArchive(const Archive& value, std::ostream& stream)
        {
            stream.write(reinterpret_cast<const char*>(&value.mHeader), sizeof(value.mHeader));

            if (!value.mOffsets.empty())
                stream.write(reinterpret_cast<const char*>(value.mOffsets.data()),
                    static_cast<std::streamsize>(value.mOffsets.size() * sizeof(std::uint32_t)));

            if (!value.mStringBuffer.empty())
                stream.write(reinterpret_cast<const char*>(value.mStringBuffer.data()),
                    static_cast<std::streamsize>(value.mStringBuffer.size()));

            for (const BSAFile::Hash& hash : value.mHashes)
                stream.write(reinterpret_cast<const char*>(&hash), sizeof(BSAFile::Hash));

            const std::size_t chunkSize = 4096;
            std::vector<char> chunk(chunkSize);
            for (std::size_t i = 0; i < value.mTailSize; i += chunkSize)
                stream.write(reinterpret_cast<const char*>(chunk.data()),
                    static_cast<std::streamsize>(std::min(chunk.size(), value.mTailSize - i)));
        }

        std::filesystem::path makeOutputPath()
        {
            const auto testInfo = UnitTest::GetInstance()->current_test_info();
            return TestingOpenMW::outputFilePath(
                std::format("{}.{}.bsa", testInfo->test_suite_name(), testInfo->name()));
        }

        Buffer makeBsaBuffer(std::uint32_t fileSize, std::uint32_t fileOffset)
        {
            std::ostringstream stream;

            const Header header{
                .mFormat = static_cast<std::uint32_t>(BsaVersion::Uncompressed),
                .mDirSize = 14,
                .mFileCount = 1,
            };

            const BSAFile::Hash hash{
                .mLow = 0xaaaabbbb,
                .mHigh = 0xccccdddd,
            };

            const Archive archive{
                .mHeader = header,
                .mOffsets = { fileSize, fileOffset, 0 },
                .mStringBuffer = { 'a', '\0' },
                .mHashes = { hash },
                .mTailSize = 0,
            };

            writeArchive(archive, stream);

            const std::string data = std::move(stream).str();

            const std::size_t capacity = static_cast<std::size_t>(fileSize) + static_cast<std::size_t>(fileOffset) + 34;
            std::unique_ptr<char, Free> buffer(reinterpret_cast<char*>(std::malloc(capacity)));

            if (buffer == nullptr)
                throw std::bad_alloc();

            // A buffer of bytes, not a string.
            // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
            std::memcpy(buffer.get(), data.data(), data.size());

            return Buffer{ .mData = std::move(buffer), .mCapacity = capacity };
        }

        TEST(BSAFileTest, shouldHandleEmpty)
        {
            const std::filesystem::path path = makeOutputPath();

            {
                std::ofstream stream;
                stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                stream.open(path, std::ios::binary);
            }

            BSAFile file;
            EXPECT_THROW(file.open(path), std::runtime_error);

            EXPECT_THAT(file.getList(), IsEmpty());
        }

        TEST(BSAFileTest, shouldHandleZeroFiles)
        {
            const std::filesystem::path path = makeOutputPath();

            {
                std::ofstream stream;
                stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

                stream.open(path, std::ios::binary);

                const Header header{
                    .mFormat = static_cast<std::uint32_t>(BsaVersion::Uncompressed),
                    .mDirSize = 0,
                    .mFileCount = 0,
                };

                const Archive archive{
                    .mHeader = header,
                    .mOffsets = {},
                    .mStringBuffer = {},
                    .mHashes = {},
                    .mTailSize = 0,
                };

                writeArchive(archive, stream);
            }

            BSAFile file;
            file.open(path);

            EXPECT_THAT(file.getList(), IsEmpty());
        }

        TEST(BSAFileTest, shouldHandleSingleFile)
        {
            const std::filesystem::path path = makeOutputPath();

            {
                std::ofstream stream;
                stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

                stream.open(path, std::ios::binary);

                const Header header{
                    .mFormat = static_cast<std::uint32_t>(BsaVersion::Uncompressed),
                    .mDirSize = 14,
                    .mFileCount = 1,
                };

                const BSAFile::Hash hash{
                    .mLow = 0xaaaabbbb,
                    .mHigh = 0xccccdddd,
                };

                const Archive archive{
                    .mHeader = header,
                    .mOffsets = { 42, 0, 0 },
                    .mStringBuffer = { 'a', '\0' },
                    .mHashes = { hash },
                    .mTailSize = 42,
                };

                writeArchive(archive, stream);
            }

            BSAFile file;
            file.open(path);

            std::vector<char> namesBuffer = { 'a', '\0' };

            EXPECT_THAT(file.getList(),
                ElementsAre(BSAFile::FileStruct{
                    .mFileSize = 42,
                    .mOffset = 34,
                    .mHash = BSAFile::Hash{ .mLow = 0xaaaabbbb, .mHigh = 0xccccdddd },
                    .mNameOffset = 0,
                    .mNameSize = 1,
                    .mNamesBuffer = &namesBuffer,
                }));
        }

        TEST(BSAFileTest, shouldHandleTwoFiles)
        {
            const std::filesystem::path path = makeOutputPath();

            {
                std::ofstream stream;
                stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

                stream.open(path, std::ios::binary);

                const std::uint32_t fileSize1 = 42;
                const std::uint32_t fileSize2 = 13;

                const Header header{
                    .mFormat = static_cast<std::uint32_t>(BsaVersion::Uncompressed),
                    .mDirSize = 28,
                    .mFileCount = 2,
                };

                const BSAFile::Hash hash1{
                    .mLow = 0xaaaabbbb,
                    .mHigh = 0xccccdddd,
                };

                const BSAFile::Hash hash2{
                    .mLow = 0x11112222,
                    .mHigh = 0x33334444,
                };

                const Archive archive{
                    .mHeader = header,
                    .mOffsets = { fileSize1, 0, fileSize2, fileSize1, 0, 2 },
                    .mStringBuffer = { 'a', '\0', 'b', '\0' },
                    .mHashes = { hash1, hash2 },
                    .mTailSize = fileSize1 + fileSize2,
                };

                writeArchive(archive, stream);
            }

            BSAFile file;
            file.open(path);

            std::vector<char> namesBuffer = { 'a', '\0', 'b', '\0' };

            EXPECT_THAT(file.getList(),
                ElementsAre(
                    BSAFile::FileStruct{
                        .mFileSize = 42,
                        .mOffset = 56,
                        .mHash = BSAFile::Hash{ .mLow = 0xaaaabbbb, .mHigh = 0xccccdddd },
                        .mNameOffset = 0,
                        .mNameSize = 1,
                        .mNamesBuffer = &namesBuffer,
                    },
                    BSAFile::FileStruct{
                        .mFileSize = 13,
                        .mOffset = 98,
                        .mHash = BSAFile::Hash{ .mLow = 0x11112222, .mHigh = 0x33334444 },
                        .mNameOffset = 2,
                        .mNameSize = 1,
                        .mNamesBuffer = &namesBuffer,
                    }));
        }

        /// **Entries read at once through the archive's one handle each read their own bytes**, and an entry's stream
        /// reads on after its archive is closed and gone.
        TEST(BSAFileTest, entriesReadAtOnceEachReadTheirOwnBytes)
        {
            constexpr std::size_t entryCount = 8;
            // Each past the stream's 8 KiB buffer, and each a different length.
            const auto entrySize = [](std::size_t entry) { return 9000 + 1111 * entry; };
            const auto byteOf = [](std::size_t entry, std::size_t at) {
                return static_cast<char>((entry * 31 + at * 7 + at / 253) & 0xFF);
            };
            const auto entryBytes = [&](std::size_t entry) {
                std::string bytes(entrySize(entry), '\0');
                for (std::size_t at = 0; at < bytes.size(); ++at)
                    bytes[at] = byteOf(entry, at);
                return bytes;
            };

            const std::filesystem::path path = makeOutputPath();
            {
                std::ofstream stream;
                stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                stream.open(path, std::ios::binary);

                Archive archive{
                    // A directory of 12 bytes an entry and its two-byte name.
                    .mHeader = { .mFormat = static_cast<std::uint32_t>(BsaVersion::Uncompressed),
                        .mDirSize = static_cast<std::uint32_t>(entryCount * (12 + 2)),
                        .mFileCount = static_cast<std::uint32_t>(entryCount) },
                    .mOffsets = std::vector<std::uint32_t>(3 * entryCount),
                    .mStringBuffer = {},
                    .mHashes = {},
                    .mTailSize = 0,
                };
                std::size_t offset = 0;
                for (std::size_t entry = 0; entry < entryCount; ++entry)
                {
                    archive.mOffsets[2 * entry] = static_cast<std::uint32_t>(entrySize(entry));
                    archive.mOffsets[2 * entry + 1] = static_cast<std::uint32_t>(offset);
                    archive.mOffsets[2 * entryCount + entry] = static_cast<std::uint32_t>(2 * entry);
                    archive.mStringBuffer.push_back(static_cast<char>('a' + entry));
                    archive.mStringBuffer.push_back('\0');
                    archive.mHashes.push_back(BSAFile::Hash{
                        .mLow = static_cast<std::uint32_t>(entry), .mHigh = static_cast<std::uint32_t>(entry) });
                    offset += entrySize(entry);
                }
                writeArchive(archive, stream);
                for (std::size_t entry = 0; entry < entryCount; ++entry)
                {
                    const std::string bytes = entryBytes(entry);
                    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                }
            }

            auto archive = std::make_unique<BSAFile>();
            archive->open(path);
            ASSERT_EQ(archive->getList().size(), entryCount);

            std::vector<Files::IStreamPtr> streams;
            for (const BSAFile::FileStruct& entry : archive->getList())
                streams.push_back(archive->getFile(&entry));
            std::vector<std::size_t> entries;
            for (const BSAFile::FileStruct& entry : archive->getList())
                entries.push_back(static_cast<std::size_t>(entry.name()[0] - 'a'));

            // Each thread reads a few bytes at a time, so the reads interleave.
            std::vector<std::string> read(entryCount);
            std::latch start(entryCount);
            {
                std::vector<std::jthread> threads;
                threads.reserve(entryCount);
                for (std::size_t index = 0; index < entryCount; ++index)
                    threads.emplace_back([&, index] {
                        start.arrive_and_wait();
                        std::array<char, 97> chunk{};
                        while (streams[index]->read(chunk.data(), chunk.size()) || streams[index]->gcount() > 0)
                            read[index].append(chunk.data(), static_cast<std::size_t>(streams[index]->gcount()));
                    });
            }
            for (std::size_t index = 0; index < entryCount; ++index)
                EXPECT_EQ(read[index], entryBytes(entries[index])) << "entry " << entries[index];

            const Files::IStreamPtr afterwards = archive->getFile(&archive->getList().back());
            const std::size_t lastEntry = entries.back();
            archive.reset();
            EXPECT_EQ(std::string(std::istreambuf_iterator<char>(*afterwards), std::istreambuf_iterator<char>()),
                entryBytes(lastEntry));
        }

        /// **An archive is written while it is held open for its entries' reads**, as the archive tool adds to one,
        /// and an entry added reads at once and after the archive is opened again.
        TEST(BSAFileTest, entriesAddedToAnOpenArchiveRead)
        {
            const std::filesystem::path path = makeOutputPath();
            std::filesystem::remove(path);
            const auto read = [](BSAFile& archive) {
                const Files::IStreamPtr stream = archive.getFile(&archive.getList().front());
                return std::string(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>());
            };

            {
                BSAFile archive;
                archive.open(path);
                std::istringstream entry("hello");
                archive.addFile("a", entry);
                ASSERT_EQ(archive.getList().size(), 1);
                EXPECT_EQ(read(archive), "hello");
            }

            BSAFile archive;
            archive.open(path);
            ASSERT_EQ(archive.getList().size(), 1);
            EXPECT_EQ(archive.getList().front().name(), "a");
            EXPECT_EQ(read(archive), "hello");
        }

        TEST(BSAFileTest, shouldHandleSomewhatLargeFiles)
        {
            constexpr std::uint32_t maxUInt32 = std::numeric_limits<uint32_t>::max();
            constexpr std::uint32_t fileSize = maxUInt32 / 4;
            constexpr std::uint32_t fileOffset = maxUInt32 / 4 - 34;
            const Buffer buffer = makeBsaBuffer(fileSize, fileOffset);

            TestBSAFile file;
            // Use capacity assuming we never read beyond small header.
            Files::IMemStream stream(buffer.mData.get(), buffer.mCapacity);
            file.readHeader(stream);

            std::vector<char> namesBuffer = { 'a', '\0' };

            EXPECT_THAT(file.getList(),
                ElementsAre(BSAFile::FileStruct{
                    .mFileSize = maxUInt32 / 4,
                    .mOffset = maxUInt32 / 4,
                    .mHash = BSAFile::Hash{ .mLow = 0xaaaabbbb, .mHigh = 0xccccdddd },
                    .mNameOffset = 0,
                    .mNameSize = 1,
                    .mNamesBuffer = &namesBuffer,
                }));
        }

// std::streambuf in MSVC does not support buffers larger than 2**31 - 1:
// https://developercommunity.visualstudio.com/t/stdbasic-stringbuf-is-broken/290124
#ifndef _MSC_VER
        TEST(BSAFileTest, shouldHandleSingleFileAtTheEndOfLargeFile)
        {
            constexpr std::uint32_t maxUInt32 = std::numeric_limits<uint32_t>::max();
            constexpr std::uint32_t fileSize = maxUInt32;
            constexpr std::uint32_t fileOffset = maxUInt32 - 34;
            const Buffer buffer = makeBsaBuffer(fileSize, fileOffset);

            TestBSAFile file;
            // Use capacity assuming we never read beyond small header.
            Files::IMemStream stream(buffer.mData.get(), buffer.mCapacity);
            file.readHeader(stream);

            std::vector<char> namesBuffer = { 'a', '\0' };

            EXPECT_THAT(file.getList(),
                ElementsAre(BSAFile::FileStruct{
                    .mFileSize = maxUInt32,
                    .mOffset = maxUInt32,
                    .mHash = BSAFile::Hash{ .mLow = 0xaaaabbbb, .mHigh = 0xccccdddd },
                    .mNameOffset = 0,
                    .mNameSize = 1,
                    .mNamesBuffer = &namesBuffer,
                }));
        }

        TEST(BSAFileTest, shouldThrowExceptionOnTooBigAbsoluteOffset)
        {
            constexpr std::uint32_t maxUInt32 = std::numeric_limits<uint32_t>::max();
            constexpr std::uint32_t fileSize = maxUInt32;
            constexpr std::uint32_t fileOffset = maxUInt32 - 34 + 1;
            const Buffer buffer = makeBsaBuffer(fileSize, fileOffset);

            TestBSAFile file;
            // Use capacity assuming we never read beyond small header.
            Files::IMemStream stream(buffer.mData.get(), buffer.mCapacity);
            EXPECT_THROW(file.readHeader(stream), std::runtime_error);

            EXPECT_THAT(file.getList(), IsEmpty());
        }
#endif
    }
}
