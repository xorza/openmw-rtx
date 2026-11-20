#include <array>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/digestwords.hpp>
#include <components/misc/hashstate.hpp>
#include <components/testing/util.hpp>

namespace ContentStore
{
    namespace
    {
        /// An owner's three kinds: two facts, and a shape held in memory at any size, as the ray tracer's
        /// store holds its shapes.
        constexpr std::uint32_t sFact = 0;
        constexpr std::uint32_t sMean = 1;
        constexpr std::uint32_t sShape = 2;

        constexpr Policy sPolicy{ .mKindCount = 3 };

        /// A queue of 1 KiB, which two entries of more than half of it pass, where the owner's tens of megabytes
        /// would have a test build hundreds of megabytes of payload to reach its bound.
        constexpr Policy sSmallQueue{ .mKindCount = 3, .mQueuedBytesMost = 1024 };

        constexpr Key sKey{ { 0x0123456789abcdefull, 0xfedcba9876543210ull } };
        constexpr Key sOther{ { 0x1111111111111111ull, 0x2222222222222222ull } };
        constexpr Key sThird{ { 0x3333333333333333ull, 0x4444444444444444ull } };

        /// The bytes an entry of one segment holds past its payload: the segment's record, its end and its digest,
        /// 8 + 16, and the trailer, four words, two keys' worth, a length and two words more, 4 × 4 + 16 + 16 + 8 + 2
        /// × 4.
        constexpr std::size_t sRecordBytes = 24;
        constexpr std::size_t sTrailerBytes = 64;
        constexpr std::size_t sTailBytes = sRecordBytes + sTrailerBytes;

        /// Where a large entry's trailer starts, past its payload and its one record.
        constexpr std::size_t sTrailerAt = 5000 + sRecordBytes;

        /// Past `Store::sResidentMost`, so the store reads it from its file and not from memory.
        constexpr std::size_t sLarge = 5000;

        Misc::ByteBuffer payloadOf(std::size_t bytes, std::uint32_t seed)
        {
            Misc::ByteBuffer payload(bytes);
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

        /// One entry written and read back: the file stands where the key names it, as long as its payload, its one
        /// segment's record and its trailer, 5000 + 24 + 64 = 5088 bytes, in the folder of the key's first byte, `01`,
        /// in the folder of its kind, `0`, in the folder of the format, `v3`, and another store on the same folder
        /// reads it whole.
        TEST(ContentStoreTest, anEntryWrittenIsReadBackWholeByAnotherStoreOnTheSameFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const Misc::ByteBuffer payload = payloadOf(sLarge, 7);
            {
                Store store(folder, sPolicy);
                store.write(
                    Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payload, .mSegmentEnds = {} });
                store.flush();
                EXPECT_EQ(store.getStats().mWritten, 1u);
                EXPECT_EQ(store.pathOf(sKey, sFact), folder / "v3" / "0" / "01" / "0123456789abcdeffedcba9876543210");
                EXPECT_EQ(Store::sFormat, 3u);
            }
            EXPECT_EQ(std::filesystem::file_size(folder / "v3" / "0" / "01" / "0123456789abcdeffedcba9876543210"),
                sTailBytes + sLarge);
            EXPECT_EQ(countFiles(folder, ".partial"), 0u) << "the temporary was renamed into place";

            // **A flush with a patience answers what is left, and nought once nothing is**: the wait a start makes
            // under its loading screen, a few milliseconds at a time, ends with every write done as `flush` ends.
            {
                const std::filesystem::path apart = TestingOpenMW::currentTestDirPath() / "store-waited";
                std::filesystem::remove_all(apart);
                Store waited(apart, sPolicy);
                waited.write(
                    Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payload, .mSegmentEnds = {} });
                std::size_t asks = 0;
                while (waited.flushFor(std::chrono::milliseconds(1)) > 0)
                    ASSERT_LT(++asks, 60000u) << "the store never finished one write";
                EXPECT_EQ(waited.getStats().mWritten, 1u);
                EXPECT_EQ(waited.flushFor(std::chrono::milliseconds::zero()), 0u);
            }

