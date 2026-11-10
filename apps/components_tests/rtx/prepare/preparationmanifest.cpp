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

#include <components/misc/bytebuffer.hpp>
#include <components/platform/wholefile.hpp>
#include <components/rtx/prepare/filestamps.hpp>
#include <components/rtx/prepare/preparationmanifest.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
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
            const std::optional<PreparationManifest::Entry> b = read.find("meshes/b.nif");
            ASSERT_TRUE(b.has_value());
            EXPECT_EQ(b->mDigest, 9u);
            EXPECT_EQ(b->mFiles, (std::vector<std::string>{ "meshes/b.nif", "textures/shared.dds" }));
            EXPECT_EQ(b->mKeys, (std::vector<StoredKey>{ sShared, sOwn }));
            EXPECT_EQ(read.find("meshes/a.nif")->mDigest, 7u);
            EXPECT_EQ(read.find("meshes/a.nif")->mKeys, std::vector<StoredKey>{ sShared });
            EXPECT_FALSE(read.find("meshes/c.nif").has_value());

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
            read.name("m", 10);
            EXPECT_FALSE(read.isChanged()) << "named on the day it was";
            read.name("m", 11);
            EXPECT_TRUE(read.isChanged());
            ASSERT_TRUE(read.write(path, sSignature));

            PreparationManifest again = PreparationManifest::read(path, sSignature);
            again.name("m", 11);
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
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store");
            for (const StoredKey& key : { sShared, sOwn })
                store.write(
                    key.mKey, ContentPassId::AlphaSpan, 1, Misc::ByteBuffer{ std::byte{ 1 } }, StoreReach::Disk);
            store.flush();

            const std::vector<std::string> files{ "meshes/m.nif" };
            PreparationManifest manifest;
            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{ .mDigest = PreparationManifest::digestOf(files, stamps),
                    .mFiles = files,
                    .mKeys = { sShared, sOwn } },
                0);
            EXPECT_TRUE(manifest.standsAsWalked("meshes/m.nif", stamps, store, scratch));

            std::filesystem::remove(store.pathOf(sOwn.mKey));
            ContentStore reopened(TestingOpenMW::currentTestDirPath() / "store");
            EXPECT_FALSE(manifest.standsAsWalked("meshes/m.nif", stamps, reopened, scratch)) << "a key the store lost";

            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{
                    .mDigest = PreparationManifest::digestOf(files, stamps), .mFiles = files, .mKeys = { sShared } },
                0);
            EXPECT_TRUE(manifest.standsAsWalked("meshes/m.nif", stamps, reopened, scratch));
            stamps.mStamp = 6;
            EXPECT_FALSE(manifest.standsAsWalked("meshes/m.nif", stamps, reopened, scratch)) << "a stamp that moved";
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
                ContentStore store(folder);
                store.write(
                    shape.mKey, ContentPassId::Shape, ShapePass::sVersion, Misc::ByteBuffer(8192), StoreReach::Disk);
                store.flush();
            }

            const std::vector<std::string> files{ "meshes/m.nif" };
            PreparationManifest manifest;
            manifest.keep("meshes/m.nif",
                PreparationManifest::Entry{
                    .mDigest = PreparationManifest::digestOf(files, stamps), .mFiles = files, .mKeys = { shape } },
                0);

            ContentStore reopened(folder);
            reopened.flush();
            Misc::ByteBuffer read;
            EXPECT_FALSE(reopened.read(shape.mKey, ContentPassId::Shape, ShapePass::sVersion, StoreReach::Memory, read))
                << "a large shape the start loaded";
            Misc::ByteBuffer scratch;
            EXPECT_TRUE(manifest.standsAsWalked("meshes/m.nif", stamps, reopened, scratch));
            EXPECT_TRUE(reopened.read(shape.mKey, ContentPassId::Shape, ShapePass::sVersion, StoreReach::Memory, read));
            EXPECT_EQ(read.size(), 8192u);

            std::filesystem::remove(reopened.pathOf(shape.mKey));
            ContentStore lost(folder);
            EXPECT_FALSE(manifest.standsAsWalked("meshes/m.nif", stamps, lost, scratch)) << "a shape the store lost";
        }
    }
}
