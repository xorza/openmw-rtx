#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/surfacedescription.hpp>
#include <components/rtx/prepare/contentpreparation.hpp>
#include <components/rtx/prepare/filestamps.hpp>
#include <components/rtx/prepare/preparationmanifest.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// Two models and the stamps of their files, which a test moves.
        class TwoModels final : public ContentSource, public FileStamps
        {
        public:
            TwoModels()
            {
                // A normal map both models wear, carrying a chain whose second level is black, so
                // its chain is made again.
                mNormals = new osg::Image;
                const std::uint8_t bytes[]
                    = { 128, 128, 255, 0, 128, 136, 255, 0, 120, 128, 247, 100, 112, 128, 251, 200, 0, 0, 0, 0 };
                auto* data = new unsigned char[sizeof(bytes)];
                std::memcpy(data, bytes, sizeof(bytes));
                mNormals->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
                mNormals->setMipmapLevels(osg::Image::MipmapDataType{ 16 });
                mNormals->setFileName("textures/tx_rock_n.dds");

                // A ground diffuse of one level, whose chain is made whole.
                mGround = new osg::Image;
                mGround->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                std::memset(mGround->data(), 200, mGround->getTotalSizeInBytes());
                mGround->setFileName("textures/tx_ground.dds");

                mA = modelOf(Testing::makeQuad(), "textures/tx_a.dds");
                osg::ref_ptr<osg::Geometry> raised = Testing::makeQuad();
                raised->setVertexArray(Testing::makePositions({ osg::Vec3f(0.0f, 0.0f, 1.0f),
                    osg::Vec3f(2.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 1.0f), osg::Vec3f(0.0f, 2.0f, 1.0f) }));
                mB = modelOf(raised, "textures/tx_b.dds");
            }

            osg::ref_ptr<const osg::Node> getTemplate(VFS::Path::NormalizedView path) override
            {
                if (path.value() == "meshes/a.nif")
                    return mA;
                if (path.value() == "meshes/b.nif")
                    return mB;
                return nullptr;
            }

            Result<osg::ref_ptr<const osg::Image>, std::string> getImage(VFS::Path::NormalizedView path) override
            {
                if (path.value() == "textures/tx_ground.dds")
                    return osg::ref_ptr<const osg::Image>(mGround);
                if (path.value() == "textures/tx_rock_n.dds")
                    return osg::ref_ptr<const osg::Image>(mNormals);
                return Err{ std::string("no image here") };
            }

            void letGo(std::span<const std::string> paths) override
            {
                const std::lock_guard lock(mLetGoMutex);
                mLetGo.insert(paths.begin(), paths.end());
            }

            std::set<std::string> takeLetGo()
            {
                const std::lock_guard lock(mLetGoMutex);
                return std::exchange(mLetGo, {});
            }

            std::uint64_t stampOf(VFS::Path::NormalizedView path) override
            {
                const auto found = mStamps.find(std::string(path.value()));
                return found != mStamps.end() ? found->second : 1;
            }

            std::map<std::string, std::uint64_t> mStamps;

        private:
            std::mutex mLetGoMutex;
            std::set<std::string> mLetGo;

            osg::ref_ptr<osg::Node> modelOf(const osg::ref_ptr<osg::Geometry>& quad, const char* diffuse)
            {
                Testing::paint(*quad->getOrCreateStateSet(), diffuse);
                Testing::paint(*quad->getOrCreateStateSet(), *mNormals, TextureRole::Normal);
                osg::ref_ptr<osg::Geode> geode = new osg::Geode;
                geode->addDrawable(quad);
                return geode;
            }

            osg::ref_ptr<osg::Image> mNormals;
            osg::ref_ptr<osg::Image> mGround;
            osg::ref_ptr<osg::Node> mA;
            osg::ref_ptr<osg::Node> mB;
        };

        struct Prepared
        {
            std::size_t mWalked = 0;
            std::uint32_t mShapes = 0;
            std::uint32_t mChains = 0;
            std::uint32_t mSpans = 0;
            bool mWritten = false;
            PreparationTimes mTimes;

            /// From before the preparation was made to after `finish` returned, which its times lie within.
            std::chrono::nanoseconds mAround{};
        };

        Prepared prepare(
            const PreparationSetup& setup, std::vector<std::string> models, std::vector<PreparationLayer> layers = {})
        {
            const auto began = std::chrono::steady_clock::now();
            ContentPreparation preparation(setup, std::move(models), std::move(layers));
            while (!preparation.awaitProgress(std::chrono::milliseconds(100)).isDone())
            {
            }
            const bool written = preparation.finish();
            const std::chrono::nanoseconds around = std::chrono::steady_clock::now() - began;
            return Prepared{ .mWalked = preparation.getWalked(),
                .mShapes = preparation.getStats().at(ContentPassId::Shape).mAsked,
                .mChains = preparation.getStats().at(ContentPassId::Chain).mAsked,
                .mSpans = preparation.getStats().at(ContentPassId::AlphaSpan).mAsked,
                .mWritten = written,
                .mTimes = preparation.getTimes(),
                .mAround = around };
        }

        /// **A model is walked once, and again only once a file it read moved.** Two quads, each with
        /// a diffuse of its own and one normal map they share: the first start walks both, folds two
        /// shapes and makes the shared map's chain once; a start where nothing moved walks neither and
        /// writes no manifest;
        /// one where the second model's diffuse moved walks it alone; a content list that no longer
        /// names it leaves it in the manifest, so a switch back walks nothing, until no run named it for
        /// 31 days. A model no file stands for is walked again once its stamp moves, as a mod that adds
        /// it moves it. Each run says what it took: its two threads, and a time to its last job inside the
        /// call, nought where it had no job; and has the loader let go of every file a job read.
        TEST(RtxContentPreparationTest, aModelIsWalkedOnceAndAgainOnlyOnceAFileItReadMoved)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            ContentStore::Store store(folder, sContentStorePolicy);
            TwoModels content;
            const PreparationSetup setup{ .mContent = content,
                .mStamps = content,
                .mStore = store,
                .mManifest = folder / "prepared.manifest",
                .mHands = 2 };
            const std::vector<std::string> both{ "meshes/a.nif", "meshes/b.nif" };
            const std::vector<std::string> withMissing{ "meshes/a.nif", "meshes/missing.nif" };

            const Prepared first = prepare(setup, both);
            EXPECT_TRUE(first.mWritten);
            EXPECT_EQ(first.mWalked, 2u);
            EXPECT_EQ(first.mShapes, 2u);
            EXPECT_EQ(first.mChains, 1u) << "the shared map made once a run";
            const std::set<std::string> letGo = content.takeLetGo();
            for (const char* file :
                { "meshes/a.nif", "meshes/b.nif", "textures/tx_a.dds", "textures/tx_b.dds", "textures/tx_rock_n.dds" })
                EXPECT_TRUE(letGo.contains(file)) << file << ", every file a job read let go of once it ended";
            EXPECT_EQ(first.mTimes.mThreads, 2u);
            EXPECT_GT(first.mTimes.mWall, std::chrono::nanoseconds::zero());
            EXPECT_LE(first.mTimes.mWall, first.mAround);

            const Prepared none = prepare(setup, {});
            EXPECT_EQ(none.mTimes.mWall, std::chrono::nanoseconds::zero()) << "no job, no time to the last";
            EXPECT_EQ(none.mTimes.mThreads, 2u);

            const auto written = std::filesystem::last_write_time(setup.mManifest);
            std::filesystem::last_write_time(setup.mManifest, written - std::chrono::hours(1));
            const Prepared again = prepare(setup, both);
            EXPECT_EQ(again.mWalked, 0u);
            EXPECT_EQ(std::filesystem::last_write_time(setup.mManifest), written - std::chrono::hours(1))
                << "a manifest written where nothing moved";
            EXPECT_EQ(again.mShapes, 0u);
            EXPECT_EQ(again.mChains, 0u);

            content.mStamps["textures/tx_b.dds"] = 2;
            const Prepared moved = prepare(setup, both);
            EXPECT_EQ(moved.mWalked, 1u) << "the model whose diffuse moved, and no other";
            EXPECT_EQ(moved.mShapes, 1u);

            // **And a model whose entries the store lost is walked again**, and only it: the second model's
            // own entries deleted, as a store refuses a file, under a store opened again, which holds in
            // memory none of what the first wrote. Its walk makes them again. The first store's writes
            // finished first, which run on its own thread: one still queued is an entry no file holds yet.
            {
                store.flush();
                const PreparationManifest filed
                    = PreparationManifest::read(setup.mManifest, ContentPreparation::signatureOf(setup));
                const std::optional<PreparationManifest::Entry> a = filed.find("meshes/a.nif");
                const std::optional<PreparationManifest::Entry> b = filed.find("meshes/b.nif");
                ASSERT_TRUE(a.has_value());
                ASSERT_TRUE(b.has_value());
                std::vector<StoredKey> lost;
                for (const StoredKey& key : b->mKeys)
                    if (std::find(a->mKeys.begin(), a->mKeys.end(), key) == a->mKeys.end())
                        lost.push_back(key);
                ASSERT_FALSE(lost.empty()) << "the second model filed no entry of its own";
                for (const StoredKey& key : lost)
                    ASSERT_TRUE(std::filesystem::remove(store.pathOf(key.mKey)));

                ContentStore::Store reopened(folder, sContentStorePolicy);
                const PreparationSetup lostSetup{ .mContent = content,
                    .mStamps = content,
                    .mStore = reopened,
                    .mManifest = setup.mManifest,
                    .mHands = 2 };
                EXPECT_EQ(prepare(lostSetup, both).mWalked, 1u) << "the model whose entry was lost, and no other";
                reopened.flush();
                for (const StoredKey& key : lost)
                    EXPECT_TRUE(std::filesystem::exists(reopened.pathOf(key.mKey)));
            }

            prepare(setup, { "meshes/a.nif" });
            EXPECT_EQ(PreparationManifest::read(setup.mManifest, ContentPreparation::signatureOf(setup)).size(), 2u)
                << "another list's model stands";
            EXPECT_EQ(prepare(setup, both).mWalked, 0u) << "a switch back walks nothing";
            PreparationSetup monthOn = setup;
            monthOn.mDay = setup.mDay + PreparationManifest::sUnnamedDaysMost;
            prepare(monthOn, { "meshes/a.nif" });
            EXPECT_EQ(PreparationManifest::read(setup.mManifest, ContentPreparation::signatureOf(setup)).size(), 2u)
                << "named 30 days ago";
            monthOn.mDay += 1;
            prepare(monthOn, { "meshes/a.nif" });
            EXPECT_EQ(PreparationManifest::read(setup.mManifest, ContentPreparation::signatureOf(setup)).size(), 1u)
                << "named 31 days ago";

            EXPECT_EQ(prepare(setup, withMissing).mWalked, 1u) << "the missing model, the first time";
            EXPECT_EQ(prepare(setup, withMissing).mWalked, 0u);
            content.mStamps["meshes/missing.nif"] = 5;
            EXPECT_EQ(prepare(setup, withMissing).mWalked, 1u) << "a model whose file came";

            // **And a manifest of another signature is none**: another mask walks both again.
            PreparationSetup data = setup;
            data.mMask = setup.mMask ^ 1u;
            EXPECT_EQ(prepare(data, both).mWalked, 2u);
        }

        /// **A ground layer is read as the ground reader reads it**: its diffuse of one level has its
        /// chain made, its normal map's chain is made, and the normal map, which may carry a height and
        /// does, has its heights' span read; read again only once a file of it moves or a normal map
        /// comes beside its diffuse.
        TEST(RtxContentPreparationTest, aGroundLayerIsReadAsTheGroundReaderReadsIt)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            ContentStore::Store store(folder, sContentStorePolicy);
            TwoModels content;
            const PreparationSetup setup{ .mContent = content,
                .mStamps = content,
                .mStore = store,
                .mManifest = folder / "prepared.manifest",
                .mHands = 1 };
            const std::vector<PreparationLayer> layers{ PreparationLayer{
                .mDiffuse = "textures/tx_ground.dds", .mNormal = "textures/tx_rock_n.dds", .mParallax = true } };

            const Prepared first = prepare(setup, {}, layers);
            EXPECT_EQ(first.mWalked, 1u);
            EXPECT_EQ(first.mChains, 2u) << "the diffuse's whole chain and the normal map's";
            EXPECT_EQ(first.mSpans, 1u) << "the heights the normal map's alpha spans";

            EXPECT_EQ(prepare(setup, {}, layers).mWalked, 0u);
            content.mStamps["textures/tx_rock_n.dds"] = 3;
            EXPECT_EQ(prepare(setup, {}, layers).mWalked, 1u) << "a layer whose normal map moved";

            // **A layer is named by its normal map as well**: the diffuse alone is a layer read anew, and
            // the layer with its normal map stood beside it, read no more on a switch back.
            const PreparationLayer bare{ .mDiffuse = "textures/tx_ground.dds" };
            EXPECT_EQ(prepare(setup, {}, { bare }).mWalked, 1u);
            EXPECT_EQ(prepare(setup, {}, { bare }).mWalked, 0u);
            EXPECT_EQ(prepare(setup, {}, layers).mWalked, 0u) << "the layer with its normal map stood";

            // **And no span where the layer reads no height**, as the reader reads none.
            PreparationLayer flat = layers.front();
            flat.mParallax = false;
            content.mStamps["textures/tx_rock_n.dds"] = 4;
            EXPECT_EQ(prepare(setup, {}, { flat }).mSpans, 0u);
        }
    }
}