            Store again(folder, sPolicy);
            again.flush();
            Misc::ByteBuffer read;
            ASSERT_TRUE(again.read(sKey, sFact, 3, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sOther, sFact, 3, read)) << "no file";

            const Stats stats = again.getStats();
            EXPECT_EQ(stats.mFound, 1u);
            EXPECT_EQ(stats.mMissed, 1u);
            EXPECT_EQ(stats.mRefused, 0u);
        }

        /// **Every file is untrusted**: a file shorter than its trailer, one whose magic, format, key,
        /// kind, length or digest is not what it should be, and one the ask holds against another
        /// version, are each a miss, and the file is gone after. **`holds` checks the trailer as the read
        /// does**, for the file's size, and refuses as the read refuses; the payload's digest and the
        /// version are the read's, so a file faulted there is held.
        TEST(ContentStoreTest, aFileThatDoesNotMatchIsAMissAndIsDeleted)
        {
            struct Fault
            {
                const char* mName;
                std::function<void(std::vector<char>&)> mSpoil;
                bool mHeld = false;
                std::uint32_t mAskedVersion = 3;
            };
            const std::vector<Fault> faults{
                { "shorter than its trailer", [](std::vector<char>& bytes) { bytes.resize(30); } },
                { "its magic", [](std::vector<char>& bytes) { bytes[sTrailerAt + 0] ^= 1; } },
                { "its format", [](std::vector<char>& bytes) { bytes[sTrailerAt + 4] ^= 1; } },
                { "a kind past the last", [](std::vector<char>& bytes) { bytes[sTrailerAt + 8] = 99; } },
                { "another key", [](std::vector<char>& bytes) { bytes[sTrailerAt + 16] ^= 1; } },
                { "another table's digest", [](std::vector<char>& bytes) { bytes[sTrailerAt + 32] ^= 1; }, true },
                { "another segment's digest", [](std::vector<char>& bytes) { bytes[sLarge + 8] ^= 1; }, true },
                { "no segment", [](std::vector<char>& bytes) { bytes[sTrailerAt + 56] = 0; } },
                { "more segments than a file holds", [](std::vector<char>& bytes) { bytes[sTrailerAt + 56] = 65; } },
                { "its payload", [](std::vector<char>& bytes) { bytes[100] ^= 1; }, true },
                { "a length past the file", [](std::vector<char>& bytes) { bytes[sTrailerAt + 48] += 1; } },
                { "a length short of the file", [](std::vector<char>& bytes) { bytes.push_back(0); } },
                { "asked at another version", [](std::vector<char>&) {}, true, 4 },
            };

            for (const Fault& fault : faults)
            {
                const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / fault.mName;
                {
                    Store store(folder, sPolicy);
                    store.write(Filed{ .mKey = sKey,
                        .mKind = sFact,
                        .mVersion = 3,
                        .mPayload = payloadOf(sLarge, 1),
                        .mSegmentEnds = {} });
                }

                Store again(folder, sPolicy);
                again.flush();
                const std::filesystem::path path = again.pathOf(sKey, sFact);
                std::vector<char> bytes = readAll(path);
                fault.mSpoil(bytes);
                writeAll(path, bytes);

                EXPECT_EQ(again.holds(sKey, sFact), fault.mHeld) << fault.mName;
                EXPECT_EQ(std::filesystem::exists(path), fault.mHeld) << fault.mName;
                EXPECT_EQ(again.getStats().mHeld, fault.mHeld ? 1u : 0u) << fault.mName;
                EXPECT_EQ(again.getStats().mUnheld, fault.mHeld ? 0u : 1u) << fault.mName;

                Misc::ByteBuffer read;
                EXPECT_FALSE(again.read(sKey, sFact, fault.mAskedVersion, read)) << fault.mName;
                EXPECT_FALSE(std::filesystem::exists(path)) << fault.mName;
                EXPECT_EQ(again.getStats().mRefused, 1u) << fault.mName;
            }
        }

