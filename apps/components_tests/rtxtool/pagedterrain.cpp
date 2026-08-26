#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <osg/Group>
#include <osg/Matrixf>
#include <osg/Vec3f>

#include <gtest/gtest.h>

#include <boost/program_options/variables_map.hpp>

#include <components/esm3/loadcell.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/sceneextractor.hpp>

#include <apps/rtxtool/cellscene.hpp>
#include <apps/rtxtool/world.hpp>

#include "installation.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// An exterior with a lot of open ground in it, so the chunks are most of what is placed.
        constexpr std::string_view sOutdoors = "-2,-9";

        /// One cell across, in world units.
        constexpr float sCellSize = static_cast<float>(Constants::CellSizeInUnits);

        /// Places one cell's worth of world into `scene`, with or without the residency asked for.
        void placeOutdoors(World& world, const ESM::Cell& cell, Rtx::SceneDesc& scene, bool ask)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            LoadedCells loaded;
            Rtx::SceneExtractor extractor(scene);

            readRegion(world, cell, *root, scene, extractor, loaded, /*liveProps=*/false);
            world.setTerrainViewPoint(osg::Vec3f(cell.getGridX() * sCellSize, cell.getGridY() * sCellSize, 0.0f));

            extractor.extract(*root, osg::Matrixf::identity(), 0, 0, ask ? world.getTerrainResidency() : nullptr);
        }

        /// What one placement came to, for the things a run of this file compares between two.
        struct GroundTally
        {
            /// Placed, and pointing at no material at all. Not terrain's alone — a drawable with no
            /// state set anywhere on its path resolves to nothing either, which is why this is
            /// compared between two runs rather than expected to be nought.
            std::uint32_t mMaterialless = 0;

            std::uint32_t mChunks = 0;

            /// The widest chunk, in world units.
            float mWidest = 0.0f;
        };

        /// How far a mesh reaches along its widest horizontal axis, in the units it was built in.
        ///
        /// **What says a chunk is bigger than a cell**, which the scene carries nowhere else: a
        /// terrain chunk is placed by a translation, so its own vertices span exactly the ground it
        /// covers.
        float spanOf(const Rtx::SceneDesc& scene, Rtx::Index mesh)
        {
            const std::span<const osg::Vec3f> positions = scene.getMeshPositions(mesh);
            if (positions.empty())
                return 0.0f;

            osg::Vec3f least = positions.front();
            osg::Vec3f most = positions.front();
            for (const osg::Vec3f& at : positions)
            {
                least.x() = std::min(least.x(), at.x());
                least.y() = std::min(least.y(), at.y());
                most.x() = std::max(most.x(), at.x());
                most.y() = std::max(most.y(), at.y());
            }

            return std::max(most.x() - least.x(), most.y() - least.y());
        }

        GroundTally tallyGround(const Rtx::SceneDesc& scene)
        {
            GroundTally tally;

            for (const Rtx::MeshInstance& instance : scene.getInstances())
            {
                if (!instance.isPlaced())
                    continue;

                if (instance.mMaterial == Rtx::sNoIndex)
                {
                    ++tally.mMaterialless;
                    continue;
                }

                const Rtx::Material& material = scene.getMaterials()[instance.mMaterial];
                if (material.mKind != Rtx::MaterialKind::Terrain)
                    continue;

                ++tally.mChunks;
                tally.mWidest = std::max(tally.mWidest, spanOf(scene, instance.mMesh));
            }

            return tally;
        }

        /// A paged world's ground reaches the mirror, and only because it was asked for.
        ///
        /// **`Terrain::QuadTreeWorld` keeps its chunks out of the scene graph.** It resolves them
        /// inside a cull, against a view keyed on the camera doing the culling, and parents them to
        /// nothing — so with `distant terrain` on, everything that walks the graph rather than
        /// culling it saw no ground, no paged objects and no grass. `Terrain::World::collect` is the
        /// way to ask instead, and this is what says it works.
        ///
        /// **Two runs of one cell, differing in nothing but whether the residency is handed over.**
        /// A count that went up for any other reason would show up as the control placing the same
        /// number, which is what the second assertion is for.
        TEST(RtxPagedTerrainTest, aPagedWorldsGroundReachesTheMirrorAndOnlyBecauseItWasAskedFor)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* cell = world->findCell(std::string(sOutdoors));
            ASSERT_NE(cell, nullptr);

            world->pageTerrain(true);

            Rtx::SceneDesc asked;
            placeOutdoors(*world, *cell, asked, true);
            ASSERT_NE(world->getTerrainResidency(), nullptr) << "the run did not page its terrain";

            Rtx::SceneDesc unasked;
            placeOutdoors(*world, *cell, unasked, false);

            EXPECT_GT(asked.getPlacedCount(), unasked.getPlacedCount())
                << "the chunks a quad tree hides never reached the mirror";
        }

        /// A world that parents its chunks offers no residency, and is reached by walking.
        ///
        /// **The other half of the same rule**, and what stops the ground being placed twice: a
        /// caller that both walked the graph and asked every terrain world it knew would count
        /// `TerrainGrid`'s chunks once each way.
        TEST(RtxPagedTerrainTest, aGridWorldOffersNoResidencyBecauseItsChunksAreInTheGraph)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* cell = world->findCell(std::string(sOutdoors));
            ASSERT_NE(cell, nullptr);

            Rtx::SceneDesc scene;
            placeOutdoors(*world, *cell, scene, true);

            EXPECT_EQ(world->getTerrainResidency(), nullptr);
            EXPECT_GT(scene.getPlacedCount(), 0u) << "a grid world's ground is found by walking, and was not";
        }

        /// No chunk is textured by a render target, however large the quad tree makes it.
        ///
        /// **A composite map is an `osg::Texture2D` with no image** that `CompositeMapRenderer` draws
        /// the layer stack into through OpenGL, and every chunk a cell or larger asks for one. This
        /// path initialises no OpenGL at all, so a chunk that got one arrives with a diffuse nothing
        /// can open: `resolveTerrainMaterial` finds no layer it can use, gives up, and the chunk is
        /// placed carrying `sNoIndex` where its material should be. `Terrain::sNoCompositeMap` is
        /// what stops it being asked for, and this is what says so.
        ///
        /// **Two runs of one cell differing only in how far out the quad tree may go**, because a
        /// placement with nothing to point at is not terrain's alone: a drawable with no state set
        /// anywhere on its path resolves to nothing too, and asserting nought outright would blame a
        /// render target for one of those. Distance is what may not add to the count.
        ///
        /// **The premise is asserted beside the conclusion**, and it is not the one this was written
        /// expecting. A cell staged on its own has an active grid one cell wide, which `viewing
        /// distance` of 7168 leaves at once — so the control already reaches a chunk a whole cell
        /// across, which is exactly the size a composite map is made for. Unforced, that run alone
        /// loses five placements their material and the four-cell run loses thirty-two.
        TEST(RtxPagedTerrainTest, noChunkIsTexturedByARenderTargetHoweverLargeItIs)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* cell = world->findCell(std::string(sOutdoors));
            ASSERT_NE(cell, nullptr);

            world->pageTerrain(true);

            Rtx::SceneDesc grid;
            placeOutdoors(*world, *cell, grid, true);
            const GroundTally near = tallyGround(grid);

            world->setTerrainViewDistance(4.0f * sCellSize);

            Rtx::SceneDesc paged;
            placeOutdoors(*world, *cell, paged, true);
            const GroundTally far = tallyGround(paged);

            EXPECT_GT(far.mChunks, near.mChunks) << "four cells of distance produced no more ground than none";
            EXPECT_GE(near.mWidest, sCellSize) << "the control made no chunk a composite was ever on offer for";
            EXPECT_GT(far.mWidest, near.mWidest) << "distance made nothing coarser than the grid already had";

            EXPECT_EQ(far.mMaterialless, near.mMaterialless)
                << "distance cost a placement its material, which is what a render target does";

            for (const Rtx::MeshInstance& instance : paged.getInstances())
            {
                if (!instance.isPlaced() || instance.mMaterial == Rtx::sNoIndex)
                    continue;

                const Rtx::Material& material = paged.getMaterials()[instance.mMaterial];
                if (material.mKind != Rtx::MaterialKind::Terrain)
                    continue;

                ASSERT_GT(material.mLayerCount, 0u) << "ground with nothing to draw it with";
                for (std::uint32_t at = 0; at < material.mLayerCount; ++at)
                {
                    const Rtx::MaterialLayer& layer = paged.getLayers()[material.mLayerOffset + at];

                    ASSERT_NE(layer.mDiffuse, Rtx::sNoIndex);
                    EXPECT_FALSE(paged.getTextures()[layer.mDiffuse].value().empty())
                        << "a ground layer whose diffuse came from no file";
                }
            }
        }
    }
}
