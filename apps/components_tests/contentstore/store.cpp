#include <array>
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
#include <system_error>
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
        constexpr Policy sPolicy{ .mKindCount = 3, .mHolding = Holding{ .mAtAnySize = 1u << sShape } };

        constexpr Key sKey{ { 0x0123456789abcdefull, 0xfedcba9876543210ull } };
        constexpr Key sOther{ { 0x1111111111111111ull, 0x2222222222222222ull } };
        constexpr Key sThird{ { 0x3333333333333333ull, 0x4444444444444444ull } };

        /// The bytes an entry's file ends with, its trailer's: four words, two keys' worth and a
        /// length, 4 × 4 + 16 + 16 + 8.
        constexpr std::size_t sTrailerBytes = 56;

        /// Past `Store::sResidentMost`, so the store reads it from its file and not from memory.
        constexpr std::size_t sLarge = 5000;

        Misc::ByteBuffer payloadOf(std::size_t bytes, unsigned seed)
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

        /// One entry written and read back: the file stands where the key names it, as long as its
        /// header and payload, 56 + 5000 = 5056 bytes, in the folder of the key's first byte, `01`, in
        /// the folder of the format, `v2`; and
        /// another store on the same folder reads it whole from its file. The frame's reach, memory
        /// alone, does not find an entry too large to hold there.
        TEST(ContentStoreTest, anEntryWrittenIsReadBackWholeByAnotherStoreOnTheSameFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const Misc::ByteBuffer payload = payloadOf(sLarge, 7);
            {
                Store store(folder, sPolicy);
                store.write(sKey, sFact, 3, payload, Reach::Disk);
                store.flush();
                EXPECT_EQ(store.getStats().mWritten, 1u);
                EXPECT_EQ(store.pathOf(sKey), folder / "v2" / "01" / "0123456789abcdeffedcba9876543210");
                EXPECT_EQ(Store::sFormat, 2u);
            }
            EXPECT_EQ(std::filesystem::file_size(folder / "v2" / "01" / "0123456789abcdeffedcba9876543210"),
                sTrailerBytes + sLarge);
            EXPECT_EQ(countFiles(folder, ".partial"), 0u) << "the temporary was renamed into place";

            Store again(folder, sPolicy);
            again.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(again.read(sKey, sFact, 3, Reach::Memory, read))
                << "too large to be held in memory, so the frame's reach does not find it";
            ASSERT_TRUE(again.read(sKey, sFact, 3, Reach::Disk, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sOther, sFact, 3, Reach::Disk, read)) << "no file";

            const Stats stats = again.getStats();
            EXPECT_EQ(stats.mFound, 1u);
            EXPECT_EQ(stats.mMissed, 2u);
            EXPECT_EQ(stats.mRefused, 0u);
        }

        /// **A small entry is in memory from the start, but one of a kind held at any size**: a store made on
        /// a folder that holds a small texel mean and a small shape loads the mean as it sweeps, so the frame's
        /// reach finds it with its file gone, and leaves the shape to its owner's read, which a reader's reach
        /// then holds in memory.
        TEST(ContentStoreTest, aSmallEntryIsHeldInMemoryFromTheStartButAKindHeldAtAnySize)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const Misc::ByteBuffer payload = payloadOf(16, 3);
            const Misc::ByteBuffer shape = payloadOf(16, 4);
            {
                Store store(folder, sPolicy);
                store.write(sKey, sMean, 1, payload, Reach::Disk);
                store.write(sOther, sShape, 1, shape, Reach::Disk);
            }

            Store again(folder, sPolicy);
            again.flush();
            std::filesystem::remove(again.pathOf(sKey));

            Misc::ByteBuffer read;
            ASSERT_TRUE(again.read(sKey, sMean, 1, Reach::Memory, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sKey, sMean, 2, Reach::Memory, read)) << "held for another version";

            EXPECT_FALSE(again.read(sOther, sShape, 1, Reach::Memory, read)) << "a shape the start did not load";
            ASSERT_TRUE(again.read(sOther, sShape, 1, Reach::Disk, read));
            std::filesystem::remove(again.pathOf(sOther));
            ASSERT_TRUE(again.read(sOther, sShape, 1, Reach::Memory, read)) << "held once its owner read it";
            EXPECT_EQ(read, shape);
        }

        /// **A shape is held in memory at any size, and nothing else past `sResidentMost`**: one of 4097
        /// bytes written is found by the frame's reach, which reads memory alone, and a texel mean of the
        /// same size is not.
        TEST(ContentStoreTest, aShapeIsHeldInMemoryAtAnySize)
        {
            Store store(TestingOpenMW::currentTestDirPath() / "store", sPolicy);
            const Misc::ByteBuffer large = payloadOf(Store::sResidentMost + 1, 5);
            const Key shapeKey{ .mHash = { 11, 12 } };
            const Key meanKey{ .mHash = { 13, 14 } };
            store.write(shapeKey, sShape, 1, large, Reach::Disk);
            store.write(meanKey, sMean, 1, large, Reach::Disk);

            Misc::ByteBuffer read;
            ASSERT_TRUE(store.read(shapeKey, sShape, 1, Reach::Memory, read));
            EXPECT_EQ(read, large);
            EXPECT_FALSE(store.read(meanKey, sMean, 1, Reach::Memory, read));
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
                { "its magic", [](std::vector<char>& bytes) { bytes[sLarge + 0] ^= 1; } },
                { "its format", [](std::vector<char>& bytes) { bytes[sLarge + 4] ^= 1; } },
                { "a kind past the last", [](std::vector<char>& bytes) { bytes[sLarge + 8] = 99; } },
                { "another key", [](std::vector<char>& bytes) { bytes[sLarge + 16] ^= 1; } },
                { "another digest", [](std::vector<char>& bytes) { bytes[sLarge + 32] ^= 1; }, true },
                { "its payload", [](std::vector<char>& bytes) { bytes[100] ^= 1; }, true },
                { "a length past the file", [](std::vector<char>& bytes) { bytes[sLarge + 48] += 1; } },
                { "a length short of the file", [](std::vector<char>& bytes) { bytes.push_back(0); } },
                { "asked at another version", [](std::vector<char>&) {}, true, 4 },
            };

            for (const Fault& fault : faults)
            {
                const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / fault.mName;
                {
                    Store store(folder, sPolicy);
                    store.write(sKey, sFact, 3, payloadOf(sLarge, 1), Reach::Disk);
                }

                Store again(folder, sPolicy);
                again.flush();
                const std::filesystem::path path = again.pathOf(sKey);
                std::vector<char> bytes = readAll(path);
                fault.mSpoil(bytes);
                writeAll(path, bytes);

                EXPECT_EQ(again.holds(sKey, Reach::Disk), fault.mHeld) << fault.mName;
                EXPECT_EQ(std::filesystem::exists(path), fault.mHeld) << fault.mName;
                EXPECT_EQ(again.getStats().mHeld, fault.mHeld ? 1u : 0u) << fault.mName;
                EXPECT_EQ(again.getStats().mUnheld, fault.mHeld ? 0u : 1u) << fault.mName;

                Misc::ByteBuffer read;
                EXPECT_FALSE(again.read(sKey, sFact, fault.mAskedVersion, Reach::Disk, read)) << fault.mName;
                EXPECT_FALSE(std::filesystem::exists(path)) << fault.mName;
                EXPECT_EQ(again.getStats().mRefused, 1u) << fault.mName;
            }
        }

        /// **The sweep removes stale temporaries and no entry**: the store has no cap, so three entries
        /// last used three, two and one hours ago all stay. A temporary file two days old is one a killed
        /// process left and goes; one written now is a live write and stays.
        TEST(ContentStoreTest, theSweepRemovesStaleTemporariesAndNoEntry)
        {
            // A store that holds nothing in memory lists no entry at its start, and sweeps all the same.
            constexpr Policy holdsNothing{ .mKindCount = 3, .mHolding = std::nullopt };
            for (const auto& [name, policy] :
                { std::pair{ "holds entries", sPolicy }, std::pair{ "holds nothing", holdsNothing } })
            {
                const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / name;
                const auto now = std::filesystem::file_time_type::clock::now();
                std::filesystem::path oldest;
                std::filesystem::path middle;
                std::filesystem::path newest;
                {
                    Store store(folder, sPolicy);
                    store.write(sKey, sFact, 3, payloadOf(sLarge, 1), Reach::Disk);
                    store.write(sOther, sFact, 3, payloadOf(sLarge, 2), Reach::Disk);
                    store.write(sThird, sFact, 3, payloadOf(sLarge, 3), Reach::Disk);
                    store.flush();
                    oldest = store.pathOf(sKey);
                    middle = store.pathOf(sOther);
                    newest = store.pathOf(sThird);
                }
                std::filesystem::last_write_time(oldest, now - std::chrono::hours(3));
                std::filesystem::last_write_time(middle, now - std::chrono::hours(2));
                std::filesystem::last_write_time(newest, now - std::chrono::hours(1));

                const std::filesystem::path stale
                    = folder / "v2" / "01" / "0123456789abcdeffedcba9876543210.9-1.partial";
                const std::filesystem::path live
                    = folder / "v2" / "01" / "0123456789abcdeffedcba9876543210.9-2.partial";
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

        /// **A hit stamps its file's time once a session**, so the next start's sweep counts it as
        /// used now: a file two days old reads as used within the minute after a hit, and a second
        /// hit stamps nothing more.
        TEST(ContentStoreTest, aHitStampsItsFileOnceASession)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            {
                Store store(folder, sPolicy);
                store.write(sKey, sFact, 3, payloadOf(sLarge, 1), Reach::Disk);
            }

            Store again(folder, sPolicy);
            again.flush();
            const std::filesystem::path path = again.pathOf(sKey);
            std::filesystem::last_write_time(path, now - std::chrono::hours(48));

            Misc::ByteBuffer read;
            ASSERT_TRUE(again.read(sKey, sFact, 3, Reach::Disk, read));
            again.flush();
            EXPECT_GT(std::filesystem::last_write_time(path), now - std::chrono::minutes(1));
            EXPECT_EQ(again.getStats().mStamped, 1u);

            ASSERT_TRUE(again.read(sKey, sFact, 3, Reach::Disk, read));
            again.flush();
            EXPECT_EQ(again.getStats().mStamped, 1u) << "once a session";
        }

        /// **A write of a key whose file stands stamps it and writes nothing**, as a frame thread that
        /// cannot reach the file offers its output again in a later session: a two-day-old file is
        /// used within the minute and keeps its payload, which is the same bytes in a real store and
        /// another here, to show it was not written.
        TEST(ContentStoreTest, aWriteOfAKeyWhoseFileStandsStampsItAndWritesNothing)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const auto now = std::filesystem::file_time_type::clock::now();
            const Misc::ByteBuffer first = payloadOf(sLarge, 1);
            {
                Store store(folder, sPolicy);
                store.write(sKey, sFact, 3, first, Reach::Disk);
            }

            Store again(folder, sPolicy);
            again.flush();
            const std::filesystem::path path = again.pathOf(sKey);
            std::filesystem::last_write_time(path, now - std::chrono::hours(48));
            again.write(sKey, sFact, 3, payloadOf(sLarge, 2), Reach::Disk);
            again.flush();

            EXPECT_EQ(again.getStats().mWritten, 0u);
            EXPECT_EQ(again.getStats().mStamped, 1u);
            EXPECT_GT(std::filesystem::last_write_time(path), now - std::chrono::minutes(1));
            Misc::ByteBuffer read;
            ASSERT_TRUE(again.read(sKey, sFact, 3, Reach::Disk, read));
            EXPECT_EQ(read, first);
        }

        /// **A discarded entry is gone from memory and from its folder**, and counted refused: a small
        /// entry held from the start is found by neither reach after it.
        TEST(ContentStoreTest, aDiscardedEntryIsGoneFromMemoryAndFromItsFolder)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            Store store(folder, sPolicy);
            store.write(sKey, sFact, 3, payloadOf(16, 1), Reach::Disk);
            store.flush();
            ASSERT_TRUE(std::filesystem::exists(store.pathOf(sKey)));

            store.discard(sKey);
            Misc::ByteBuffer read;
            EXPECT_FALSE(store.read(sKey, sFact, 3, Reach::Memory, read)) << "held still";
            store.flush();
            EXPECT_FALSE(std::filesystem::exists(store.pathOf(sKey)));
            EXPECT_FALSE(store.read(sKey, sFact, 3, Reach::Disk, read));
            EXPECT_EQ(store.getStats().mRefused, 1u);
        }

        /// **An empty queue admits any write, and a writer that reaches the disk never drops one**: one
        /// payload past the queue's bound, the queue empty, is written; and two that together pass it,
        /// written from a reach to the disk, are both written, the second waiting for room where the
        /// first still stood in the queue.
        TEST(ContentStoreTest, anEmptyQueueAdmitsAnyWriteAndADiskWriterNeverDrops)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            Store store(folder, sPolicy);
            store.write(sKey, sFact, 3, payloadOf(Store::sQueuedMost + 1, 0), Reach::Memory);
            store.flush();
            EXPECT_EQ(store.getStats().mWritten, 1u);
            EXPECT_EQ(store.getStats().mDropped, 0u);
            EXPECT_TRUE(std::filesystem::exists(store.pathOf(sKey)));

            const std::size_t half = Store::sQueuedMost / 2 + 1;
            store.write(sOther, sFact, 3, payloadOf(half, 1), Reach::Disk);
            const Key third{ .mHash = { 3, 4 } };
            store.write(third, sFact, 3, payloadOf(half, 2), Reach::Disk);
            store.flush();
            const Stats stats = store.getStats();
            EXPECT_EQ(stats.mWritten, 3u);
            EXPECT_EQ(stats.mDropped, 0u);
            EXPECT_TRUE(std::filesystem::exists(store.pathOf(third)));
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
                first.write(sKey, sFact, 3, payload, Reach::Disk);
                second.write(sKey, sFact, 3, payload, Reach::Disk);
            }
            EXPECT_EQ(countFiles(folder, ".partial"), 0u);

            Store reader(folder, sPolicy);
            Misc::ByteBuffer read;
            ASSERT_TRUE(reader.read(sKey, sFact, 3, Reach::Disk, read));
            EXPECT_EQ(read, payload);
        }

        /// **A file of a kind its owner does not have is refused**, as a fault: a kind is the owner's, and
        /// a store opened with three kinds wrote what a store of two reads at the same key.
        TEST(ContentStoreTest, aKindItsOwnerDoesNotHaveIsRefused)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            {
                Store store(folder, sPolicy);
                store.write(sKey, sShape, 1, payloadOf(16, 1), Reach::Disk);
            }

            constexpr Policy twoKinds{ .mKindCount = 2, .mHolding = Holding{} };
            Store fewer(folder, twoKinds);
            fewer.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(fewer.read(sKey, sFact, 1, Reach::Disk, read));
            EXPECT_FALSE(std::filesystem::exists(fewer.pathOf(sKey))) << "the file stands";
            EXPECT_EQ(fewer.getStats().mRefused, 1u) << "refused at its start's load, or at the read";
        }

        /// **A store that holds nothing in memory loads nothing, holds nothing and stamps nothing**, the
        /// navigator's tiles' (no `Holding`): an entry of 16 bytes, which a store that holds entries
        /// loads at its start, is found on the disk alone, and a hit sets no file's time.
        TEST(ContentStoreTest, aStoreThatHoldsNothingLoadsHoldsAndStampsNothing)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            constexpr Policy none{ .mKindCount = 3, .mHolding = std::nullopt };
            const Misc::ByteBuffer payload = payloadOf(16, 1);
            {
                Store store(folder, none);
                store.write(sKey, sFact, 1, payload, Reach::Disk);
                Misc::ByteBuffer read;
                EXPECT_FALSE(store.read(sKey, sFact, 1, Reach::Memory, read)) << "held once written";
            }

            Store again(folder, none);
            again.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(again.read(sKey, sFact, 1, Reach::Memory, read)) << "loaded at the start";
            ASSERT_TRUE(again.read(sKey, sFact, 1, Reach::Disk, read));
            EXPECT_EQ(read, payload);
            EXPECT_FALSE(again.read(sKey, sFact, 1, Reach::Memory, read)) << "held once read";
            again.flush();
            EXPECT_EQ(again.getStats().mStamped, 0u);
        }

        /// **The file format, byte for byte**: a payload of 8 bytes and the trailer after it, the magic
        /// "RTXC" read as a little-endian word, format 2, the kind, the version, the key, the payload's
        /// digest and its length, 8 + 56 = 64 bytes. A file a store wrote before is read by every store
        /// after, whichever owner it moved to.
        TEST(ContentStoreTest, anEntryInTheFormatIsReadAsItWasWritten)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const std::array<std::byte, 8> payload{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 },
                std::byte{ 5 }, std::byte{ 6 }, std::byte{ 7 }, std::byte{ 8 } };
            Misc::HashState digest;
            digest.add(std::span<const std::byte>(payload));

            struct Trailer
            {
                std::uint32_t mMagic;
                std::uint32_t mFormat;
                std::uint32_t mKind;
                std::uint32_t mVersion;
                Misc::DigestWords mKey;
                Misc::DigestWords mDigest;
                std::uint64_t mPayloadBytes;
            };
            static_assert(sizeof(Trailer) == sTrailerBytes);
            const Trailer trailer{ .mMagic = 0x43585452u,
                .mFormat = 2,
                .mKind = sMean,
                .mVersion = 7,
                .mKey = sKey.mHash,
                .mDigest = digest.getWords(),
                .mPayloadBytes = payload.size() };

            Store store(folder, sPolicy);
            store.flush();
            std::vector<char> bytes(payload.size() + sizeof(trailer));
            std::memcpy(bytes.data(), payload.data(), payload.size());
            std::memcpy(bytes.data() + payload.size(), &trailer, sizeof(trailer));
            std::filesystem::create_directories(store.pathOf(sKey).parent_path());
            writeAll(store.pathOf(sKey), bytes);

            Misc::ByteBuffer read;
            ASSERT_TRUE(store.read(sKey, sMean, 7, Reach::Disk, read));
            EXPECT_EQ(read, Misc::ByteBuffer(payload.begin(), payload.end()));
        }
    }
}