        /// **The sweep removes stale temporaries and no entry**: the store has no cap, so three entries
        /// last used three, two and one hours ago all stay. A temporary file two days old is one a killed
        /// process left and goes; one written now is a live write and stays.
        TEST(ContentStoreTest, theSweepRemovesStaleTemporariesAndNoEntry)
        {
            for (const auto& [name, policy] : { std::pair{ "once", sPolicy }, std::pair{ "again", sPolicy } })
            {
                const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / name;
                const auto now = std::filesystem::file_time_type::clock::now();
                std::filesystem::path oldest;
                std::filesystem::path middle;
                std::filesystem::path newest;
                {
                    Store store(folder, sPolicy);
                    store.write(Filed{ .mKey = sKey,
                        .mKind = sFact,
                        .mVersion = 3,
                        .mPayload = payloadOf(sLarge, 1),
                        .mSegmentEnds = {} });
                    store.write(Filed{ .mKey = sOther,
                        .mKind = sFact,
                        .mVersion = 3,
                        .mPayload = payloadOf(sLarge, 2),
                        .mSegmentEnds = {} });
                    store.write(Filed{ .mKey = sThird,
                        .mKind = sFact,
                        .mVersion = 3,
                        .mPayload = payloadOf(sLarge, 3),
                        .mSegmentEnds = {} });
                    store.flush();
                    oldest = store.pathOf(sKey, sFact);
                    middle = store.pathOf(sOther, sFact);
                    newest = store.pathOf(sThird, sFact);
                }
                std::filesystem::last_write_time(oldest, now - std::chrono::hours(3));
                std::filesystem::last_write_time(middle, now - std::chrono::hours(2));
                std::filesystem::last_write_time(newest, now - std::chrono::hours(1));

                const std::filesystem::path stale
                    = folder / "v3" / "0" / "01" / "0123456789abcdeffedcba9876543210.9-1.partial";
                const std::filesystem::path live
                    = folder / "v3" / "0" / "01" / "0123456789abcdeffedcba9876543210.9-2.partial";
                writeAll(stale, { 'x' });
                writeAll(live, { 'x' });
                std::filesystem::last_write_time(stale, now - std::chrono::hours(48));

                Store swept(folder, policy);
                swept.flush();
                EXPECT_TRUE(std::filesystem::exists(oldest)) << name;
                EXPECT_TRUE(std::filesystem::exists(middle)) << name;
                EXPECT_TRUE(std::filesystem::exists(newest)) << name;
                EXPECT_FALSE(std::filesystem::exists(stale)) << name;
                EXPECT_TRUE(std::filesystem::exists(live)) << name;
                EXPECT_EQ(swept.getStats().mStale, 1u) << name;
            }
        }

        /// **An entry of a version its owner makes no more is swept, once after the versions change**: a fact of
        /// version 3, a mean of version 1 and a small fact of version 3, each written by a store that states no
        /// versions. A store stating 3, 2 and 2 removes the mean alone; one stating the same again, after the mean is
        /// written once more, reads no trailer and keeps it; one stating 4 removes all three facts and the mean.
        TEST(ContentStoreTest, anEntryOfAVersionItsOwnerMakesNoMoreIsSweptOnceTheVersionsChange)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
            const auto writeOne
                = [&](const Key& key, const std::uint32_t kind, const std::uint32_t version, const std::size_t bytes) {
                      Store store(folder, sPolicy);
                      store.write(Filed{ .mKey = key,
                          .mKind = kind,
                          .mVersion = version,
                          .mPayload = payloadOf(bytes, 1),
                          .mSegmentEnds = {} });
                      store.flush();
                      return store.pathOf(key, kind);
                  };
            const std::filesystem::path fact = writeOne(sKey, sFact, 3, sLarge);
            const std::filesystem::path mean = writeOne(sOther, sMean, 1, sLarge);
            const std::filesystem::path small = writeOne(sThird, sFact, 3, 16);

