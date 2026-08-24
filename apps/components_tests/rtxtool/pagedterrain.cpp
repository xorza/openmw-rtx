#include <memory>
#include <string>
#include <string_view>

#include <osg/Group>
#include <osg/Matrixf>

#include <gtest/gtest.h>

#include <boost/program_options/variables_map.hpp>

#include <components/esm3/loadcell.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

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

        /// What one cell's worth of world comes to, with or without the residency asked for.
        std::uint32_t placeOutdoors(World& world, const ESM::Cell& cell, bool ask)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            LoadedCells loaded;
            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);

            readRegion(world, cell, *root, scene, extractor, loaded, /*liveProps=*/false);
            world.setTerrainViewPoint(osg::Vec3f(cell.getGridX() * 8192.0f, cell.getGridY() * 8192.0f, 0.0f));

            extractor.extract(*root, osg::Matrixf::identity(), 0, 0, ask ? world.getTerrainResidency() : nullptr);
            return scene.getPlacedCount();
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

            const std::uint32_t asked = placeOutdoors(*world, *cell, true);
            ASSERT_NE(world->getTerrainResidency(), nullptr) << "the run did not page its terrain";

            const std::uint32_t unasked = placeOutdoors(*world, *cell, false);

            EXPECT_GT(asked, unasked) << "the chunks a quad tree hides never reached the mirror";
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

            const std::uint32_t placed = placeOutdoors(*world, *cell, true);

            EXPECT_EQ(world->getTerrainResidency(), nullptr);
            EXPECT_GT(placed, 0u) << "a grid world's ground is found by walking, and was not";
        }
    }
}
