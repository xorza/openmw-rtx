#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Group>
#include <osg/Matrixf>

#include <gtest/gtest.h>

#include <boost/program_options/variables_map.hpp>

#include <components/debug/debugging.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/sceneuploader.hpp>

#include <apps/rtxtool/cellscene.hpp>
#include <apps/rtxtool/world.hpp>

#include "../rtxbridge/countingrenderer.hpp"
#include "installation.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        /// Two exterior cells side by side in the middle of a town, so the grid that follows the
        /// camera keeps three columns and swaps three for three.
        constexpr std::string_view sFrom = "-3,-2";
        constexpr std::string_view sTo = "-2,-2";

        /// Brings the ring around `centre` in, takes the ones that left off, and mirrors what is
        /// left — which is what `runWindow`'s `bring` does, in the same order and for the same
        /// reasons.
        std::uint32_t crossTo(World& world, const ESM::Cell& centre, osg::Group& root, LoadedCells& loaded,
            RtxBridge::SceneExtractor& extractor)
        {
            readRegion(world, centre, root, loaded, /*liveProps=*/false);
            const std::uint32_t went = dropCellsOutside(world, centre, root, loaded);

            extractor.extract(root, osg::Matrixf::identity(), 0);
            extractor.advance();

            if (went > 0)
                extractor.retire();

            return went;
        }

        /// What a crossing actually costs the renderer, which is the question the harness exists to
        /// put a number on.
        ///
        /// **It appends, and that was not the expected answer.** Walking one cell east drops three
        /// columns as it gains three, and dropping a cell was supposed to compact the tables and
        /// force a full rebuild. It does not, because a town is built out of a few dozen models
        /// placed hundreds of times: the resource cache hands the same nodes to every cell, so the
        /// three columns that left took no mesh with them that the six that stayed were not still
        /// using. `retain` finds every mesh still met, drops nothing, and renumbers nothing — so the
        /// arrival is a growth and `extendScene` is the honest answer to it.
        ///
        /// Measured here at Balmora: 1,397 meshes to 1,665, and 50 textures described where a
        /// rebuild would have decoded and shading-estimated all 231 again.
        TEST(RtxCrossingTest, walkingIntoTheNextCellAppendsBecauseATownSharesItsModels)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* from = world->findCell(sFrom);
            const ESM::Cell* to = world->findCell(sTo);
            ASSERT_NE(from, nullptr);
            ASSERT_NE(to, nullptr);

            const osg::ref_ptr<osg::Group> root = new osg::Group;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            RtxBridge::SceneUploader uploader;
            RtxBridge::Testing::CountingRenderer renderer;
            LoadedCells loaded;

            EXPECT_EQ(crossTo(*world, *from, *root, loaded, extractor), 0u) << "nothing was loaded to leave yet";

            // The first ring has nothing to append to, so it builds whatever it found.
            const RtxBridge::SceneUpload first
                = uploader.hand(renderer, scene, world->getImageManager(), Rtx::SeaState{});
            ASSERT_EQ(first.mKind, RtxBridge::SceneUpload::Kind::Rebuilt);
            ASSERT_GT(scene.getPlacedCount(), std::size_t{ 0 }) << "the ring placed no geometry";

            // Standing still is the ordinary frame: the walk finds everything where it was.
            extractor.extract(*root, osg::Matrixf::identity(), 0);
            extractor.advance();
            EXPECT_EQ(uploader.hand(renderer, scene, world->getImageManager(), Rtx::SeaState{}).mKind,
                RtxBridge::SceneUpload::Kind::Placed);

            const std::size_t meshesBefore = scene.getMeshes().size();
            const std::size_t texturesBefore = scene.getTextures().size();

            EXPECT_EQ(crossTo(*world, *to, *root, loaded, extractor), 3u)
                << "a step of one cell east leaves the three columns behind it";

            const RtxBridge::SceneUpload crossed
                = uploader.hand(renderer, scene, world->getImageManager(), Rtx::SeaState{});
            EXPECT_EQ(crossed.mKind, RtxBridge::SceneUpload::Kind::Extended);
            EXPECT_GT(scene.getMeshes().size(), meshesBefore) << "three cells arrived and brought no geometry";

            // **Exactly the arrivals, which is the whole saving.** Anything else means the offset
            // the descriptions began at disagreed with where the renderer's array ends, and the
            // difference is every image in the region decoded and shading-estimated a second time.
            EXPECT_EQ(crossed.mDescribed, scene.getTextures().size() - texturesBefore);
            EXPECT_LT(crossed.mDescribed, scene.getTextures().size()) << "the crossing described the whole table";

            // The array ends where the scene's table does, so no material names the wrong image.
            EXPECT_EQ(renderer.mTextures, scene.getTextures().size());
            EXPECT_FALSE(renderer.mAppendedToWrongEnd);
            EXPECT_EQ(renderer.mRebuilt, 1u) << "the crossing cost a full build after all";
        }

        /// Walking a long way leaves a working set the size of the grid, not the size of the walk.
        ///
        /// **The instrument for the streaming bench.** A camera flown across the island crosses
        /// twenty boundaries, and if each one only adds then what is being measured after the fifth
        /// is a world no player ever holds. This walks a straight line of cells and asserts the
        /// count settles instead of climbing.
        TEST(RtxCrossingTest, walkingAcrossManyCellsHoldsAGridRatherThanEverythingVisited)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const osg::ref_ptr<osg::Group> root = new osg::Group;

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            LoadedCells loaded;

            std::size_t afterFirstStep = 0;
            std::size_t placed = 0;

            // North out of Balmora, one cell at a time, over land the whole way.
            for (int y = -2; y <= 6; ++y)
            {
                const ESM::Cell* cell = world->findCell("-3," + std::to_string(y));
                ASSERT_NE(cell, nullptr) << "no cell at -3," << y;

                crossTo(*world, *cell, *root, loaded, extractor);
                placed = scene.getPlacedCount();

                if (y == -1)
                    afterFirstStep = placed;

                out() << "  at -3," << y << ": " << placed << " placed, " << scene.getMeshes().size() << " meshes\n";
            }

            ASSERT_GT(afterFirstStep, std::size_t{ 0 });

            // **Twice the first step's grid, and the room is for content and not for a leak.** Nine
            // cells of ashland hold less than nine of town, so the count moves with where the camera
            // is; what it must not do is carry the eight grids it has already left.
            EXPECT_LT(placed, afterFirstStep * 2)
                << "the working set grew with the walk rather than with what is around the camera";
        }
    }
}
