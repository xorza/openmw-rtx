#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/platform/wholefile.hpp>
#include <components/rtx/prepare/filestamps.hpp>
#include <components/rtx/prepare/preparationmanifest.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t sSignature = 0x1234;

        std::size_t countOf(std::string_view text, std::string_view part)
        {
            std::size_t count = 0;
            for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + 1))
                ++count;
            return count;
        }

        constexpr StoredKey sShared{ .mKey = { { 0x1111222233334444ull, 0x5555666677778888ull } },
            .mPass = ContentPassId::AlphaSpan };
        constexpr StoredKey sOwn{ .mKey = { { 0x9999aaaabbbbccccull, 0xddddeeeeffff0000ull } },
            .mPass = ContentPassId::AlphaSpan };

        /// **What is written is read back under the same signature, and nothing under another, each name
        /// and each key once**: two models' digests, files and keys come back as they went, and the
        /// texture and the key both name stand once in the file; a manifest signed by another preparation
        /// reads as empty, as one that is not there does.
        TEST(RtxPreparationManifestTest, aManifestIsReadBackUnderItsSignatureAloneEachNameOnce)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "prepared.manifest";
            PreparationManifest written;
            written.keep("meshes/a.nif",
                PreparationManifest::Entry{
                    .mDigest = 7, .mFiles = { "meshes/a.nif", "textures/shared.dds" }, .mKeys = { sShared } },
                3);
            written.keep("meshes/b.nif",
                PreparationManifest::Entry{
                    .mDigest = 9, .mFiles = { "meshes/b.nif", "textures/shared.dds" }, .mKeys = { sShared, sOwn } },
                3);
            EXPECT_TRUE(written.isChanged());
            ASSERT_TRUE(written.write(path, sSignature));

            const PreparationManifest read = PreparationManifest::read(path, sSignature);
            ASSERT_EQ(read.size(), 2u);
            EXPECT_FALSE(read.isChanged());
            const std::vector<PreparationManifest::Entry> b = read.entriesOf("meshes/b.nif");
            ASSERT_EQ(b.size(), 1u);
            EXPECT_EQ(b[0].mDigest, 9u);
            EXPECT_EQ(b[0].mFiles, (std::vector<std::string>{ "meshes/b.nif", "textures/shared.dds" }));
            EXPECT_EQ(b[0].mKeys, (std::vector<StoredKey>{ sShared, sOwn }));
            ASSERT_EQ(read.entriesOf("meshes/a.nif").size(), 1u);
            EXPECT_EQ(read.entriesOf("meshes/a.nif")[0].mDigest, 7u);
            EXPECT_EQ(read.entriesOf("meshes/a.nif")[0].mKeys, std::vector<StoredKey>{ sShared });
            EXPECT_TRUE(read.entriesOf("meshes/c.nif").empty());

            std::string bytes;
            ASSERT_TRUE(Platform::readWhole(path, bytes, Platform::sUnbounded).isOk());
            EXPECT_EQ(countOf(bytes, "textures/shared.dds"), 1u);
            EXPECT_EQ(
                countOf(bytes, std::string_view(reinterpret_cast<const char*>(&sShared.mKey), sizeof(sShared.mKey))),
                1u);

            EXPECT_EQ(PreparationManifest::read(path, sSignature + 1).size(), 0u);
            EXPECT_EQ(PreparationManifest::read(path.parent_path() / "missing.manifest", sSignature).size(), 0u);
        }

        /// **Each path's stamp is asked of the source once**: three asks of one path and one of another
        /// ask the source twice, and each answer is the source's, the path's length plus forty.
        TEST(RtxKeptStampsTest, eachPathIsStampedOnce)
        {
            struct Counted final : FileStamps
            {
                std::uint64_t stampOf(VFS::Path::NormalizedView path) override
                {
                    ++mAsked;
                    return path.value().size() + 40;
                }
                std::size_t mAsked = 0;
            };
            Counted source;
            KeptStamps kept(source);
            EXPECT_EQ(kept.stampOf(VFS::Path::NormalizedView("textures/a.dds")), 54u);
            EXPECT_EQ(kept.stampOf(VFS::Path::NormalizedView("textures/a.dds")), 54u);
            EXPECT_EQ(kept.stampOf(VFS::Path::NormalizedView("meshes/b.nif")), 52u);
            EXPECT_EQ(kept.stampOf(VFS::Path::NormalizedView("textures/a.dds")), 54u);
            EXPECT_EQ(source.mAsked, 2u);
        }

        /// **A file that claims more than it holds reads as empty**: a count of names, or of keys, of four
        /// billion in a file of a few dozen bytes, which is never an allocation of that many, and an entry
        /// whose file is an index past the names or whose key is one past the keys.
        TEST(RtxPreparationManifestTest, aFileThatClaimsMoreThanItHoldsReadsAsEmpty)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "prepared.manifest";
            PreparationManifest written;
            written.keep("m", PreparationManifest::Entry{ .mDigest = 1, .mFiles = { "f" }, .mKeys = { sOwn } }, 0);
            ASSERT_TRUE(written.write(path, sSignature));

            std::string bytes;
            ASSERT_TRUE(Platform::readWhole(path, bytes, Platform::sUnbounded).isOk());
            const auto patched = [&](std::size_t at, std::uint32_t value) {
                std::string copy = bytes;
                std::memcpy(copy.data() + at, &value, sizeof(value));
                EXPECT_TRUE(Platform::writeWhole(path, std::string_view(copy), Platform::Durability::Whole).isOk());
                return PreparationManifest::read(path, sSignature).size();
            };

            // Magic, format and signature, then the count of names, the two names of a length and one
            // character each, and the count of keys; the file ends with the entry's one file, an index, its
            // count of keys, and its one key, an index.
            const std::size_t namesAt = 4 + 4 + 8;
            const std::size_t keysAt = namesAt + 4 + 2 * (2 + 1);
            std::uint32_t names = 0;
            std::uint32_t keys = 0;
            std::memcpy(&names, bytes.data() + namesAt, sizeof(names));
            std::memcpy(&keys, bytes.data() + keysAt, sizeof(keys));
            ASSERT_EQ(names, 2u) << "the layout this test reads is the one written";
            ASSERT_EQ(keys, 1u) << "the layout this test reads is the one written";
            EXPECT_EQ(patched(namesAt, 0xffffffffu), 0u);
            EXPECT_EQ(patched(keysAt, 0xffffffffu), 0u);
            EXPECT_EQ(patched(bytes.size() - 12, 2u), 0u) << "an index past the two names";
            EXPECT_EQ(patched(bytes.size() - 4, 1u), 0u) << "an index past the one key";
            EXPECT_EQ(patched(bytes.size() - 4, 0u), 1u) << "the file as written";
        }

        /// **A model is kept until no run named it for `sUnnamedDaysMost` days, and a naming is a change
        /// once a day**: named on day 11 where it was filed on day 10, the manifest changed, and named on
        /// day 11 again it did not; dropped on day 42, 31 days on, and kept on day 41.
        TEST(RtxPreparationManifestTest, aModelStandsUntilNoRunNamedItForAMonth)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "prepared.manifest";
            PreparationManifest written;
            written.keep("m", PreparationManifest::Entry{ .mDigest = 1, .mFiles = { "f" } }, 10);
            ASSERT_TRUE(written.write(path, sSignature));

            PreparationManifest read = PreparationManifest::read(path, sSignature);
            read.name("m", 1, 10);
            EXPECT_FALSE(read.isChanged()) << "named on the day it was";
            read.name("m", 2, 11);
            EXPECT_FALSE(read.isChanged()) << "a digest the model has no entry of";
            read.name("m", 1, 11);
            EXPECT_TRUE(read.isChanged());
            ASSERT_TRUE(read.write(path, sSignature));

            PreparationManifest again = PreparationManifest::read(path, sSignature);
            again.name("m", 1, 11);
            EXPECT_FALSE(again.isChanged());
            again.dropUnnamed(11 + PreparationManifest::sUnnamedDaysMost);
            EXPECT_EQ(again.size(), 1u);
            EXPECT_FALSE(again.isChanged());
            again.dropUnnamed(12 + PreparationManifest::sUnnamedDaysMost);
            EXPECT_EQ(again.size(), 0u);
            EXPECT_TRUE(again.isChanged());
        }

        /// **A model stands where its files' stamps match and the store holds every key its walk filed**:
        /// filed with two keys the store holds, it stands; with one of them deleted, as a store refuses a
        /// file, it does not; and a stamp that moved is no match whatever the store holds.
        TEST(RtxPreparationManifestTest, aModelStandsOnlyWhereTheStoreHoldsEveryKeyItFiled)
        {
            struct Fixed final : FileStamps
            {
                std::uint64_t stampOf(VFS::Path::NormalizedView) override { return mStamp; }
                std::uint64_t mStamp = 5;
            };
            Fixed stamps;
            Misc::ByteBuffer scratch;
            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            for (const StoredKey& key : { sShared, sOwn })
                store.write(key.mKey, storeKindOf(ContentPassId::AlphaSpan), 1, Misc::ByteBuffer{ std::byte{ 1 } },
                    ContentStore::Reach::Disk);
            store.flush();

            const std::vector<std::string> files{ "meshes/m.nif" };
            PreparationManifest manifest;
            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{ .mDigest = PreparationManifest::digestOf(files, stamps),
                    .mFiles = files,
                    .mKeys = { sShared, sOwn } },
                0);
            EXPECT_TRUE(manifest.standingDigest("meshes/m.nif", stamps, store, scratch).has_value());

            std::filesystem::remove(store.pathOf(sOwn.mKey));
            ContentStore::Store reopened(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, reopened, scratch).has_value())
                << "a key the store lost";

            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{
                    .mDigest = PreparationManifest::digestOf(files, stamps), .mFiles = files, .mKeys = { sShared } },
                0);
            EXPECT_TRUE(manifest.standingDigest("meshes/m.nif", stamps, reopened, scratch).has_value());
            stamps.mStamp = 6;
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, reopened, scratch).has_value())
                << "a stamp that moved";
        }

        /// **A model walked from two installs stands from either, and holds at most `sEntriesPerModelMost`
        /// entries**: the same files stamped by two installs' archives are two entries, each standing while
        /// its install's stamps are the ones answered, read back as written; a model re-walked from files of a
        /// digest it has replaces that entry; past the bound the least recently named goes, the earlier filed
        /// of two named the same day.
        TEST(RtxPreparationManifestTest, aModelStandsFromEveryInstallItWasWalkedFrom)
        {
            struct Install final : FileStamps
            {
                std::uint64_t stampOf(VFS::Path::NormalizedView) override { return mStamp; }
                std::uint64_t mStamp = 0;
            };
            Install stamps;
            Misc::ByteBuffer scratch;
            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            const std::vector<std::string> files{ "meshes/m.nif", "textures/t.dds" };
            const auto filed = [&](std::uint64_t install) {
                stamps.mStamp = install;
                return PreparationManifest::Entry{ .mDigest = PreparationManifest::digestOf(files, stamps),
                    .mFiles = files };
            };
            const PreparationManifest::Entry vanilla = filed(100);
            const PreparationManifest::Entry repack = filed(200);
            ASSERT_NE(vanilla.mDigest, repack.mDigest);

            PreparationManifest manifest;
            manifest.keep("meshes/m.nif", vanilla, 1);
            manifest.keep("meshes/m.nif", repack, 2);
            stamps.mStamp = 100;
            EXPECT_EQ(manifest.standingDigest("meshes/m.nif", stamps, store, scratch), vanilla.mDigest);
            stamps.mStamp = 200;
            EXPECT_EQ(manifest.standingDigest("meshes/m.nif", stamps, store, scratch), repack.mDigest);
            stamps.mStamp = 300;
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, store, scratch).has_value());

            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "prepared.manifest";
            ASSERT_TRUE(manifest.write(path, sSignature));
            const PreparationManifest read = PreparationManifest::read(path, sSignature);
            EXPECT_EQ(read.size(), 2u);
            const std::vector<PreparationManifest::Entry> both = read.entriesOf("meshes/m.nif");
            ASSERT_EQ(both.size(), 2u);
            EXPECT_EQ(both[0].mDigest, std::min(vanilla.mDigest, repack.mDigest));
            EXPECT_EQ(both[1].mDigest, std::max(vanilla.mDigest, repack.mDigest));

            // Re-walked from the vanilla files, now with a key the store holds: that entry is replaced, not added.
            store.write(sOwn.mKey, storeKindOf(ContentPassId::AlphaSpan), 1, Misc::ByteBuffer{ std::byte{ 1 } },
                ContentStore::Reach::Disk);
            store.flush();
            PreparationManifest::Entry rewalked = vanilla;
            rewalked.mKeys = { sOwn };
            manifest.keep("meshes/m.nif", rewalked, 3);
            EXPECT_EQ(manifest.size(), 2u);
            ASSERT_EQ(manifest.entriesOf("meshes/m.nif").size(), 2u);

            // Installs 300 and 400 fill the bound of four, and 500 passes it: the repack's entry, named on day 2,
            // goes, and the vanilla one, named on day 3, stays.
            static_assert(PreparationManifest::sEntriesPerModelMost == 4);
            manifest.keep("meshes/m.nif", filed(300), 4);
            manifest.keep("meshes/m.nif", filed(400), 4);
            EXPECT_EQ(manifest.size(), 4u);
            manifest.keep("meshes/m.nif", filed(500), 4);
            EXPECT_EQ(manifest.size(), 4u);
            stamps.mStamp = 200;
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, store, scratch).has_value());
            stamps.mStamp = 100;
            EXPECT_EQ(manifest.standingDigest("meshes/m.nif", stamps, store, scratch), vanilla.mDigest);

            // Named on day 4 too, the vanilla entry ties with the three installs' entries, and of the four it was
            // filed earliest, re-walked on day 3: it goes, and the three stay.
            manifest.name("meshes/m.nif", vanilla.mDigest, 4);
            manifest.keep("meshes/m.nif", filed(600), 4);
            EXPECT_EQ(manifest.size(), 4u);
            stamps.mStamp = 100;
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, store, scratch).has_value());
            for (const std::uint64_t standing : { 300, 400, 500, 600 })
            {
                stamps.mStamp = standing;
                EXPECT_TRUE(manifest.standingDigest("meshes/m.nif", stamps, store, scratch).has_value()) << standing;
            }
        }

        /// **A file with more entries for a model than a manifest keeps, or two of one digest, reads as empty**:
        /// no write lays either, and both would hold a model past what `keep` allows.
        TEST(RtxPreparationManifestTest, aFileWithTooManyOrRepeatedEntriesForAModelReadsAsEmpty)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "prepared.manifest";
            PreparationManifest written;
            written.keep("m", PreparationManifest::Entry{ .mDigest = 1, .mFiles = { "f" } }, 0);
            written.keep("m", PreparationManifest::Entry{ .mDigest = 2, .mFiles = { "f" } }, 0);
            ASSERT_TRUE(written.write(path, sSignature));
            ASSERT_EQ(PreparationManifest::read(path, sSignature).size(), 2u) << "the file as written";

            // The entries close the file, each the model's index, its digest, its day, one file and no keys:
            // 4 + 8 + 4 + 4 + 4 + 4 bytes. The second's digest made the first's is two entries of one digest.
            std::string bytes;
            ASSERT_TRUE(Platform::readWhole(path, bytes, Platform::sUnbounded).isOk());
            constexpr std::size_t entryBytes = 4 + 8 + 4 + 4 + 4 + 4;
            std::uint64_t first = 0;
            std::memcpy(&first, bytes.data() + bytes.size() - 2 * entryBytes + 4, sizeof(first));
            std::string repeated = bytes;
            std::memcpy(repeated.data() + repeated.size() - entryBytes + 4, &first, sizeof(first));
            ASSERT_TRUE(Platform::writeWhole(path, std::string_view(repeated), Platform::Durability::Whole).isOk());
            EXPECT_EQ(PreparationManifest::read(path, sSignature).size(), 0u);

            PreparationManifest full;
            for (std::uint64_t digest = 1; digest <= PreparationManifest::sEntriesPerModelMost; ++digest)
                full.keep("m", PreparationManifest::Entry{ .mDigest = digest, .mFiles = { "f" } }, 0);
            ASSERT_TRUE(full.write(path, sSignature));
            ASSERT_TRUE(Platform::readWhole(path, bytes, Platform::sUnbounded).isOk());
            // One entry more, a copy of the last with a digest of its own, and the count of entries one up.
            std::string more = bytes + bytes.substr(bytes.size() - entryBytes);
            const std::uint64_t fifth = 99;
            std::memcpy(more.data() + more.size() - entryBytes + 4, &fifth, sizeof(fifth));
            const std::size_t countAt = bytes.size() - PreparationManifest::sEntriesPerModelMost * entryBytes - 4;
            std::uint32_t count = 0;
            std::memcpy(&count, more.data() + countAt, sizeof(count));
            ASSERT_EQ(count, PreparationManifest::sEntriesPerModelMost)
                << "the layout this test reads is the one written";
            ++count;
            std::memcpy(more.data() + countAt, &count, sizeof(count));
            ASSERT_TRUE(Platform::writeWhole(path, std::string_view(more), Platform::Durability::Whole).isOk());
            EXPECT_EQ(PreparationManifest::read(path, sSignature).size(), 0u);
        }

        /// **A standing model's shapes are read into the store's memory**, where the frame, which
        /// reaches nothing else, finds them: a shape of 8 KiB, past the 4 KiB a start loads, is not in
        /// a reopened store's memory until the model is found standing, and is after. A shape whose
        /// file is gone is a model that does not stand.
        TEST(RtxPreparationManifestTest, aStandingModelsShapesAreReadIntoTheStoresMemory)
        {
            struct Fixed final : FileStamps
            {
                std::uint64_t stampOf(VFS::Path::NormalizedView) override { return 5; }
            };
            Fixed stamps;
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            constexpr StoredKey shape{ .mKey = { { 0x0123456789abcdefull, 0xfedcba9876543210ull } },
                .mPass = ContentPassId::Shape };
            {
                ContentStore::Store store(folder, sContentStorePolicy);
                store.write(shape.mKey, storeKindOf(ContentPassId::Shape), ShapePass::sVersion, Misc::ByteBuffer(8192),
                    ContentStore::Reach::Disk);
                store.flush();
            }

            const std::vector<std::string> files{ "meshes/m.nif" };
            PreparationManifest manifest;
            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{
                    .mDigest = PreparationManifest::digestOf(files, stamps), .mFiles = files, .mKeys = { shape } },
                0);

            ContentStore::Store reopened(folder, sContentStorePolicy);
            reopened.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(reopened.read(
                shape.mKey, storeKindOf(ContentPassId::Shape), ShapePass::sVersion, ContentStore::Reach::Memory, read))
                << "a large shape the start loaded";
            Misc::ByteBuffer scratch;
            EXPECT_TRUE(manifest.standingDigest("meshes/m.nif", stamps, reopened, scratch).has_value());
            EXPECT_TRUE(reopened.read(
                shape.mKey, storeKindOf(ContentPassId::Shape), ShapePass::sVersion, ContentStore::Reach::Memory, read));
            EXPECT_EQ(read.size(), 8192u);

            std::filesystem::remove(reopened.pathOf(shape.mKey));
            ContentStore::Store lost(folder, sContentStorePolicy);
            EXPECT_FALSE(manifest.standingDigest("meshes/m.nif", stamps, lost, scratch).has_value())
                << "a shape the store lost";
        }
    }
}
