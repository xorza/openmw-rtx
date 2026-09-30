#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <semaphore>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Material>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Quat>
#include <osg/Switch>
#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/Vec4i>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/fakeland.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/refnum.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/common/result.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/mirror/cells/cellreader.hpp>
#include <components/rtx/mirror/cells/cellring.hpp>
#include <components/rtx/mirror/cells/cellsupply.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/cells/nightday.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/rtx/mirror/cells/readermemory.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/sceneextractor.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/lightbuilder.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/terrain/objectstorage.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx::Testing
{
    namespace
    {
        constexpr float sCellSize = static_cast<float>(Constants::CellSizeInUnits);

        /// One reference a storage stands.
        struct Placed
        {
            osg::Vec2i mCell;
            const char* mModel = nullptr;
            ESM::RefNum mRefNum;
            osg::Vec3f mPosition;
            osg::Vec3f mRotation;
            float mScale = 1.0f;
            std::uint32_t mGate = Terrain::sNoGate;
        };

        /// One `LIGH` reference a storage stands, and the record it names. The id is the record's
        /// own, so two lamps with two records are two ids.
        struct Lit
        {
            osg::Vec2i mCell;
            const char* mRecord = nullptr;
            ESM::RefNum mRefNum;
            osg::Vec3f mPosition;
            std::uint32_t mGate = Terrain::sNoGate;
        };

        /// A `LIGH` record reduced the way the engine reduces one: a white lamp of radius 100,
        /// carrying `flags`.
        SceneUtil::LightCommon describeLamp(std::int32_t flags)
        {
            ESM::Light record;
            record.mData.mRadius = 100;
            record.mData.mColor = 0x00FFFFFF;
            record.mData.mFlags = flags;
            return SceneUtil::LightCommon(record);
        }

        /// A storage of a handful of statics and lamps, each in a cell of its own choosing. One
        /// walk of a cell answers both kinds, as the game's does, and a lamp is a reference whose
        /// record `getLight` knows.
        class FewStatics final : public Terrain::ObjectStorage
        {
        public:
            std::vector<Placed> mPlaced;
            std::vector<Lit> mLit;

            /// Whether a walk of a cell throws, which is a reader that fails on its own thread.
            bool mThrows = false;

            /// The records the lamps name: `lit` burns where it stands, `unlit` is off by default,
            /// `flame` flickers and `dark` takes light away.
            std::map<std::string, SceneUtil::LightCommon> mRecords{
                { "lit", describeLamp(0) },
                { "unlit", describeLamp(ESM::Light::OffDefault) },
                { "flame", describeLamp(ESM::Light::Flicker) },
                { "dark", describeLamp(ESM::Light::Negative) },
            };

            std::unique_ptr<Terrain::RefCollector> makeCollector() const override
            {
                return std::make_unique<Terrain::RefCollector>();
            }

            void collect(float, const osg::Vec2i& startCell, ESM::RefId, Terrain::RefKinds kinds,
                Terrain::RefCollector&, std::vector<Terrain::PagedCellRef>& into) const override
            {
                if (mThrows)
                    throw std::runtime_error("a storage that cannot be read");

                into.clear();

                if (Terrain::holds(kinds, Terrain::RefKinds::Paged))
                    for (const Placed& placed : mPlaced)
                        if (placed.mCell == startCell)
                            into.push_back(Terrain::PagedCellRef{
                                .mRefId = ESM::RefId::stringRefId(placed.mModel),
                                .mRefNum = placed.mRefNum,
                                .mPosition = placed.mPosition,
                                .mRotation = placed.mRotation,
                                .mScale = placed.mScale,
                                .mGate = placed.mGate,
                            });

                if (Terrain::holds(kinds, Terrain::RefKinds::Lit))
                    for (const Lit& lamp : mLit)
                        if (lamp.mCell == startCell)
                            into.push_back(Terrain::PagedCellRef{
                                .mRefId = ESM::RefId::stringRefId(lamp.mRecord),
                                .mRefNum = lamp.mRefNum,
                                .mPosition = lamp.mPosition,
                                .mGate = lamp.mGate,
                            });
            }

            std::optional<SceneUtil::LightCommon> getLight(const ESM::RefId& id) const override
            {
                const auto found = mRecords.find(id.getRefIdString());
                return found != mRecords.end() ? std::optional(found->second) : std::nullopt;
            }

            /// The record's id doubles as its model here.
            VFS::Path::Normalized getModel(const ESM::RefId& id) const override
            {
                ++mModelsAsked;
                return VFS::Path::Normalized(id.getRefIdString());
            }

            /// How many times `getModel` was asked, which a reader asks once a record.
            mutable int mModelsAsked = 0;
        };

        /// Templates by name — square sheets of a radius the size rule can be asked about, each
        /// lifted under a transform of its own — and an image for every path the ground asks.
        class FewContent final : public ContentSource
        {
        public:
            /// By the corrected path, which is what a reader asks for: `correctMeshPath` puts
            /// every model under `meshes/`.
            osg::ref_ptr<const osg::Node> getTemplate(VFS::Path::NormalizedView path) override
            {
                if (path.value() == "meshes/tree.nif")
                    return mTree;
                if (path.value() == "meshes/fern.nif")
                    return mFern;
                if (path.value() == "meshes/window.nif")
                    return mWindow;
                if (path.value() == "meshes/ember.nif")
                    return mEmber;
                if (path.value() == "meshes/flame" || path.value() == "meshes/dark")
                    return mLantern;
                if (path.value() == "meshes/broken.nif")
                {
                    ++mBrokenAsked;
                    return mBroken;
                }
                return nullptr;
            }

            Result<osg::ref_ptr<const osg::Image>, std::string> getImage(const VFS::Path::NormalizedView path) override
            {
                return mImages.get(path);
            }

            ImagesByPath mImages;

            /// A four-by-four grey with one level, which is an image the ring has a chain to build
            /// for and a shading to estimate.
            osg::ref_ptr<osg::Image> mBark = [] {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->setFileName("textures/bark.dds");
                image->allocateImage(4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                std::fill_n(image->data(), image->getTotalSizeInBytes(), static_cast<unsigned char>(128));
                return image;
            }();

            osg::ref_ptr<osg::Group> mTree = makeSheet(300.0f, 5.0f);
            osg::ref_ptr<osg::Group> mFern = makeSheet(20.0f, 5.0f);

            /// A fern's sheet that glows, which the size rule never thins.
            osg::ref_ptr<osg::Group> mEmber = [this] {
                osg::ref_ptr<osg::Material> glow = new osg::Material;
                glow->setEmission(osg::Material::FRONT_AND_BACK, osg::Vec4f(1.0f, 0.5f, 0.0f, 1.0f));

                osg::ref_ptr<osg::Group> root = makeSheet(20.0f, 5.0f);
                root->getOrCreateStateSet()->setAttribute(glow);
                return root;
            }();

            /// The model of the `flame` and the `dark` lamps: a fern's sheet, and its `AttachLight` 30 units up
            /// under a transform 10 units along x — `sLanternAnchor`.
            osg::ref_ptr<osg::Group> mLantern = [this] {
                osg::ref_ptr<osg::MatrixTransform> anchor
                    = new osg::MatrixTransform(osg::Matrix::translate(0.0f, 0.0f, 30.0f));
                anchor->setName("AttachLight");
                osg::ref_ptr<osg::MatrixTransform> along
                    = new osg::MatrixTransform(osg::Matrix::translate(10.0f, 0.0f, 0.0f));
                along->addChild(anchor);

                osg::ref_ptr<osg::Group> root = makeSheet(20.0f, 5.0f);
                root->addChild(along);
                return root;
            }();

            /// A `NightDaySwitch` of two sheets, the day's lifted five units and the night's seven,
            /// whose file opens on the night's.
            osg::ref_ptr<osg::Group> mWindow = [this] {
                osg::ref_ptr<osg::Switch> branches = new osg::Switch;
                branches->setName(Constants::NightDayLabel);
                branches->addChild(makeSheet(300.0f, 5.0f), false);
                branches->addChild(makeSheet(300.0f, 7.0f), true);

                osg::ref_ptr<osg::Group> root = new osg::Group;
                root->addChild(branches);
                return root;
            }();

            /// A template whose triangles name a vertex it does not have, and how often it was
            /// asked for.
            osg::ref_ptr<osg::Group> mBroken = [] {
                osg::ref_ptr<osg::Group> root = new osg::Group;
                root->addChild(makeIndexPastItsVertices());
                return root;
            }();
            std::uint32_t mBrokenAsked = 0;

        private:
            osg::ref_ptr<osg::Group> makeSheet(float extent, float lift) const
            {
                osg::ref_ptr<osg::Geometry> sheet = new osg::Geometry;
                const auto corners = sheetAt(extent, 0.0f);
                sheet->setVertexArray(makePositions({ corners[0], corners[1], corners[2], corners[3] }));
                sheet->addPrimitiveSet(makeTriangles({ 0, 1, 2, 0, 2, 3 }));
                paint(*sheet->getOrCreateStateSet(), *mBark);

                osg::ref_ptr<osg::MatrixTransform> lifted
                    = new osg::MatrixTransform(osg::Matrix::translate(0.0f, 0.0f, lift));
                lifted->addChild(sheet);

                osg::ref_ptr<osg::Group> root = new osg::Group;
                root->addChild(lifted);
                return root;
            }
        };

        /// Where the `flame` lamp's model attaches its light — `FewContent::mLantern`.
        const osg::Vec3f sLanternAnchor(10.0f, 0.0f, 30.0f);

        /// Where the game stands a clone of a reference: the transform `MWRender::Objects` builds,
        /// with the quaternion the paging and the objects both spell.
        osg::Matrixf gameStands(const Placed& placed)
        {
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> stand = new SceneUtil::PositionAttitudeTransform;
            stand->setPosition(placed.mPosition);
            stand->setAttitude(osg::Quat(placed.mRotation.z(), osg::Vec3f(0.0f, 0.0f, -1.0f))
                * osg::Quat(placed.mRotation.y(), osg::Vec3f(0.0f, -1.0f, 0.0f))
                * osg::Quat(placed.mRotation.x(), osg::Vec3f(-1.0f, 0.0f, 0.0f)));
            stand->setScale(osg::Vec3f(placed.mScale, placed.mScale, placed.mScale));

            osg::Matrix matrix;
            stand->computeLocalToWorldMatrix(matrix, nullptr);
            return osg::Matrixf(matrix);
        }

        /// The number of cells some point of which lies nearer than `reach` cells to an eye at the
        /// centre of its own cell — the disc `withinReach` draws. A cell `dx` columns over has its
        /// nearest point `|dx| - 0.5` cells away, and one in the eye's own column has it at nought.
        constexpr std::uint32_t cellsWithin(const float reach)
        {
            const auto nearest
                = [](const int away) { return away == 0 ? 0.0f : static_cast<float>(away < 0 ? -away : away) - 0.5f; };

            std::uint32_t count = 0;
            const int span = static_cast<int>(reach) + 1;
            for (int dx = -span; dx <= span; ++dx)
                for (int dy = -span; dy <= span; ++dy)
                {
                    const float ax = nearest(dx);
                    const float ay = nearest(dy);
                    if (ax * ax + ay * ay < reach * reach)
                        ++count;
                }

            return count;
        }

        /// The placed disc at a reach of four cells, and the prepared disc a cell wider. A square
        /// of nine by nine held twelve more, every one of them behind the air that closes at the
        /// reach.
        constexpr std::uint32_t sPlacedCells = cellsWithin(4.0f);
        constexpr std::uint32_t sPreparedCells = cellsWithin(5.0f);
        static_assert(sPlacedCells == 69 && sPreparedCells == 101);

        /// A world with nothing on its graph and a ring beside it, walked from a fixed eye. Every
        /// cell of the land has a record, so every cell stands two ground types.
        class RtxCellRingTest : public ::testing::Test
        {
        protected:
            RtxCellRingTest()
            {
                for (int x = -8; x <= 30; ++x)
                    for (int y = -8; y <= 30; ++y)
                        mLand.mWithData.emplace_back(x, y);

                mRing.setMinSize(0.0f);
                mRing.setSettled(true);

                mAround.mReach = 4.0f * sCellSize;
                mAround.mActiveGrid = osg::Vec4i(-1, -1, 2, 2);
                mAround.mEye = osg::Vec3f(0.5f * sCellSize, 0.5f * sCellSize, 0.0f);
                mAround.mExterior = true;
            }

            /// What `WorldMirror::detach` does, asked of every scenario: the ring told of no
            /// world, its holds given back, the extractor swept — and the scene then empty, or a
            /// row of this world outlived it.
            void TearDown() override
            {
                mRing.follow(WorldAround{});
                mExtractor.detach(mRing);
                EXPECT_TRUE(mScene.isEmpty()) << "a world detached and still standing rows";
            }

            void start(const ESM::RefId worldspace = ESM::Cell::sDefaultWorldspaceId)
            {
                mAround.mWorld = Rtx::CellWorld{
                    .mStorage = &mStorage,
                    .mGround = &mLand,
                    .mContent = &mContent,
                    .mWorldspace = worldspace,
                    .mMask = ~0u,
                };
                mRing.follow(mAround);
            }

            /// Says where the eye stands and what the game holds around it, as one value.
            void around(const osg::Vec3f& eye, const osg::Vec4i& grid)
            {
                mAround.mEye = eye;
                mAround.mActiveGrid = grid;
                mRing.follow(mAround);
            }

            /// The same for a grid the game moved without the eye moving with it.
            void around(const osg::Vec4i& grid) { around(mAround.mEye, grid); }

            /// One frame's walk, which is where the ring adopts, places and stamps. The lists a walk
            /// refills wholesale are emptied first, as a frame empties them.
            ExtractionStats walk(std::size_t frame)
            {
                mScene.clearPlacement();
                mRing.follow(mAround);
                mRing.setFrame(frame);
                const ExtractionStats stats
                    = mExtractor.extractWorld(*mEmpty, osg::Matrixf::identity(), 0, frame, mRing);
                mScene.placements().advance();
                return stats;
            }

            /// Walks until the ring has adopted every cell the band wants, and answers what those
            /// walks came to.
            ///
            /// **A walk adopts one cell, settled or not**, so a band of `sPreparedCells` is that
            /// many walks. `CellRing::setSettled` says why the settled rule is the wait and not the
            /// count.
            ///
            /// What a walk added is summed, and what stands is the last walk's: a sum of the
            /// standing counts would count the whole ring once for every walk it took to build.
            ExtractionStats fill()
            {
                ExtractionStats total;
                ExtractionStats last;
                do
                {
                    last = walk(mWalked++);
                    total += last;
                } while (last.mMeshesAdded > 0);

                total.mDistantStatics = last.mDistantStatics;
                total.mGroundCells = last.mGroundCells;
                total.mInstances = last.mInstances;
                total.mLights = last.mLights;
                return total;
            }

            std::uint32_t placed() const { return mScene.placements().getCounts().mPlaced; }

            /// The height of every placement standing above fifty units, lowest first: what a test
            /// lifted its statics to, apart from the ground at nought.
            std::vector<float> lifted() const
            {
                std::vector<float> heights;
                for (const PlacementRow& row : mScene.placements().getRows())
                {
                    const auto height = static_cast<float>(row.mInstance.mTransform.getTrans().z());
                    if (row.mInstance.isPlaced() && height > 50.0f)
                        heights.push_back(height);
                }
                std::sort(heights.begin(), heights.end());
                return heights;
            }

            /// The placement standing the ground of `cell`, which is the one translated to the
            /// cell's centre.
            std::optional<MeshInstance> groundOf(const osg::Vec2i& cell) const
            {
                const osg::Vec3f centre((static_cast<float>(cell.x()) + 0.5f) * sCellSize,
                    (static_cast<float>(cell.y()) + 0.5f) * sCellSize, 0.0f);

                for (const PlacementRow& row : mScene.placements().getRows())
                    if (row.mInstance.isPlaced() && row.mInstance.mTransform.getTrans() == centre)
                        return row.mInstance;

                return std::nullopt;
            }

            /// The frame the next walk is for, so every walk of a test is a frame of its own.
            std::size_t mWalked = 1;

            WorldAround mAround;

            FewStatics mStorage;
            FakeLand mLand;
            FewContent mContent;
            osg::ref_ptr<osg::Group> mEmpty = new osg::Group;

            SceneDesc mScene;
            SceneExtractor mExtractor{ mScene };
            CellRing mRing{ mExtractor };
        };

        /// A reference stands where the game would stand its clone, on the mesh every copy shares;
        /// what the active grid holds is left to the game; every cell of the reach stands its
        /// ground on a row of the ring's own; and a second walk adds nothing.
        TEST_F(RtxCellRingTest, referencesStandWhereTheGameWouldStandThemOnOneMeshEach)
        {
            const Placed tree{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 1, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.25f * sCellSize, 100.0f),
                .mRotation = osg::Vec3f(0.1f, 0.2f, 1.5f),
                .mScale = 2.0f };
            const Placed anotherTree{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 2, 0 },
                .mPosition = osg::Vec3f(3.2f * sCellSize, 0.75f * sCellSize, 50.0f) };
            const Placed fern{ .mCell = osg::Vec2i(0, 3),
                .mModel = "fern.nif",
                .mRefNum = ESM::RefNum{ 3, 0 },
                .mPosition = osg::Vec3f(0.5f * sCellSize, 3.5f * sCellSize, 0.0f) };
            const Placed atHome{ .mCell = osg::Vec2i(0, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 4, 0 },
                .mPosition = osg::Vec3f(0.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            const Placed beyond{ .mCell = osg::Vec2i(7, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 5, 0 },
                .mPosition = osg::Vec3f(7.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            mStorage.mPlaced = { tree, anotherTree, fern, atHome, beyond };

            mRing.setSpecularLayout(SpecularLayout::MetalRoughness);
            start();

            const ExtractionStats first = fill();
            EXPECT_EQ(first.mDistantStatics, 3u)
                << "two trees and a fern; the active grid's is the game's and the far one is past the reach";
            EXPECT_EQ(first.mGroundCells, sPlacedCells)
                << "every cell of the reach's ground, the active grid's included";
            EXPECT_EQ(first.mInstances, 3u + sPlacedCells);
            EXPECT_EQ(placed(), 3u + sPlacedCells);
            EXPECT_EQ(first.mMeshesAdded, 2u + sPreparedCells)
                << "one mesh for the tree however many stand, one for the fern, and one a cell of ground";
            EXPECT_EQ(first.mMaterialsAdded, 2u + sPreparedCells);
            EXPECT_EQ(mRing.getHeldCellCount(), sPreparedCells);
            EXPECT_EQ(first.mPreprocessed.mOffFrame.at(ContentPassId::Shape).mAsked, 2u)
                << "the tree and the fern shaped once each, on the reader's thread, however many stand";
            EXPECT_EQ(mExtractor.getPreprocessor().takeStats().at(ContentPassId::Shape).mAsked, 0u)
                << "the frame adopts what the reader shaped";

            // **The ground stands where the storage put it**: cell (3, 0)'s placement is translated
            // to the cell's centre, and its mesh's first vertex is the storage's own south-western
            // corner. Its two layers, outside the active grid, ask for a composite; the eye's own
            // cell shades from its stack.
            const std::optional<MeshInstance> far = groundOf(osg::Vec2i(3, 0));
            ASSERT_TRUE(far.has_value());
            EXPECT_EQ(mScene.meshes().getMeshPositions(far->mMesh)[0], FakeLand::positionAt(0, 0));
            EXPECT_EQ(mScene.meshes().getMeshPositions(far->mMesh).size(),
                static_cast<std::size_t>(FakeLand::sVerts) * FakeLand::sVerts);
            {
                const Material& material = mScene.materials().getRows()[far->mMaterial];
                EXPECT_EQ(material.mKind, MaterialKind::Terrain);
                EXPECT_EQ(material.mLayers.mCount, 2u);
                EXPECT_TRUE(material.mFlatten);

                // The rock's maps: its normal map in a slot of its own, tiled and read as data, and
                // its `_diffusespec` authored under the layout that reads the alpha as a roughness.
                const std::span<const MaterialLayer> layers = material.mLayers.in(mScene.materials().getLayers());
                EXPECT_EQ(layers[0].mNormal, sNoIndex);
                EXPECT_EQ(layers[0].mFlags, 0u);
                ASSERT_NE(layers[1].mNormal, sNoIndex);
                const TextureRow& normal = mScene.textures().getRows()[layers[1].mNormal];
                EXPECT_EQ(normal.mPath, "textures/rock_nh.dds");
                EXPECT_EQ(normal.mWrap, TextureWrap::Repeat);
                EXPECT_EQ(normal.mEncoding, TextureEncoding::Normal);

                // Each slot keeps the image the reader opened on its thread, which is what the
                // upload reads: the frame it lands on opens no file.
                EXPECT_EQ(
                    normal.mImage, mContent.mImages.get(VFS::Path::NormalizedView("textures/rock_nh.dds")).value());
                EXPECT_EQ(mScene.textures().getRows()[layers[1].mDiffuse].mImage,
                    mContent.mImages.get(VFS::Path::NormalizedView("textures/rock_diffusespec.dds")).value());
                EXPECT_EQ(layers[1].mFlags, Shaders::LAYER_AUTHORED | Shaders::LAYER_PARALLAX);
                EXPECT_TRUE(material.mLayersMapped);
            }
            const std::optional<MeshInstance> home = groundOf(osg::Vec2i(0, 0));
            ASSERT_TRUE(home.has_value());
            EXPECT_FALSE(mScene.materials().getRows()[home->mMaterial].mFlatten);
            EXPECT_FALSE(groundOf(osg::Vec2i(5, 0)).has_value()) << "prepared a band out, and not placed";

            // The sheet's origin, lifted five units in the template and then stood as the game
            // stands the reference — which one placement of the three lands on. The cells are
            // walked in their own order, so the tree's is not the first placement.
            const osg::Vec3f expected = osg::Vec3f(0.0f, 0.0f, 5.0f) * gameStands(tree);
            std::size_t standing = 0;
            for (const PlacementRow& row : mScene.placements().getRows())
            {
                const osg::Vec3f stood = osg::Vec3f() * row.mInstance.mTransform;
                if ((stood - expected).length() < 0.01f)
                    ++standing;
            }
            EXPECT_EQ(standing, 1u) << "one placement stands where the game would stand the scaled, turned tree";

            // The other tree is neither turned nor scaled, so its sheet stands exactly five over
            // the reference: 50 + 5, by hand.
            const osg::Vec3f untilted = osg::Vec3f(0.0f, 0.0f, 5.0f) * gameStands(anotherTree);
            EXPECT_NEAR(untilted.z(), 55.0f, 0.001f);
            EXPECT_NEAR(untilted.x(), 3.2f * sCellSize, 0.01f);
            standing = 0;
            for (const PlacementRow& row : mScene.placements().getRows())
                if ((osg::Vec3f() * row.mInstance.mTransform - untilted).length() < 0.01f)
                    ++standing;
            EXPECT_EQ(standing, 1u);

            // **A steady frame reaches the heap zero times**, which is the rule every loader here
            // keeps: what the ring holds is placed and counted out of buffers it already grew.
            const std::size_t before = Testing::getAllocationCount();
            const ExtractionStats again = walk(mWalked++);
            const std::size_t spent = Testing::getAllocationCount() - before;
            EXPECT_EQ(spent, 0u) << "a steady walk of the ring reached the heap " << spent << " times";

            EXPECT_EQ(again.mMeshesAdded, 0u) << "a second walk of one frame adds nothing";
            EXPECT_EQ(again.mMaterialsAdded, 0u);
            EXPECT_EQ(again.mMeshesReused, 0u) << "what the ring holds is held, and is not met again to be reused";
            EXPECT_EQ(placed(), 3u + sPlacedCells);

            EXPECT_TRUE(mExtractor.retire().empty()) << "everything the ring holds is held through the sweep";

            // A script disables one tree: the walk after it stands two. Swept between walks, as
            // every frame of the game is.
            mRing.setReferenceEnabled(ESM::RefNum{ 1, 0 }, false);
            walk(2);
            EXPECT_EQ(placed(), 2u + sPlacedCells);
            EXPECT_TRUE(mExtractor.retire().empty()) << "a reference kept out still stands on a mesh the ring holds";
            mRing.setReferenceEnabled(ESM::RefNum{ 1, 0 }, true);
            walk(3);
            EXPECT_EQ(placed(), 3u + sPlacedCells);
            EXPECT_TRUE(mExtractor.retire().empty());

            // The chunks that asked are flattened as the game's queue flattens them, and the rock
            // reflects, so cell (3, 0) is given a gloss beside its composite.
            CompositeQueue composites;
            while (composites.advance(mScene) > 0)
            {
            }
            EXPECT_NE(mScene.materials().getRows()[far->mMaterial].mDiffuse, sNoIndex);
            EXPECT_NE(mScene.materials().getRows()[far->mMaterial].mSpecular, sNoIndex);

            // The active grid moves over cell (3, 0): its ground shades from its stack from now
            // on, on the same row and with neither of the two images its composite was, the two
            // trees inside the grid are the game's, and the tree at the eye's own cell — outside
            // the grid now — is the ring's.
            around(osg::Vec4i(2, -1, 5, 2));
            walk(mWalked++);
            EXPECT_EQ(placed(), 2u + sPlacedCells) << "the fern and the tree at home stand outside the grid";
            EXPECT_FALSE(mScene.materials().getRows()[far->mMaterial].mFlatten);
            EXPECT_EQ(mScene.materials().getRows()[far->mMaterial].mDiffuse, sNoIndex);
            EXPECT_EQ(mScene.materials().getRows()[far->mMaterial].mSpecular, sNoIndex);
            EXPECT_TRUE(mExtractor.retire().empty());

            // The eye leaves for a cell far away. **The band that left goes on the first walk after
            // the move and the band that arrives comes a cell a walk after it**, so the sweep that
            // follows that one walk is where the meshes nothing stands on go — the two models' and
            // the ground of every cell that left.
            // Under the layout that reads a `_diffusespec`'s alpha as no roughness from here on.
            mRing.setSpecularLayout(SpecularLayout::Ignore);
            around(osg::Vec3f(20.5f * sCellSize, 20.5f * sCellSize, 0.0f), osg::Vec4i(19, 19, 22, 22));
            walk(mWalked++);

            const Retirement went = mExtractor.retire();
            EXPECT_EQ(went.mMeshes, 2u + sPreparedCells);
            EXPECT_EQ(went.mMaterials, 2u + sPreparedCells);

            fill();
            EXPECT_EQ(placed(), sPlacedCells) << "ground and nothing on it";
            EXPECT_EQ(mRing.getHeldCellCount(), sPreparedCells) << "the prepared disc's cells";
            EXPECT_EQ(mScene.meshes().getLiveCount(), sPreparedCells);

            // So the rock there is a diffuse like any, and keeps its normal map.
            const std::optional<MeshInstance> away = groundOf(osg::Vec2i(24, 20));
            ASSERT_TRUE(away.has_value());
            const Material& awayMaterial = mScene.materials().getRows()[away->mMaterial];
            const std::span<const MaterialLayer> awayLayers = awayMaterial.mLayers.in(mScene.materials().getLayers());
            ASSERT_EQ(awayLayers.size(), 2u);
            EXPECT_EQ(awayLayers[1].mFlags, Shaders::LAYER_PARALLAX);
            ASSERT_NE(awayLayers[1].mNormal, sNoIndex);
            EXPECT_EQ(mScene.textures().getRows()[awayLayers[1].mNormal].mPath, "textures/rock_nh.dds");
            EXPECT_TRUE(awayMaterial.mLayersMapped);
        }

        /// The lamps of the cells the game has not loaded stand with their cells: outside the active
        /// grid only, because inside it the game's own graph carries them and a lantern must not be
        /// counted twice; where the record casts at all; and as the light the walk would build from
        /// the graph's own node at the same hour, so a lamp the game loads later is the lamp that
        /// was there: at the model's `AttachLight`, where the game attaches it, and with its model
        /// standing as a static does. The frame's clock reaches them: a flame at one second is not
        /// the flame at nought.
        ///
        /// **A fake and not a cell, because the shipped content cannot ask about an unlit lamp.**
        /// Every one of the 1559 exterior cells of `Morrowind.esm`, `Tribunal.esm` and
        /// `Bloodmoon.esm` places its lights lit: the off-default flag is an interior's brazier and
        /// a storeroom's torch.
        TEST_F(RtxCellRingTest, lampsStandWithTheirCellsOutsideTheGridAndBurnAtTheFramesHour)
        {
            const osg::Vec3f far(4.5f * sCellSize, 0.5f * sCellSize, 40.0f);
            mStorage.mLit = {
                Lit{ .mCell = osg::Vec2i(4, 0), .mRecord = "flame", .mRefNum = ESM::RefNum{ 7, 0 }, .mPosition = far },
                Lit{ .mCell = osg::Vec2i(3, 0), .mRecord = "unlit", .mRefNum = ESM::RefNum{ 8, 0 } },
                Lit{ .mCell = osg::Vec2i(0, 0), .mRecord = "lit", .mRefNum = ESM::RefNum{ 9, 0 } },
                Lit{ .mCell = osg::Vec2i(7, 0), .mRecord = "lit", .mRefNum = ESM::RefNum{ 10, 0 } },
            };
            mAround.mSimulationTime = 0.0;
            start();

            const ExtractionStats filled = fill();
            ASSERT_EQ(mScene.lights().size(), std::size_t{ 1 })
                << "one lamp is in reach, outside the grid and lit: (4, 0). (0, 0) is the game's, (3, 0) is off "
                   "by default and (7, 0) is out of reach";
            EXPECT_EQ(filled.mLights, 1u) << "and the walk's report counts it";
            EXPECT_EQ(filled.mDistantStatics, 1u) << "the lantern's model, the one lamp that has one";

            const osg::Vec3f anchored = far + sLanternAnchor;
            const Light stood = mScene.lights().front();
            EXPECT_EQ(stood.mPosition, anchored) << "through both transforms above the anchor";

            // The light the walk builds from the graph's own node, to the bit: one rule, in
            // `makeLight`, phased by the reference number.
            const std::optional<Light> built = makeLight(mStorage.mRecords.at("flame"), anchored, 0.0, 7).value();
            ASSERT_TRUE(built.has_value());
            EXPECT_EQ(stood.mIntensity, built->mIntensity);
            EXPECT_EQ(stood.mReach, built->mReach);
            EXPECT_EQ(stood.mSourceRadius, built->mSourceRadius);

            // A second later the flame has moved, because the hour reaches it through the walk.
            mAround.mSimulationTime = 1.0;
            mRing.follow(mAround);
            walk(mWalked++);
            ASSERT_EQ(mScene.lights().size(), std::size_t{ 1 });
            EXPECT_NE(mScene.lights().front().mIntensity, stood.mIntensity) << "a flame that stood still";
            EXPECT_EQ(mScene.lights().front().mIntensity,
                makeLight(mStorage.mRecords.at("flame"), anchored, 1.0, 7).value()->mIntensity);

            // What the game says of the lamp reaches its light as it reaches its model.
            mRing.setReferenceEnabled(ESM::RefNum{ 7, 0 }, false);
            const ExtractionStats disabled = walk(mWalked++);
            EXPECT_EQ(disabled.mLights, 0u) << "a lamp a script disabled";
            EXPECT_EQ(disabled.mDistantStatics, 0u) << "and its model with it";
            mRing.setReferenceEnabled(ESM::RefNum{ 7, 0 }, true);
            EXPECT_EQ(walk(mWalked++).mLights, 1u) << "enabled again";
            mRing.blacklistReference(ESM::RefNum{ 7, 0 });
            EXPECT_EQ(walk(mWalked++).mLights, 0u) << "a lantern the player took";
            mRing.forgetReferences();
            EXPECT_EQ(walk(mWalked++).mLights, 1u) << "a cleared world";

            // The grid grows over the lamp's cell, and the game's graph is what carries it now.
            around(osg::Vec4i(-1, -1, 5, 2));
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 0u) << "nor its model";
            EXPECT_TRUE(mScene.lights().empty()) << "a lamp inside the active grid would be counted twice";

            // And back out again, on the frame the grid leaves it.
            around(osg::Vec4i(-1, -1, 2, 2));
            walk(mWalked++);
            EXPECT_EQ(mScene.lights().size(), std::size_t{ 1 });
        }

        /// **What the reader cannot take is refused where its cell is adopted**, on the frame's
        /// thread, which is the one that reports: a model its walk refused, and a lamp that takes
        /// light away. The model is walked once however many references name it, and what else the
        /// cell holds stands. A land texture that does not read is not the reader's to refuse: its
        /// layer stands, and the texture table stands it in and refuses it as it does any texture.
        TEST_F(RtxCellRingTest, whatTheReaderCannotTakeIsRefusedWhereItsCellIsAdopted)
        {
            mContent.mImages.lose("textures/rock_diffusespec.dds");

            const osg::Vec3f inCell(3.5f * sCellSize, 1.5f * sCellSize, 0.0f);
            mStorage.mPlaced = {
                Placed{ .mCell = osg::Vec2i(3, 1),
                    .mModel = "broken.nif",
                    .mRefNum = ESM::RefNum{ 1, 0 },
                    .mPosition = inCell },
                Placed{ .mCell = osg::Vec2i(3, 1),
                    .mModel = "broken.nif",
                    .mRefNum = ESM::RefNum{ 2, 0 },
                    .mPosition = inCell },
                Placed{ .mCell = osg::Vec2i(3, 1),
                    .mModel = "tree.nif",
                    .mRefNum = ESM::RefNum{ 3, 0 },
                    .mPosition = inCell },
            };
            mStorage.mLit = {
                Lit{
                    .mCell = osg::Vec2i(3, 1), .mRecord = "dark", .mRefNum = ESM::RefNum{ 4, 0 }, .mPosition = inCell },
                Lit{ .mCell = osg::Vec2i(3, 1), .mRecord = "lit", .mRefNum = ESM::RefNum{ 5, 0 }, .mPosition = inCell },
            };
            start();

            const ExtractionStats filled = fill();
            EXPECT_EQ(mScene.refusals().count(Refused::Model), 1u);
            EXPECT_EQ(mScene.refusals().count(Refused::Lamp), 1u);
            EXPECT_EQ(mContent.mBrokenAsked, 1u) << "a refused model walked again for its second reference";

            EXPECT_EQ(filled.mDistantStatics, 2u)
                << "the tree beside the broken models stands, and so does the dark lamp's lantern: a light this "
                   "refuses is no model it refuses";
            ASSERT_EQ(mScene.lights().size(), std::size_t{ 1 }) << "and the lamp beside the dark one burns";
            EXPECT_EQ(mScene.lights().front().mPosition, inCell);

            const Index lost = mScene.textures().findFile(VFS::Path::Normalized("textures/rock_diffusespec.dds"));
            ASSERT_NE(lost, sNoIndex) << "a layer whose image does not read was dropped rather than stood in for";
            EXPECT_EQ(mScene.textures().getRows()[lost].mImage, nullptr)
                << "the slot the upload stands in keeps no image";
        }

        /// The paging's size rule, per reference: a radius under the threshold at the eye's distance
        /// to the cell is not stood, and the threshold is the eye's and not the chunk's. A model
        /// that glows is never thinned, however small, and nor is a lamp whose light stands; a lamp
        /// this cannot light, one that takes light away, is thinned as a fern is.
        TEST_F(RtxCellRingTest, theSizeRuleIsTheEyesDistanceTimesTheSetting)
        {
            const Placed tree{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 1, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            const Placed fern{ .mCell = osg::Vec2i(3, 0),
                .mModel = "fern.nif",
                .mRefNum = ESM::RefNum{ 2, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            const Placed ember{ .mCell = osg::Vec2i(3, 0),
                .mModel = "ember.nif",
                .mRefNum = ESM::RefNum{ 3, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            mStorage.mPlaced = { tree, fern, ember };
            const osg::Vec3f lampAt(3.5f * sCellSize, 0.5f * sCellSize, 40.0f);
            mStorage.mLit = {
                Lit{ .mCell = osg::Vec2i(3, 0),
                    .mRecord = "flame",
                    .mRefNum = ESM::RefNum{ 4, 0 },
                    .mPosition = lampAt },
                Lit{
                    .mCell = osg::Vec2i(3, 0), .mRecord = "dark", .mRefNum = ESM::RefNum{ 5, 0 }, .mPosition = lampAt },
            };

            // The eye stands half a cell in, so cell 3 begins two and a half cells away: 20480
            // units, and a hundredth of that is 204.8. The tree's sheet reaches 300 * sqrt(2), and
            // the fern's, the ember's and each lantern's 20 * sqrt(2).
            mRing.setMinSize(0.01f);
            start();

            EXPECT_EQ(fill().mDistantStatics, 3u) << "the tree clears 204.8, the ember glows and the flame gives "
                                                     "light; the fern and the dark lamp do not";

            // Nearer, the fern clears too: at a hundredth of 8192 the threshold is 81.92, and the
            // fern's 28.28 still does not — so the threshold is lowered instead.
            mRing.setMinSize(0.001f);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 5u) << "at 20.48 all five clear";

            // A threshold of 20480 thins even the tree.
            mRing.setMinSize(1.0f);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 2u) << "the ember and the flame alone";
        }

        /// What the size rule admits is a prefix of the cell's placements, largest first, and a
        /// walk touches only what crossed the prefix's end since the last one. A disabled
        /// reference leaves and returns the moment the script says so, one disabled before its
        /// cell is held arrives that way, and a cleared world forgets them all.
        ///
        /// Five trees of one sheet at scales five to one, so their radii are 424.26 times each —
        /// 2121.3, 1697.1, 1272.8, 848.5 and 424.3. The eye stands half a cell in and cell 3 begins
        /// two and a half cells away, 20480 units, so a setting of `t / 20480` is a threshold of
        /// `t`: 1000 admits three, 600 admits four, 400 all five.
        TEST_F(RtxCellRingTest, theSizeRuleAdmitsAPrefixAndAToggleLandsAtOnce)
        {
            constexpr float sDistance = 2.5f * sCellSize;

            for (std::uint32_t scale = 5; scale >= 1; --scale)
                mStorage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(3, 0),
                    .mModel = "tree.nif",
                    .mRefNum = ESM::RefNum{ scale, 0 },
                    .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 100.0f * static_cast<float>(scale)),
                    .mScale = static_cast<float>(scale) });
            const Placed elsewhere{ .mCell = osg::Vec2i(3, 1),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 6, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 1.5f * sCellSize, 0.0f),
                .mScale = 5.0f };
            mStorage.mPlaced.push_back(elsewhere);

            // Every placed slot's height, which names the tree standing in it: the template's lift
            // of five is scaled with the reference, so a tree at scale `s` stands at `105 s`.
            const auto standing = [this] {
                std::vector<std::pair<std::size_t, float>> slots;
                const std::span<const PlacementRow> all = mScene.placements().getRows();
                for (std::size_t slot = 0; slot < all.size(); ++slot)
                    if (all[slot].mInstance.isPlaced())
                        slots.emplace_back(slot, all[slot].mInstance.mTransform.getTrans().z());
                return slots;
            };
            const auto heights = [](const std::vector<std::pair<std::size_t, float>>& slots) {
                std::vector<float> lifted;
                for (const auto& [slot, height] : slots)
                    if (height > 50.0f)
                        lifted.push_back(height);
                std::sort(lifted.begin(), lifted.end());
                return lifted;
            };
            const auto trees = [](std::initializer_list<int> scales) {
                std::vector<float> lifted;
                for (const int scale : scales)
                    lifted.push_back(105.0f * static_cast<float>(scale));
                return lifted;
            };
            // Whether every slot of `before` still stands with the same tree in it.
            const auto keeps = [](const std::vector<std::pair<std::size_t, float>>& before,
                                   const std::vector<std::pair<std::size_t, float>>& after) {
                return std::all_of(before.begin(), before.end(),
                    [&](const auto& was) { return std::find(after.begin(), after.end(), was) != after.end(); });
            };

            mRing.setReferenceEnabled(ESM::RefNum{ 6, 0 }, false);
            mRing.setMinSize(1000.0f / sDistance);
            start();

            EXPECT_EQ(fill().mDistantStatics, 3u)
                << "the three largest, and the disabled one in the next cell arrived out";
            const auto three = standing();
            EXPECT_EQ(heights(three), trees({ 3, 4, 5 }));

            mRing.setMinSize(600.0f / sDistance);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 4u) << "the fourth clears 600";
            const auto four = standing();
            EXPECT_TRUE(keeps(three, four)) << "one add and no drop: the three stand where they stood";
            EXPECT_EQ(heights(four), trees({ 2, 3, 4, 5 }));

            mRing.setMinSize(1000.0f / sDistance);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 3u);
            EXPECT_TRUE(keeps(three, standing())) << "one drop and no add";

            // A script disables the tree at scale four, inside the prefix: gone before any walk.
            mRing.setReferenceEnabled(ESM::RefNum{ 4, 0 }, false);
            EXPECT_EQ(heights(standing()), trees({ 3, 5 }));
            mRing.setReferenceEnabled(ESM::RefNum{ 4, 0 }, true);
            EXPECT_EQ(heights(standing()), trees({ 3, 4, 5 }));

            // And the tree at scale one, outside it: nothing changes now, and when the threshold
            // falls to admit all five it is the one still kept out.
            mRing.setReferenceEnabled(ESM::RefNum{ 1, 0 }, false);
            EXPECT_EQ(heights(standing()), trees({ 3, 4, 5 }));
            mRing.setMinSize(400.0f / sDistance);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 4u) << "four of five, the disabled one skipped";
            EXPECT_EQ(heights(standing()), trees({ 2, 3, 4, 5 }));
            mRing.setReferenceEnabled(ESM::RefNum{ 1, 0 }, true);
            EXPECT_EQ(heights(standing()), trees({ 1, 2, 3, 4, 5 })) << "enabled inside the prefix, at once";
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 5u) << "and the walk keeps it";

            // The tree in the next cell was disabled before its cell was held; enabled, it stands.
            mRing.setReferenceEnabled(ESM::RefNum{ 6, 0 }, true);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 6u);

            // The game moved the tree at scale three, so it is blacklisted: down at once, and a
            // script's word does not raise it — upstream's paging keeps a moved object out of the
            // distance whatever a script says, because where it stands now is the game's.
            mRing.blacklistReference(ESM::RefNum{ 3, 0 });
            EXPECT_EQ(heights(standing()), trees({ 1, 2, 4, 5 }));
            mRing.setReferenceEnabled(ESM::RefNum{ 3, 0 }, true);
            EXPECT_EQ(heights(standing()), trees({ 1, 2, 4, 5 })) << "enabled, and still blacklisted";
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 5u) << "and the walk keeps it down";

            // The world is cleared: what two scripts kept out and what the game blacklisted stand
            // again at once, and the walk keeps it — a list carried into the next game would be
            // the first game's holes in the second one's distance.
            mRing.setReferenceEnabled(ESM::RefNum{ 2, 0 }, false);
            mRing.setReferenceEnabled(ESM::RefNum{ 6, 0 }, false);
            EXPECT_EQ(heights(standing()), trees({ 1, 4, 5 }));
            mRing.forgetReferences();
            EXPECT_EQ(heights(standing()), trees({ 1, 2, 3, 4, 5 }));
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 6u);
        }

        /// **A gate is the game's answer for the references behind it, over a script's word.** Three
        /// trees in cell 3 at scales three to one — lifted `105 s`, so 315, 210 and 105 — the first
        /// two behind gate 0 with a lamp that has no model, and a fourth behind gate 1 in cell
        /// (3, 1), whose gate closes before its cell is held. A gate never told keeps its trees and
        /// the lamp's light down; open, they stand whatever a script said of one; undecided, each
        /// stands by the script's word as an ungated tree does; closed, none does. The blacklist
        /// wins over an open gate, and a cleared world keeps the gates, which the game tells again
        /// anyway. Once the grid covers both cells, every reference behind a gate that decided has
        /// its verdict, the lamp's among them.
        TEST_F(RtxCellRingTest, aGateDecidesItsReferencesInTheDistanceOverAScriptsWord)
        {
            for (std::uint32_t scale = 3; scale >= 1; --scale)
                mStorage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(3, 0),
                    .mModel = "tree.nif",
                    .mRefNum = ESM::RefNum{ scale, 0 },
                    .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 100.0f * static_cast<float>(scale)),
                    .mScale = static_cast<float>(scale),
                    .mGate = scale >= 2 ? 0u : Terrain::sNoGate });
            mStorage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(3, 1),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 4, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 1.5f * sCellSize, 400.0f),
                .mScale = 4.0f,
                .mGate = 1 });
            mStorage.mLit.push_back(Lit{ .mCell = osg::Vec2i(3, 0),
                .mRecord = "lit",
                .mRefNum = ESM::RefNum{ 5, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 40.0f),
                .mGate = 0 });
            const auto lit = [this] { return walk(mWalked++).mLights; };

            const std::vector<float> none = { 105.0f };
            const std::vector<float> all = { 105.0f, 210.0f, 315.0f };

            mRing.setGate(1, Terrain::GateState::Closed);
            start();
            EXPECT_EQ(fill().mLights, 0u) << "the lamp behind gate 0 is dark";
            EXPECT_EQ(lifted(), none) << "gate 0 was never told, and gate 1 closed before its cell arrived";

            mRing.setGate(0, Terrain::GateState::Open);
            EXPECT_EQ(lifted(), all) << "open, at once";
            mRing.setReferenceEnabled(ESM::RefNum{ 3, 0 }, false);
            mRing.setReferenceEnabled(ESM::RefNum{ 5, 0 }, false);
            EXPECT_EQ(lifted(), all) << "a script's word under an open gate is one the gate moved past";
            EXPECT_EQ(lit(), 1u) << "the lamp's too";

            mRing.setGate(0, Terrain::GateState::Undecided);
            EXPECT_EQ(lifted(), (std::vector<float>{ 105.0f, 210.0f })) << "undecided: the script's word stands";
            EXPECT_EQ(lit(), 0u) << "the lamp's too";

            mRing.setGate(0, Terrain::GateState::Closed);
            EXPECT_EQ(lifted(), none);

            mRing.blacklistReference(ESM::RefNum{ 2, 0 });
            mRing.blacklistReference(ESM::RefNum{ 5, 0 });
            mRing.setGate(0, Terrain::GateState::Open);
            EXPECT_EQ(lifted(), (std::vector<float>{ 105.0f, 315.0f })) << "the blacklist wins over an open gate";
            EXPECT_EQ(lit(), 0u) << "over the lamp's too";

            mRing.forgetReferences();
            EXPECT_EQ(lifted(), all) << "a cleared world forgets the blacklist and keeps the gate";
            const ExtractionStats cleared = walk(mWalked++);
            EXPECT_EQ(cleared.mDistantStatics, 3u) << "and the walk keeps what the gates say";
            EXPECT_EQ(cleared.mLights, 1u) << "the lamp's with them";

            mRing.setGate(1, Terrain::GateState::Open);
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 4u) << "the tree in the next cell stands once its gate opens";

            around(osg::Vec4i(-1, -1, 4, 2));
            std::vector<GateVerdict> verdicts;
            mRing.collectGateVerdicts(verdicts);
            std::vector<std::uint32_t> judged;
            for (const GateVerdict& verdict : verdicts)
            {
                EXPECT_TRUE(verdict.mStands) << verdict.mRefNum.mIndex << ": both gates are open";
                judged.push_back(verdict.mRefNum.mIndex);
            }
            EXPECT_EQ(judged, (std::vector<std::uint32_t>{ 2, 3, 5, 4 }))
                << "cell (3, 0)'s two trees and its lamp, then cell (3, 1)'s tree, once each";
        }

        /// A window in cell 3 at a height of 100, whose day sheet stands at 105 and night sheet at
        /// 107, beside a tree at 200, which stands at 205 in every mode. A cell arriving at night
        /// stands the night's sheet; each mode after it moves the sheet on the next follow, before
        /// any walk: the day's at dawn, the day's again in a mode past the switch's two children,
        /// which `DayNightCallback` answers with the first, and the night's where the game drives
        /// no switch, because the file opens on it.
        TEST_F(RtxCellRingTest, aDayNightSwitchStandsTheBranchOfTheWorldsMode)
        {
            mStorage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(3, 0),
                .mModel = "window.nif",
                .mRefNum = ESM::RefNum{ 1, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 100.0f) });
            mStorage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 2, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.25f * sCellSize, 200.0f) });

            const std::vector<float> day = { 105.0f, 205.0f };
            const std::vector<float> night = { 107.0f, 205.0f };

            mAround.mNightDay = NightDayMode::ExteriorNight;
            start();
            fill();
            EXPECT_EQ(lifted(), night) << "a cell that arrives at night";

            for (const auto& [mode, stands] :
                { std::pair{ NightDayMode::Default, day }, std::pair{ NightDayMode::InteriorDay, day },
                    std::pair{ NightDayMode::Authored, night }, std::pair{ NightDayMode::ExteriorNight, night } })
            {
                mAround.mNightDay = mode;
                mRing.follow(mAround);
                EXPECT_EQ(lifted(), stands) << static_cast<int>(mode) << ": at once";
                EXPECT_EQ(walk(mWalked++).mDistantStatics, 2u) << static_cast<int>(mode) << ": one sheet and the tree";
                EXPECT_EQ(lifted(), stands) << static_cast<int>(mode) << ": and the walk keeps it";
            }
        }

        /// A model the frame lets go of and a delivered cell names again inside the same settled
        /// walk stays lent.
        ///
        /// **The return is stale before it is published.** A settled walk drops the cells that
        /// left the band, and so lets go of their models, before it waits for the thread to read
        /// the cells that entered — and one of those names the same model, read while the frame
        /// still held it. The frame knows the model again from that cell, so the return it wrote
        /// earlier in the walk names a lend the frame holds; published, it had the reader give
        /// the model back under the frame's feet and refill it for the next model.
        TEST_F(RtxCellRingTest, aModelNamedAgainByACellDeliveredInsideTheWaitIsNotGivenBack)
        {
            const Placed near{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 1, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            const Placed far{ .mCell = osg::Vec2i(9, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 2, 0 },
                .mPosition = osg::Vec3f(9.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            mStorage.mPlaced = { near, far };
            start();

            EXPECT_EQ(fill().mDistantStatics, 1u) << "the near tree, on the one mesh the model has";
            const std::size_t meshes = mScene.meshes().getLiveCount();

            // The eye leaves for a cell from which the near tree's cell is out of the band and the
            // far tree's is in it: the walks that follow let the model go and take it up again.
            around(osg::Vec3f(10.5f * sCellSize, 0.5f * sCellSize, 0.0f), osg::Vec4i(9, -1, 12, 2));
            EXPECT_EQ(fill().mDistantStatics, 0u) << "the far tree stands in the active grid now";
            around(osg::Vec4i(11, -1, 14, 2));
            EXPECT_EQ(walk(mWalked++).mDistantStatics, 1u) << "and outside it, on the model the ring kept";

            // The sweep takes the ground of the band that left, and nothing else: the tree's mesh
            // was stamped through, so the scene holds the new band's ground and that one mesh. The
            // old disc and the new share five cells of column 5, which both eyes stand 4.5 cells
            // from: `4.5² + ay² < 5²` holds for a row's `ay` of 0, 0.5 and 1.5, which is `|dy| <=
            // 2`, and fails at 2.5.
            EXPECT_EQ(mExtractor.retire().mMeshes, sPreparedCells - 5u);
            EXPECT_EQ(mScene.meshes().getLiveCount(), meshes) << "one tree mesh, held throughout";

            // Two more walks, so the thread's give-backs of what was returned have run against
            // the reader's own contract — which a model given back while lent breaks loudly.
            walk(mWalked++);
            walk(mWalked++);
            EXPECT_EQ(mScene.placements().getCounts().mPlaced, 1u + sPlacedCells);
        }

        /// Unsettled, the ring adopts one cell a frame as the thread delivers them, and a frame
        /// walked twice adopts once.
        TEST_F(RtxCellRingTest, unsettledTheRingAdoptsOneCellAFrame)
        {
            const Placed tree{ .mCell = osg::Vec2i(3, 0),
                .mModel = "tree.nif",
                .mRefNum = ESM::RefNum{ 1, 0 },
                .mPosition = osg::Vec3f(3.5f * sCellSize, 0.5f * sCellSize, 0.0f) };
            mStorage.mPlaced = { tree };
            mRing.setSettled(false);
            start();

            // A hundred and one cells to prepare, one adopted a frame: the tree stands somewhere
            // inside the first hundred and one frames, and never before its cell arrived.
            std::size_t frame = 1;
            std::uint32_t statics = 0;
            for (; frame <= 400 && statics == 0; ++frame)
            {
                statics = walk(frame).mDistantStatics;
                walk(frame);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            EXPECT_EQ(statics, 1u);
            EXPECT_LE(mRing.getHeldCellCount(), frame) << "one cell a frame, and a frame walked twice adopts once";
        }

        /// **A recycled cell whose new land stands nothing stands no ground.** An ESM4 world leaves a
        /// cell with no land out rather than laying a plane, and the cell that takes a dropped
        /// cell's place kept that cell's ground: it stood rows it no longer held and dropped a
        /// mesh of nothing.
        TEST_F(RtxCellRingTest, aRecycledCellWhoseLandStandsNothingStandsNoGround)
        {
            mLand.mWithData.clear();
            for (int x = -6; x <= 6; ++x)
                for (int y = -6; y <= 6; ++y)
                    mLand.mWithData.emplace_back(x, y);
            start(ESM::RefId::stringRefId("tamriel"));

            ExtractionStats stood;
            for (std::size_t walks = 0; walks < 2 * sPreparedCells; ++walks)
                stood = walk(mWalked++);
            ASSERT_GT(stood.mGroundCells, 0u);

            // Far from every cell with land: every cell dropped, and every cell adopted in their
            // place stands nothing.
            around(osg::Vec3f(60.5f * sCellSize, 60.5f * sCellSize, 0.0f), osg::Vec4i(59, 59, 62, 62));
            ExtractionStats away;
            for (std::size_t walks = 0; walks < 2 * sPreparedCells; ++walks)
                away = walk(mWalked++);

            EXPECT_EQ(away.mGroundCells, 0u) << "a recycled cell stood the ground of the cell it replaced";
            EXPECT_EQ(placed(), 0u);
        }

        /// Settled, a walk adopts the one cell an unsettled walk does and waits for it.
        ///
        /// **No sleep anywhere here, and that is the whole claim.** The test above has to wait on
        /// the wall, because an unsettled walk that finds nothing read adopts nothing. A settled
        /// walk waits for the cell it is about to adopt, so the count after N walks is exactly N.
        ///
        /// **And exactly N and never more**: waiting for the whole band and adopting all of it puts a
        /// hundred cells on one frame.
        TEST_F(RtxCellRingTest, settledAWalkWaitsForItsOneCellAndTakesNoMore)
        {
            start();

            for (std::size_t walked = 1; walked <= 20; ++walked)
            {
                walk(mWalked++);
                EXPECT_EQ(mRing.getHeldCellCount(), walked) << "a settled walk adopts one cell and waits for it";
            }

            // And a frame walked twice adopts once, which is the rule both ways.
            const std::size_t held = mRing.getHeldCellCount();
            const std::size_t frame = mWalked++;
            walk(frame);
            walk(frame);
            EXPECT_EQ(mRing.getHeldCellCount(), held + 1);
        }

        /// **A reader that throws ends the process where it threw**, and says what it threw.
        /// Nothing catches it on the reader's thread, so the crash catcher's report keeps that
        /// thread's stack; a catch that carried it to the frame handed over the message alone. The
        /// first walk asks and waits, and the wait is where the process ends.
        TEST_F(RtxCellRingTest, aReaderThatThrowsEndsTheProcessNamingWhatItThrew)
        {
            mStorage.mThrows = true;
            expectDies(
                [&] {
                    start();
                    walk(mWalked++);
                },
                "a storage that cannot be read");
        }

        /// **A walk that was not told where the world is dies where it happens**, rather than
        /// standing last frame's rings under this frame's eye.
        TEST_F(RtxCellRingTest, aWalkThatWasNotToldWhereTheWorldIsDies)
        {
            start();
            walk(mWalked++);
            expectAssertDies([&] { mExtractor.extractWorld(*mEmpty, osg::Matrixf::identity(), 0, mWalked, mRing); },
                "a call out of its turn");
        }

        /// Content whose one template is a morph the reader refuses: three vertices over a base
        /// target of two, which `MeshReader::read` throws on.
        class ShortMorph final : public ContentSource
        {
        public:
            osg::ref_ptr<const osg::Node> getTemplate(VFS::Path::NormalizedView) override { return mFace; }
            Result<osg::ref_ptr<const osg::Image>, std::string> getImage(VFS::Path::NormalizedView) override
            {
                return Err{ "no image reads from the file" };
            }

            osg::ref_ptr<osg::Group> mFace = [] {
                osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
                source->setVertexArray(makePositions({ sUnitTriangle[0], sUnitTriangle[1], sUnitTriangle[2] }));
                source->addPrimitiveSet(makeTriangles({ 0, 1, 2 }));

                osg::ref_ptr<SceneUtil::MorphGeometry> morph = new SceneUtil::MorphGeometry;
                morph->setSourceGeometry(source);
                morph->addMorphTarget(new osg::Vec3Array(2), 1.0f);
                morph->addMorphTarget(new osg::Vec3Array(3), 0.0f);

                osg::ref_ptr<osg::Group> root = new osg::Group;
                root->addChild(morph);
                return root;
            }();
        };

        /// A record's model path is built once, however many references name it and however often
        /// the cell is read.
        ///
        /// **Two strings a static reference, on every read of every cell**, is what asking the
        /// storage and correcting the path came to: a town is a few hundred references to a few
        /// dozen models, and a ring reads a band of cells at every crossing. Three references to one
        /// record, read three times: asked once.
        TEST(RtxCellReaderTest, aRecordsModelPathIsBuiltOnceWhateverNamesIt)
        {
            FakeLand land;
            FewStatics storage;
            for (std::uint32_t at = 0; at < 3; ++at)
                storage.mPlaced.push_back(
                    Placed{ .mCell = osg::Vec2i(0, 0), .mModel = "face", .mRefNum = ESM::RefNum{ at, 0 } });
            ShortMorph content;

            CellReader reader(storage, land, content, ESM::Cell::sDefaultWorldspaceId, ~0u);
            for (int pass = 0; pass < 3; ++pass)
                reader.giveBack(reader.read(osg::Vec2i(0, 0), true));

            EXPECT_EQ(storage.mModelsAsked, 1) << "the storage was asked for a record's model more than once";
        }

        /// **A layer's file is opened once for as long as a cell holds it**, and not once a cell:
        /// every cell of a band wears the same few ground textures, and an open is a lock and a
        /// read. Two cells of grass and rock are three files; a third file would be the second
        /// cell's own. The rock's `_nh` carries a height, which is its format's to say, and the
        /// grass asks for none. Given back and read again, a file that no longer reads keeps its
        /// place and its path, for the texture table to stand in and refuse.
        TEST(RtxCellReaderTest, aLayersFileIsOpenedOnceWhileACellHoldsIt)
        {
            FakeLand land;
            land.mWithData = { osg::Vec2i(0, 0), osg::Vec2i(1, 0) };
            FewStatics storage;
            FewContent content;
            CellReader reader(storage, land, content, ESM::Cell::sDefaultWorldspaceId, ~0u);

            PreparedCell& west = reader.read(osg::Vec2i(0, 0), true);
            EXPECT_EQ(content.mImages.getOpened(), 3u) << "grass, rock and the rock's normal map";
            PreparedCell& east = reader.read(osg::Vec2i(1, 0), true);
            EXPECT_EQ(content.mImages.getOpened(), 3u) << "a file opened again while a cell held it";

            ASSERT_EQ(east.mGround.mLayers.size(), 2u);
            const PreparedLayer& grass = east.mGround.mLayers[0];
            const PreparedLayer& rock = east.mGround.mLayers[1];
            EXPECT_EQ(grass.mTexture, west.mGround.mLayers[0].mTexture) << "one reading of one file";
            ASSERT_NE(rock.mNormalTexture, nullptr);
            ASSERT_NE(rock.mNormalTexture->mImage, nullptr);
            EXPECT_EQ(rock.mNormalTexture->mImage->getFileName(), "textures/rock_nh.dds");
            EXPECT_TRUE(rock.mParallax) << "an `_nh` of four channels carries a height";
            EXPECT_FALSE(grass.mParallax);
            EXPECT_EQ(grass.mNormalTexture, nullptr);

            // What a cell holds is the one list `collectTextures` derives: each layer's diffuse,
            // then its normal map.
            std::vector<PreparedTexture*> held;
            for (PreparedCell* cell : { &west, &east })
            {
                held.clear();
                cell->mGround.collectTextures(held);
                ASSERT_EQ(held.size(), 3u);
                EXPECT_EQ(held[0]->mPath, "textures/grass.dds");
                EXPECT_EQ(held[1]->mPath, "textures/rock_diffusespec.dds");
                EXPECT_EQ(held[2]->mPath, "textures/rock_nh.dds");
                for (PreparedTexture* texture : held)
                    reader.giveBack(*texture);
                reader.giveBack(*cell);
            }

            content.mImages.lose("textures/grass.dds");
            PreparedCell& again = reader.read(osg::Vec2i(0, 0), true);
            EXPECT_EQ(content.mImages.getOpened(), 6u) << "a reading given back was kept";
            ASSERT_EQ(again.mGround.mLayers.size(), 2u);
            EXPECT_EQ(again.mGround.mLayers[0].mTexture->mImage, nullptr);
            EXPECT_EQ(again.mGround.mLayers[0].mTexture->mPath, "textures/grass.dds");
        }

        /// Content whose first image is held until the test lets it go, which holds the reading
        /// thread inside the first cell of its list.
        class HeldContent final : public ContentSource
        {
        public:
            osg::ref_ptr<const osg::Node> getTemplate(VFS::Path::NormalizedView) override { return nullptr; }

            Result<osg::ref_ptr<const osg::Image>, std::string> getImage(const VFS::Path::NormalizedView path) override
            {
                if (!mHeldOnce)
                {
                    mHeldOnce = true;
                    mEntered.release();
                    mLetGo.acquire();
                }
                return mImages.get(path);
            }

            ImagesByPath mImages;
            std::binary_semaphore mEntered{ 0 };
            std::binary_semaphore mLetGo{ 0 };

            /// The reading thread's alone.
            bool mHeldOnce = false;
        };

        /// **An ask for nothing cancels the list in flight**, as a newer list does: the eye has
        /// moved and nothing it lacked is lacked now. It left the list to be read to its end,
        /// because what said "newer" was a list that was not empty. The thread is held inside the
        /// first of three cells while the empty ask is made, and the next cell it reads is the one
        /// asked after.
        TEST(RtxCellSupplyTest, anAskForNothingCancelsTheListInFlight)
        {
            FakeLand land;
            FewStatics storage;
            HeldContent content;
            CellSupply supply;
            supply.follow(CellWorld{
                .mStorage = &storage,
                .mGround = &land,
                .mContent = &content,
                .mWorldspace = ESM::Cell::sDefaultWorldspaceId,
                .mMask = ~0u,
            });

            supply.ask(CellRequest{ .mCells = { osg::Vec2i(0, 0), osg::Vec2i(1, 0), osg::Vec2i(2, 0) } });
            content.mEntered.acquire();
            supply.ask(CellRequest{});
            content.mLetGo.release();

            std::vector<PreparedCell*> read;
            supply.waitForOne();
            supply.take(read);
            ASSERT_EQ(read.size(), 1u);
            EXPECT_EQ(read[0]->mCell, osg::Vec2i(0, 0)) << "the cell in hand is finished";

            supply.ask(CellRequest{ .mCells = { osg::Vec2i(5, 0) } });
            while (std::none_of(
                read.begin(), read.end(), [](const PreparedCell* cell) { return cell->mCell == osg::Vec2i(5, 0); }))
            {
                supply.waitForOne();
                supply.take(read);
            }

            ASSERT_EQ(read.size(), 2u) << "a cell of the cancelled list was read";
            EXPECT_EQ(read[1]->mCell, osg::Vec2i(5, 0));
        }

        /// **A model's room is counted where it stands: lent to a cell, then spare, never both.**
        /// `reuse` keeps the buffers, so the room the tree grew to leaves the lent figure and
        /// reappears whole in the spare one when its one holder gives it back.
        /// **A spare keeps the room its last model filled at least half of, and gives back the
        /// rest.** Hand-counted on the positions: a hundred reserved and a hundred held is kept; fifty
        /// in the same hundred is exactly half and kept; forty is less than half and given back,
        /// which is what stops a spare keeping the largest model it ever held.
        TEST(RtxPreparedModelTest, aSpareKeepsItsRoomOnlyWhereItsModelFilledHalfOfIt)
        {
            PreparedModel model;
            model.mPositions.reserve(100);

            for (const auto [held, kept] : { std::pair{ 100u, 100u }, std::pair{ 50u, 100u }, std::pair{ 40u, 0u } })
            {
                model.mPositions.resize(held);
                ASSERT_EQ(model.mPositions.capacity(), 100u) << "the room this starts from";
                model.reuse();
                EXPECT_TRUE(model.mPositions.empty());
                EXPECT_EQ(model.mPositions.capacity(), kept) << held << " of a hundred";
                if (kept == 0)
                    break;
            }
        }

        TEST(RtxCellReaderTest, aModelsRoomIsCountedLentThenSpareAndNeverBoth)
        {
            FakeLand land;
            FewStatics storage;
            storage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(0, 0), .mModel = "tree.nif" });
            FewContent content;
            CellReader reader(storage, land, content, ESM::Cell::sDefaultWorldspaceId, ~0u);

            const ReaderMemory none = reader.measure();
            EXPECT_EQ(none.mLentModels + none.mSpareModels, 0u);
            EXPECT_EQ(none.mLentBytes + none.mSpareBytes, 0u);

            PreparedCell& cell = reader.read(osg::Vec2i(0, 0), true);
            ASSERT_EQ(cell.mModels.size(), 1u);
            PreparedModel& tree = *cell.mModels.front();
            ASSERT_FALSE(tree.mPositions.empty()) << "the sheet was read";

            const ReaderMemory lent = reader.measure();
            EXPECT_EQ(lent.mLentModels, 1u);
            EXPECT_EQ(lent.mSpareModels, 0u);
            EXPECT_EQ(lent.mLentBytes, tree.getRoomBytes());
            EXPECT_EQ(lent.mSpareBytes, 0u);

            reader.giveBack(tree);
            reader.giveBack(cell);

            const ReaderMemory spare = reader.measure();
            EXPECT_EQ(spare.mLentModels, 0u);
            EXPECT_EQ(spare.mSpareModels, 1u);
            EXPECT_EQ(spare.mLentBytes, 0u);
            EXPECT_EQ(spare.mSpareBytes, lent.mLentBytes) << "the spare keeps the room the lent model grew to";
        }

        /// **A model the walk refuses costs the reader nothing it keeps.** The reference is left
        /// out and named, as `CellReader::read` promises, and the spare the attempt was made in
        /// goes back to the pool: read again, the cell takes no second spare and the template is
        /// held by nothing. Before `Spares::take` took the fill, every read of a cell naming such a
        /// model lost one spare, each holding the template.
        TEST(RtxCellReaderTest, aModelTheWalkRefusesLeavesNoSpareTakenAndNoTemplateHeld)
        {
            FakeLand land;
            FewStatics storage;
            storage.mPlaced.push_back(Placed{ .mCell = osg::Vec2i(0, 0), .mModel = "face" });
            ShortMorph content;

            CellReader reader(storage, land, content, ESM::Cell::sDefaultWorldspaceId, ~0u);

            const int held = content.mFace->referenceCount();
            for (int pass = 0; pass < 3; ++pass)
            {
                PreparedCell& cell = reader.read(osg::Vec2i(0, 0), true);
                EXPECT_TRUE(cell.mModels.empty()) << "a model the walk refused was named";
                EXPECT_TRUE(cell.mRefs.empty());
                reader.giveBack(cell);

                EXPECT_EQ(content.mFace->referenceCount(), held)
                    << "read " << pass + 1 << " kept a hold on the template";
            }
        }
    }
}
