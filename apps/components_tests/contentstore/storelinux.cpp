#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>

#include <gtest/gtest.h>

#include <unistd.h>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/digestwords.hpp>
#include <components/misc/hashstate.hpp>
#include <components/testing/util.hpp>

namespace ContentStore
{
    namespace
    {
        constexpr Policy sPolicy{ .mKindCount = 1 };
        constexpr Key sKey{ { 0x0123456789abcdefull, 0xfedcba9876543210ull } };

        /// An entry's tail: its one segment's record, 24 bytes whose first eight are the segment's end, and its
        /// trailer, 64 bytes whose table's digest stands at 32 and whose payload's length at 48.
        constexpr std::size_t sRecordBytes = 24;
        constexpr std::size_t sTailBytes = sRecordBytes + 64;
        constexpr std::size_t sTableDigestAt = sRecordBytes + 32;
        constexpr std::size_t sLengthAt = sRecordBytes + 48;

        /// One entry of 5000 bytes, past what the store holds in memory, written and on the disk.
        std::filesystem::path writeEntry(const std::filesystem::path& folder)
        {
            Store store(folder, sPolicy);
            store.write(Filed{
                .mKey = sKey, .mKind = 0, .mVersion = 1, .mPayload = Misc::ByteBuffer(5000), .mSegmentEnds = {} });
            store.flush();
            return store.pathOf(sKey, 0);
        }

        /// **A payload is bounded on the ranged read's path as on the whole one's**: a file whose trailer and length
        /// agree on a payload a byte past the most one holds is refused on its size and deleted, where the ranged
        /// read took a buffer by what the trailer said. Sparse, so the gigabyte is no gigabyte written.
        TEST(ContentStoreLinuxTest, anEntryPastTheMostAPayloadHoldsIsRefusedOnTheRangedRead)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path path = writeEntry(folder);

            std::array<char, sTailBytes> tail{};
            {
                std::ifstream file(path, std::ios::binary);
                file.seekg(5000);
                file.read(tail.data(), tail.size());
                ASSERT_TRUE(file.good());
            }
            // Its one segment ends where the payload does, and its table's digest holds: a file every other check
            // passes.
            const std::uint64_t past = Store::sPayloadMost + 1;
            std::memcpy(tail.data(), &past, sizeof(past));
            std::memcpy(tail.data() + sLengthAt, &past, sizeof(past));
            Misc::HashState table;
            table.add(std::as_bytes(std::span(tail).first(sRecordBytes)));
            const Misc::DigestWords digest = table.getWords();
            std::memcpy(tail.data() + sTableDigestAt, digest.data(), sizeof(digest));
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file.seekp(static_cast<std::streamoff>(past));
                file.write(tail.data(), tail.size());
            }
            ASSERT_EQ(std::filesystem::file_size(path), past + sTailBytes);

            Store store(folder, sPolicy);
            EXPECT_FALSE(store.open(sKey, 0, 1).has_value());
            EXPECT_FALSE(std::filesystem::exists(path));
            EXPECT_EQ(store.getStats().mRefused, 1u);
        }

        /// **A file the system will not open is a miss, and stays**: the fault is no fault of the entry's, which a
        /// refusal would delete for a play to compute again.
        TEST(ContentStoreLinuxTest, anEntryTheSystemWillNotOpenIsAMissAndStays)
        {
            if (geteuid() == 0)
                GTEST_SKIP() << "the superuser opens a file of no permissions";

            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path path = writeEntry(folder);
            std::filesystem::permissions(path, std::filesystem::perms::none);

            Store store(folder, sPolicy);
            EXPECT_FALSE(store.open(sKey, 0, 1).has_value());
            EXPECT_TRUE(std::filesystem::exists(path));
            EXPECT_EQ(store.getStats().mRefused, 0u);
            EXPECT_EQ(store.getStats().mMissed, 1u);

            std::filesystem::permissions(path, std::filesystem::perms::owner_all);
            EXPECT_TRUE(store.open(sKey, 0, 1).has_value()) << "the entry stands for the next ask";
        }
    }
}
