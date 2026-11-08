#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <components/files/conversion.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        constexpr ContentKey sKey{ { 0x0123456789abcdefull, 0xfedcba9876543210ull } };
        constexpr ContentKey sOther{ { 0x1111111111111111ull, 0x2222222222222222ull } };
        constexpr ContentKey sThird{ { 0x3333333333333333ull, 0x4444444444444444ull } };

        /// The bytes an entry's file opens with, `EntryHeader`'s: four words, two keys' worth and a
        /// length, 4 × 4 + 16 + 16 + 8.
        constexpr std::size_t sHeaderBytes = 56;

        /// Past `ContentStore::sResidentMost`, so the store reads it from its file and not from memory.
        constexpr std::size_t sLarge = 5000;

        std::vector<std::byte> payloadOf(std::size_t bytes, unsigned seed)
        {
            std::vector<std::byte> payload(bytes);
            for (std::size_t at = 0; at < bytes; ++at)
                payload[at] = static_cast<std::byte>((at * 31 + seed) & 0xffu);
            return payload;
        }

        std::vector<char> readAll(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        void writeAll(const std::filesystem::path& path, const std::vector<char>& bytes)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }

        /// Every file under `folder`, at any depth, whose name ends with `suffix`.
        std::size_t countFiles(const std::filesystem::path& folder, const std::string& suffix)
        {
            std::size_t count = 0;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(folder))
                if (entry.is_regular_file() && Files::pathToUnicodeString(entry.path().filename()).ends_with(suffix))
                    ++count;
            return count;
        }

        /// One entry written and read back: the file stands where the key names it, as long as its
        /// header and payload, 56 + 5000 = 5056 bytes, in the folder of the key's first byte, `01`; and
        /// another store on the same folder reads it whole from its file. The frame's reach, memory
        /// alone, does not find an entry too large to hold there.
        TEST(RtxContentStoreTest, anEntryWrittenIsReadBackWholeByAnotherStoreOnTheSameFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::vector<std::byte> payload = payloadOf(sLarge, 7);
            {
                ContentStore store(folder, ContentStore::sGameCap);
                store.write(sKey, ContentPassId::AlphaSpan, 3, payload);
                store.flush();
                EXPECT_EQ(store.getStats().mWritten, 1u);
                EXPECT_EQ(store.pathOf(sKey), folder / "01" / "0123456789abcdeffedcba9876543210");
            }
            EXPECT_EQ(
                std::filesystem::file_size(folder / "01" / "0123456789abcdeffedcba9876543210"), sHeaderBytes + sLarge);
            EXPECT_EQ(countFiles(folder, ".partial"), 0u) << "the temporary was renamed into place";

            ContentStore again(folder, ContentStore::sGameCap);
            again.flush();
            std::vector<std::byte> read;
            EXPECT_FALSE(again.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Memory, read))
                << "too large to be held in memory, so the frame's reach does not find it";
            ASSERT_TRUE(again.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sOther, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read)) << "no file";

            const ContentStoreStats stats = again.getStats();
            EXPECT_EQ(stats.mFound, 1u);
            EXPECT_EQ(stats.mMissed, 2u);
            EXPECT_EQ(stats.mRefused, 0u);
        }

        /// **A small entry is in memory from the start**: a store made on a folder that holds one
        /// loads it as it sweeps, so the frame's reach finds it with its file gone.
        TEST(RtxContentStoreTest, aSmallEntryIsHeldInMemoryFromTheStart)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::vector<std::byte> payload = payloadOf(16, 3);
            {
                ContentStore store(folder, ContentStore::sGameCap);
                store.write(sKey, ContentPassId::TexelMean, 1, payload);
            }

            ContentStore again(folder, ContentStore::sGameCap);
            again.flush();
            std::filesystem::remove(again.pathOf(sKey));

            std::vector<std::byte> read;
            ASSERT_TRUE(again.read(sKey, ContentPassId::TexelMean, 1, StoreReach::Memory, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sKey, ContentPassId::TexelMean, 2, StoreReach::Memory, read))
                << "held for another version";
        }

        /// **Every file is untrusted**: a file shorter than its header, one whose magic, format, key,
        /// pass, length or digest is not what it should be, and one the ask holds against another
        /// version, are each a miss, and the file is gone after.
        TEST(RtxContentStoreTest, aFileThatDoesNotMatchIsAMissAndIsDeleted)
        {
            struct Fault
            {
                const char* mName;
                std::function<void(std::vector<char>&)> mSpoil;
                std::uint32_t mAskedVersion = 3;
            };
            const std::vector<Fault> faults{
                { "shorter than its header", [](std::vector<char>& bytes) { bytes.resize(30); } },
                { "its magic", [](std::vector<char>& bytes) { bytes[0] ^= 1; } },
                { "its format", [](std::vector<char>& bytes) { bytes[4] ^= 1; } },
                { "a pass past the last", [](std::vector<char>& bytes) { bytes[8] = 99; } },
                { "another key", [](std::vector<char>& bytes) { bytes[16] ^= 1; } },
                { "its payload", [](std::vector<char>& bytes) { bytes[sHeaderBytes + 100] ^= 1; } },
                { "a length past the file", [](std::vector<char>& bytes) { bytes[48] += 1; } },
                { "a length short of the file", [](std::vector<char>& bytes) { bytes.push_back(0); } },
                { "asked at another version", [](std::vector<char>&) {}, 4 },
            };

            for (const Fault& fault : faults)
            {
                const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / fault.mName;
                {
                    ContentStore store(folder, ContentStore::sGameCap);
                    store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 1));
                }

                ContentStore again(folder, ContentStore::sGameCap);
                again.flush();
                const std::filesystem::path path = again.pathOf(sKey);
                std::vector<char> bytes = readAll(path);
                fault.mSpoil(bytes);
                writeAll(path, bytes);

                std::vector<std::byte> read;
                EXPECT_FALSE(again.read(sKey, ContentPassId::AlphaSpan, fault.mAskedVersion, StoreReach::Disk, read))
                    << fault.mName;
                EXPECT_FALSE(std::filesystem::exists(path)) << fault.mName;
                EXPECT_EQ(again.getStats().mRefused, 1u) << fault.mName;
            }
        }

        /// **The sweep keeps the store under its cap, the least recently used leaving first**: three
        /// entries of 5056 bytes, last used three, two and one hours ago, under a cap of two of them,
        /// lose the oldest alone. A temporary file two days old is one a killed process left and
        /// goes; one written now is a live write and stays.
        TEST(RtxContentStoreTest, theSweepRemovesTheLeastRecentlyUsedPastTheCapAndStaleTemporaries)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            std::filesystem::path oldest;
            std::filesystem::path middle;
            std::filesystem::path newest;
            {
                ContentStore store(folder, ContentStore::sGameCap);
                store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 1));
                store.write(sOther, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 2));
                store.write(sThird, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 3));
                store.flush();
                oldest = store.pathOf(sKey);
                middle = store.pathOf(sOther);
                newest = store.pathOf(sThird);
            }
            std::filesystem::last_write_time(oldest, now - std::chrono::hours(3));
            std::filesystem::last_write_time(middle, now - std::chrono::hours(2));
            std::filesystem::last_write_time(newest, now - std::chrono::hours(1));

            const std::filesystem::path stale = folder / "01" / "0123456789abcdeffedcba9876543210.9-1.partial";
            const std::filesystem::path live = folder / "01" / "0123456789abcdeffedcba9876543210.9-2.partial";
            writeAll(stale, { 'x' });
            writeAll(live, { 'x' });
            std::filesystem::last_write_time(stale, now - std::chrono::hours(48));

            ContentStore swept(folder, 2 * (sHeaderBytes + sLarge));
            swept.flush();
            EXPECT_FALSE(std::filesystem::exists(oldest));
            EXPECT_TRUE(std::filesystem::exists(middle));
            EXPECT_TRUE(std::filesystem::exists(newest));
            EXPECT_FALSE(std::filesystem::exists(stale));
            EXPECT_TRUE(std::filesystem::exists(live));

            const ContentStoreStats stats = swept.getStats();
            EXPECT_EQ(stats.mEvicted, 1u);
            EXPECT_EQ(stats.mStale, 1u);
        }

        /// **The cap holds within a session too**: a store of a cap of two and a half entries that
        /// writes three passes it at the third, and sweeps to nine tenths of the cap, 11 376 bytes,
        /// which two of 5056 fit under and three do not, so one entry goes and two stay.
        TEST(RtxContentStoreTest, aSessionsWritesPastTheCapSweepTheLeastRecentlyUsed)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::uint64_t cap = 5 * (sHeaderBytes + sLarge) / 2;
            ContentStore store(folder, cap);
            store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 1));
            store.write(sOther, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 2));
            store.flush();
            EXPECT_EQ(store.getStats().mEvicted, 0u) << "two entries are under the cap";

            store.write(sThird, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 3));
            store.flush();
            EXPECT_EQ(store.getStats().mEvicted, 1u);
            EXPECT_EQ(countFiles(folder, ""), 2u) << "two of the three entries stand";
        }

        /// **A hit stamps its file's time once a session**, so the next start's sweep counts it as
        /// used now: a file two days old reads as used within the minute after a hit, and a second
        /// hit stamps nothing more.
        TEST(RtxContentStoreTest, aHitStampsItsFileOnceASession)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            {
                ContentStore store(folder, ContentStore::sGameCap);
                store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 1));
            }

            ContentStore again(folder, ContentStore::sGameCap);
            again.flush();
            const std::filesystem::path path = again.pathOf(sKey);
            std::filesystem::last_write_time(path, now - std::chrono::hours(48));

            std::vector<std::byte> read;
            ASSERT_TRUE(again.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            again.flush();
            EXPECT_GT(std::filesystem::last_write_time(path), now - std::chrono::minutes(1));
            EXPECT_EQ(again.getStats().mStamped, 1u);

            ASSERT_TRUE(again.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            again.flush();
            EXPECT_EQ(again.getStats().mStamped, 1u) << "once a session";
        }

        /// **A write of a key whose file stands stamps it and writes nothing**, as a frame thread that
        /// cannot reach the file offers its output again in a later session: a two-day-old file is
        /// used within the minute and keeps its payload, which is the same bytes in a real store and
        /// another here, to show it was not written.
        TEST(RtxContentStoreTest, aWriteOfAKeyWhoseFileStandsStampsItAndWritesNothing)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            const std::vector<std::byte> first = payloadOf(sLarge, 1);
            {
                ContentStore store(folder, ContentStore::sGameCap);
                store.write(sKey, ContentPassId::AlphaSpan, 3, first);
            }

            ContentStore again(folder, ContentStore::sGameCap);
            again.flush();
            const std::filesystem::path path = again.pathOf(sKey);
            std::filesystem::last_write_time(path, now - std::chrono::hours(48));
            again.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(sLarge, 2));
            again.flush();

            EXPECT_EQ(again.getStats().mWritten, 0u);
            EXPECT_EQ(again.getStats().mStamped, 1u);
            EXPECT_GT(std::filesystem::last_write_time(path), now - std::chrono::minutes(1));
            std::vector<std::byte> read;
            ASSERT_TRUE(again.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            EXPECT_EQ(read, first);
        }

        /// **A discarded entry is gone from memory and from its folder**, and counted refused: a small
        /// entry held from the start is found by neither reach after it.
        TEST(RtxContentStoreTest, aDiscardedEntryIsGoneFromMemoryAndFromItsFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            ContentStore store(folder, ContentStore::sGameCap);
            store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(16, 1));
            store.flush();
            ASSERT_TRUE(std::filesystem::exists(store.pathOf(sKey)));

            store.discard(sKey);
            std::vector<std::byte> read;
            EXPECT_FALSE(store.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Memory, read)) << "held still";
            store.flush();
            EXPECT_FALSE(std::filesystem::exists(store.pathOf(sKey)));
            EXPECT_FALSE(store.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            EXPECT_EQ(store.getStats().mRefused, 1u);
        }

        /// **The queue is bounded**: a payload past what it holds is dropped and counted, and no file
        /// is written for it.
        TEST(RtxContentStoreTest, aWritePastTheQueuesBoundIsDropped)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            ContentStore store(folder, ContentStore::sGameCap);
            store.write(sKey, ContentPassId::AlphaSpan, 3, payloadOf(ContentStore::sQueuedMost + 1, 0));
            store.flush();

            const ContentStoreStats stats = store.getStats();
            EXPECT_EQ(stats.mDropped, 1u);
            EXPECT_EQ(stats.mWritten, 0u);
            EXPECT_FALSE(std::filesystem::exists(store.pathOf(sKey)));
        }

        /// **Two stores writing one key leave one whole entry**, as a game and a tool may: each
        /// writes through a temporary of its own and renames it into place. Two stores of one process
        /// name their first write's temporary apart, and one store names its first and second apart.
        TEST(RtxContentStoreTest, twoStoresWritingOneKeyLeaveOneWholeEntry)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::vector<std::byte> payload = payloadOf(sLarge, 9);
            {
                ContentStore first(folder, ContentStore::sGameCap);
                ContentStore second(folder, ContentStore::sGameCap);
                EXPECT_NE(first.partialOf(sKey, 0), second.partialOf(sKey, 0));
                EXPECT_NE(first.partialOf(sKey, 0), first.partialOf(sKey, 1));
                EXPECT_EQ(first.partialOf(sKey, 0).parent_path(), first.pathOf(sKey).parent_path())
                    << "in the entry's own folder, so the rename stays on one volume";
                first.write(sKey, ContentPassId::AlphaSpan, 3, payload);
                second.write(sKey, ContentPassId::AlphaSpan, 3, payload);
            }
            EXPECT_EQ(countFiles(folder, ".partial"), 0u);

            ContentStore reader(folder, ContentStore::sGameCap);
            std::vector<std::byte> read;
            ASSERT_TRUE(reader.read(sKey, ContentPassId::AlphaSpan, 3, StoreReach::Disk, read));
            EXPECT_EQ(read, payload);
        }
    }
}