            constexpr std::array<std::uint32_t, 3> sNow{ 3, 2, 2 };
            {
                Store swept(folder, Policy{ .mKindCount = 3, .mVersions = sNow });
                swept.flush();
                EXPECT_EQ(swept.getStats().mOutdated, 1u);
                EXPECT_TRUE(std::filesystem::exists(fact));
                EXPECT_FALSE(std::filesystem::exists(mean));
                EXPECT_TRUE(std::filesystem::exists(small));
                Misc::ByteBuffer read;
                EXPECT_TRUE(swept.read(sThird, sFact, 3, read)) << "an entry kept reads";
            }

            EXPECT_EQ(writeOne(sOther, sMean, 1, sLarge), mean);
            {
                Store again(folder, Policy{ .mKindCount = 3, .mVersions = sNow });
                again.flush();
                EXPECT_EQ(again.getStats().mOutdated, 0u) << "the versions swept already";
                EXPECT_TRUE(std::filesystem::exists(mean));
            }

            constexpr std::array<std::uint32_t, 3> sNext{ 4, 2, 2 };
            Store next(folder, Policy{ .mKindCount = 3, .mVersions = sNext });
            next.flush();
            EXPECT_EQ(next.getStats().mOutdated, 3u);
            EXPECT_FALSE(std::filesystem::exists(fact));
            EXPECT_FALSE(std::filesystem::exists(mean));
            EXPECT_FALSE(std::filesystem::exists(small));
        }

        /// **A write of a key whose file stands writes nothing**: a key is its entry's whole input, so the file holds
        /// the same bytes in a real store and another here, to show it was not written; its time stays.
        TEST(ContentStoreTest, aWriteOfAKeyWhoseFileStandsWritesNothing)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            const Misc::ByteBuffer first = payloadOf(16, 1);
            {
                Store store(folder, sPolicy);
                store.write(
                    Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = first, .mSegmentEnds = {} });
            }

            Store again(folder, sPolicy);
            again.flush();
            const std::filesystem::path path = again.pathOf(sKey, sFact);
            std::filesystem::last_write_time(path, now - std::chrono::hours(48));
            again.write(
                Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payloadOf(16, 2), .mSegmentEnds = {} });
            again.flush();

            EXPECT_EQ(again.getStats().mWritten, 0u);
            EXPECT_LT(std::filesystem::last_write_time(path), now - std::chrono::hours(47)) << "the file was touched";
            const std::vector<char> file = readAll(path);
            ASSERT_EQ(file.size(), first.size() + sTailBytes);
            EXPECT_EQ(std::memcmp(file.data(), first.data(), first.size()), 0) << "the file was written again";
        }

        /// **A discarded entry is gone from its folder once the store's thread has refused it**, and counted refused.
        TEST(ContentStoreTest, aDiscardedEntryIsGoneFromItsFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            Store store(folder, sPolicy);
            store.write(
                Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payloadOf(16, 1), .mSegmentEnds = {} });
            store.flush();
            ASSERT_TRUE(std::filesystem::exists(store.pathOf(sKey, sFact)));

            store.discard(sKey, sFact);
            store.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(std::filesystem::exists(store.pathOf(sKey, sFact)));
            EXPECT_FALSE(store.read(sKey, sFact, 3, read));
            EXPECT_EQ(store.getStats().mRefused, 1u);
        }

        /// **An empty queue admits any write, and a writer that reaches the disk never drops one**: one
        /// payload past the queue's bound, the queue empty, is written; and two that together pass it,
        /// written from a reach to the disk, are both written, the second waiting for room where the
        /// first still stood in the queue.
        TEST(ContentStoreTest, anEmptyQueueAdmitsAnyWriteAndADiskWriterNeverDrops)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            Store store(folder, sSmallQueue);
            store.write(Filed{ .mKey = sKey,
                .mKind = sFact,
                .mVersion = 3,
                .mPayload = payloadOf(sSmallQueue.mQueuedBytesMost + 1, 0),
                .mSegmentEnds = {} });
            store.flush();
            EXPECT_EQ(store.getStats().mWritten, 1u);
            EXPECT_EQ(store.getStats().mDropped, 0u);
            EXPECT_TRUE(std::filesystem::exists(store.pathOf(sKey, sFact)));

            const std::size_t half = sSmallQueue.mQueuedBytesMost / 2 + 1;
            store.write(Filed{
                .mKey = sOther, .mKind = sFact, .mVersion = 3, .mPayload = payloadOf(half, 1), .mSegmentEnds = {} });
            const Key third{ .mHash = { 3, 4 } };
            store.write(Filed{
                .mKey = third, .mKind = sFact, .mVersion = 3, .mPayload = payloadOf(half, 2), .mSegmentEnds = {} });
            store.flush();
            const Stats stats = store.getStats();
            EXPECT_EQ(stats.mWritten, 3u);
            EXPECT_EQ(stats.mDropped, 0u);
            EXPECT_TRUE(std::filesystem::exists(store.pathOf(third, sFact)));
        }

        /// **Two writers on one store drop nothing that reaches the disk.** Each round starts with the queue empty
        /// and the store's thread idle, and the two write at once an entry of more than half the queue's bound,
        /// so the second waits for room behind the first. A write queued and announced to the store's thread in
        /// two lock holds let the second writer read a queue that held work beside a thread that said it had
        /// none, and drop its entry as one no thread would make room for. 2 × 256 writes, every one written.
        TEST(ContentStoreTest, twoWritersOnASmallQueueDropNothingThatReachesTheDisk)
        {
            constexpr std::uint64_t sRounds = 256;
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            Store store(folder, sSmallQueue);
            const Misc::ByteBuffer payload = payloadOf(sSmallQueue.mQueuedBytesMost / 2 + 1, 0);
            std::barrier rounds(2);
            const auto writeAll = [&](const std::uint64_t writer) {
                for (std::uint64_t round = 0; round < sRounds; ++round)
                {
                    rounds.arrive_and_wait();
                    store.write(Filed{ .mKey = Key{ .mHash = { writer, round } },
                        .mKind = sFact,
                        .mVersion = 3,
                        .mPayload = payload,
                        .mSegmentEnds = {} });
                    rounds.arrive_and_wait();
                    if (writer == 0)
                        store.flush();
                }
            };
            {
                const std::jthread other(writeAll, 1);
                writeAll(0);
            }
            store.flush();
            const Stats stats = store.getStats();
            EXPECT_EQ(stats.mWritten, 2 * sRounds);
            EXPECT_EQ(stats.mDropped, 0u);
        }

        /// **Two stores writing one key leave one whole entry**, as a game and a tool may: each
        /// writes whole (`Platform::writeWhole`), through a temporary of its own.
        TEST(ContentStoreTest, twoStoresWritingOneKeyLeaveOneWholeEntry)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const Misc::ByteBuffer payload = payloadOf(sLarge, 9);
            {
                Store first(folder, sPolicy);
                Store second(folder, sPolicy);
                first.write(
                    Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payload, .mSegmentEnds = {} });
                second.write(
                    Filed{ .mKey = sKey, .mKind = sFact, .mVersion = 3, .mPayload = payload, .mSegmentEnds = {} });
            }
            EXPECT_EQ(countFiles(folder, ".partial"), 0u);

            Store reader(folder, sPolicy);
            Misc::ByteBuffer read;
            ASSERT_TRUE(reader.read(sKey, sFact, 3, read));
            EXPECT_EQ(read, payload);
        }

        /// **A file of a kind its owner does not have is refused**, as a fault: a kind is the owner's, and a store
        /// opened with three kinds wrote what a store of two finds where it looks, its file moved into the first
        /// kind's folder. Left in its own kind's folder, the store of two never opens it.
        TEST(ContentStoreTest, aKindItsOwnerDoesNotHaveIsRefused)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            {
                Store store(folder, sPolicy);
                store.write(Filed{
                    .mKey = sKey, .mKind = sShape, .mVersion = 1, .mPayload = payloadOf(16, 1), .mSegmentEnds = {} });
            }

            constexpr Policy twoKinds{ .mKindCount = 2 };
            {
                Store untouched(folder, twoKinds);
                untouched.flush();
                Misc::ByteBuffer read;
                EXPECT_FALSE(untouched.read(sKey, sFact, 1, read));
                EXPECT_TRUE(std::filesystem::exists(untouched.pathOf(sKey, sShape))) << "a folder it never opens";
                EXPECT_EQ(untouched.getStats().mRefused, 0u);
                std::filesystem::create_directories(untouched.pathOf(sKey, sFact).parent_path());
                std::filesystem::rename(untouched.pathOf(sKey, sShape), untouched.pathOf(sKey, sFact));
            }

            Store fewer(folder, twoKinds);
            fewer.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(fewer.read(sKey, sFact, 1, read));
            EXPECT_FALSE(std::filesystem::exists(fewer.pathOf(sKey, sFact))) << "the file stands";
            EXPECT_EQ(fewer.getStats().mRefused, 1u) << "refused at its start's load, or at the read";
        }

        /// **An entry's segments read alone, each checked against its digest** (`Store::open`): a 6000-byte payload
        /// filed in three segments ending at 1000, 5000 and 6000 opens with its table and no payload read; its middle
        /// segment reads alone as the payload's bytes 1000 to 5000, and its last two together. One byte of the middle
        /// segment changed on disk fails that read, refuses and deletes the file. An entry the store holds in memory
        /// opens as one segment, its whole payload; asked at another version, its file is a fault, as a read finds it.
        TEST(ContentStoreTest, anEntrysSegmentsReadAloneEachCheckedAgainstItsDigest)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const Misc::ByteBuffer payload = payloadOf(6000, 3);
            Store store(folder, sPolicy);
            store.write(Filed{ .mKey = sKey,
                .mKind = sFact,
                .mVersion = 3,
                .mPayload = payload,
                .mSegmentEnds = { 1000, 5000, 6000 } });
            store.flush();

            std::optional<Opened> opened = store.open(sKey, sFact, 3);
            ASSERT_TRUE(opened.has_value());
            ASSERT_EQ(opened->getSegmentCount(), 3u);
            EXPECT_EQ(opened->startOf(1), 1000u);
            EXPECT_EQ(opened->endOf(1), 5000u);

            Misc::ByteBuffer middle(4000);
            ASSERT_TRUE(store.readSegments(*opened, 1, 1, middle));
            EXPECT_EQ(middle, Misc::ByteBuffer(payload.begin() + 1000, payload.begin() + 5000));
            Misc::ByteBuffer tail(5000);
            ASSERT_TRUE(store.readSegments(*opened, 1, 2, tail));
            EXPECT_EQ(tail, Misc::ByteBuffer(payload.begin() + 1000, payload.end()));

            const std::filesystem::path path = store.pathOf(sKey, sFact);
            std::vector<char> bytes = readAll(path);
            bytes[2000] ^= 1;
            writeAll(path, bytes);
            std::optional<Opened> spoiled = store.open(sKey, sFact, 3);
            ASSERT_TRUE(spoiled.has_value()) << "its table holds";
            Misc::ByteBuffer first(1000);
            EXPECT_TRUE(store.readSegments(*spoiled, 0, 1, first)) << "the first segment is whole";
            EXPECT_FALSE(store.readSegments(*spoiled, 1, 1, middle));
            EXPECT_FALSE(std::filesystem::exists(path));
            EXPECT_EQ(store.getStats().mRefused, 1u);

            const Misc::ByteBuffer small = payloadOf(16, 4);
            store.write(Filed{ .mKey = sOther, .mKind = sFact, .mVersion = 3, .mPayload = small, .mSegmentEnds = {} });
            store.flush();
            std::optional<Opened> held = store.open(sOther, sFact, 3);
            ASSERT_TRUE(held.has_value()) << "one segment where the entry states none";
            ASSERT_EQ(held->getSegmentCount(), 1u);
            Misc::ByteBuffer read(16);
            ASSERT_TRUE(store.readSegments(*held, 0, 1, read));
            EXPECT_EQ(read, small);

            store.write(
                Filed{ .mKey = sThird, .mKind = sFact, .mVersion = 3, .mPayload = payload, .mSegmentEnds = {} });
            store.flush();
            EXPECT_FALSE(store.open(sThird, sFact, 4).has_value()) << "another version";
            EXPECT_FALSE(std::filesystem::exists(store.pathOf(sThird, sFact)));
        }

        /// **The file format, byte for byte**: a payload of 8 bytes in two segments of 3 and 5, each segment's
        /// record, its end and its digest, then the trailer: the magic "RTXC" read as a little-endian word, format 3,
        /// the kind, the version, the key, the table's digest, the payload's length, the count of segments and a word
        /// of nought, 8 + 2 × 24 + 64 = 120 bytes, in the folder of its kind. A file a store wrote before is read by
        /// every store after, whichever owner it moved to; one segment's byte changed is refused.
        TEST(ContentStoreTest, anEntryInTheFormatIsReadAsItWasWritten)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::array<std::byte, 8> payload{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 },
                std::byte{ 5 }, std::byte{ 6 }, std::byte{ 7 }, std::byte{ 8 } };
            const auto digestOf = [](std::span<const std::byte> bytes) {
                Misc::HashState digest;
                digest.add(bytes);
                return digest.getWords();
            };

            struct Record
            {
                std::uint64_t mEnd;
                Misc::DigestWords mDigest;
            };
            static_assert(sizeof(Record) == sRecordBytes);
            const std::array<Record, 2> table{ Record{ .mEnd = 3, .mDigest = digestOf(std::span(payload).first(3)) },
                Record{ .mEnd = 8, .mDigest = digestOf(std::span(payload).subspan(3)) } };

            struct Trailer
            {
                std::uint32_t mMagic;
                std::uint32_t mFormat;
                std::uint32_t mKind;
                std::uint32_t mVersion;
                Misc::DigestWords mKey;
                Misc::DigestWords mTableDigest;
                std::uint64_t mPayloadBytes;
                std::uint32_t mSegments;
                std::uint32_t mReserved;
            };
            static_assert(sizeof(Trailer) == sTrailerBytes);
            const Trailer trailer{ .mMagic = 0x43585452u,
                .mFormat = 3,
                .mKind = sMean,
                .mVersion = 7,
                .mKey = sKey.mHash,
                .mTableDigest = digestOf(std::as_bytes(std::span(table))),
                .mPayloadBytes = payload.size(),
                .mSegments = 2,
                .mReserved = 0 };

            std::vector<char> bytes(payload.size() + sizeof(table) + sizeof(trailer));
            std::memcpy(bytes.data(), payload.data(), payload.size());
            std::memcpy(bytes.data() + payload.size(), table.data(), sizeof(table));
            std::memcpy(bytes.data() + payload.size() + sizeof(table), &trailer, sizeof(trailer));
            ASSERT_EQ(bytes.size(), 120u);

            {
                Store store(folder, sPolicy);
                store.flush();
                EXPECT_EQ(store.pathOf(sKey, sMean), folder / "v3" / "1" / "01" / "0123456789abcdeffedcba9876543210");
                std::filesystem::create_directories(store.pathOf(sKey, sMean).parent_path());
                writeAll(store.pathOf(sKey, sMean), bytes);

                Misc::ByteBuffer read;
                ASSERT_TRUE(store.read(sKey, sMean, 7, read));
                EXPECT_EQ(read, Misc::ByteBuffer(payload.begin(), payload.end()));

                // And what a store writes for the same entry in two segments is this file, byte for byte.
                store.discard(sKey, sMean);
                store.flush();
                store.write(Filed{ .mKey = sKey,
                    .mKind = sMean,
                    .mVersion = 7,
                    .mPayload = Misc::ByteBuffer(payload.begin(), payload.end()),
                    .mSegmentEnds = { 3, 8 } });
                store.flush();
                EXPECT_EQ(readAll(store.pathOf(sKey, sMean)), bytes);
            }

            // Spoiled before the next store starts, which would hold the small entry in memory from its start.
            bytes[5] ^= 1;
            writeAll(folder / "v3" / "1" / "01" / "0123456789abcdeffedcba9876543210", bytes);
            Store again(folder, sPolicy);
            again.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(again.read(sKey, sMean, 7, read)) << "the second segment's byte";
            EXPECT_EQ(again.getStats().mRefused, 1u);
        }
    }
}
