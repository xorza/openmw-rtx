#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/surfacedescription.hpp>
#include <components/rtx/prepare/contentpreparation.hpp>
#include <components/rtx/prepare/filestamps.hpp>
#include <components/rtx/prepare/preparationmanifest.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
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

            std::uint64_t stampOf(VFS::Path::NormalizedView path) override
            {
                const auto found = mStamps.find(std::string(path.value()));
                return found != mStamps.end() ? found->second : 1;
            }

            std::map<std::string, std::uint64_t> mStamps;

        private:
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
        };

        Prepared prepare(
            const PreparationSetup& setup, std::vector<std::string> models, std::vector<PreparationLayer> layers = {})
        {
            ContentPreparation preparation(setup, std::move(models), std::move(layers));
            while (!preparation.awaitProgress(std::chrono::milliseconds(100)).isDone())
            {
            }
            const bool written = preparation.finish();
            return Prepared{ .mWalked = preparation.getWalked(),
                .mShapes = preparation.getStats().at(ContentPassId::Shape).mAsked,
                .mChains = preparation.getStats().at(ContentPassId::Chain).mAsked,
                .mSpans = preparation.getStats().at(ContentPassId::AlphaSpan).mAsked,
                .mWritten = written };
        }

        /// **A model is walked once, and again only once a file it read moved.** Two quads, each with
        /// a diffuse of its own and one normal map they share: the first start walks both, folds two
        /// shapes and makes the shared map's chain once; a start where nothing moved walks neither and
        /// writes no manifest;
        /// one where the second model's diffuse moved walks it alone; a content list that no longer
        /// names it leaves it in the manifest, so a switch back walks nothing, until no run named it for
        /// 31 days. A model no file stands for is walked again once its stamp moves, as a mod that adds
        /// it moves it.
        TEST(RtxContentPreparationTest, aModelIsWalkedOnceAndAgainOnlyOnceAFileItReadMoved)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            ContentStore store(folder);
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
            ContentStore store(folder);
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
