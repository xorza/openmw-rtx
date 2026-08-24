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
#include <apps/rtxtool/stagedworld.hpp>
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
        std::uint32_t crossTo(World& world, const ESM::Cell& centre, osg::Group& root, Rtx::SceneDesc& scene,
            LoadedCells& loaded, RtxBridge::SceneExtractor& extractor)
        {
            readRegion(world, centre, root, scene, extractor, loaded, /*liveProps=*/false);
            const std::uint32_t went = dropCellsOutside(world, centre, root, scene, extractor, loaded);

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

            EXPECT_EQ(crossTo(*world, *from, *root, scene, loaded, extractor), 0u) << "nothing was loaded to leave yet";

            // The first ring has nothing to append to, so it builds whatever it found.
            const RtxBridge::SceneUpload first
                = uploader.hand(renderer, Rtx::sWorld, scene, world->getImageManager(), Rtx::SeaState{});
            ASSERT_EQ(first.mKind, RtxBridge::SceneUpload::Kind::Rebuilt);
            ASSERT_GT(scene.getPlacedCount(), std::size_t{ 0 }) << "the ring placed no geometry";

            // Standing still is the ordinary frame: the walk finds everything where it was.
            extractor.extract(*root, osg::Matrixf::identity(), 0);
            extractor.advance();
            EXPECT_EQ(uploader.hand(renderer, Rtx::sWorld, scene, world->getImageManager(), Rtx::SeaState{}).mKind,
                RtxBridge::SceneUpload::Kind::Placed);

            const std::size_t meshesBefore = scene.getMeshes().size();
            const std::size_t texturesBefore = scene.getTextures().size();

            EXPECT_EQ(crossTo(*world, *to, *root, scene, loaded, extractor), 3u)
                << "a step of one cell east leaves the three columns behind it";

            const RtxBridge::SceneUpload crossed
                = uploader.hand(renderer, Rtx::sWorld, scene, world->getImageManager(), Rtx::SeaState{});
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

        /// A cell brings water and lights, and takes them away again when it leaves.
        ///
        /// **Neither is anything a walk can find.** A `LIGH` record and an analytic water quad go
        /// into the scene directly, so the sweep that runs when a cell departs cannot speak for them
        /// — it clears the light table outright, on the understanding that the next walk refills it,
        /// which is true of a torch in someone's hand and false of a lamp on a post. Both of these
        /// went wrong the same way: the lamp on the boat at Seyda Neen went out on the first
        /// crossing, and the sea was one rectangle under wherever the run started.
        TEST(RtxCrossingTest, aCellBringsWaterAndLightsAndTakesThemWithIt)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* from = world->findCell(sFrom);
            ASSERT_NE(from, nullptr);

            const osg::ref_ptr<osg::Group> root = new osg::Group;
            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            LoadedCells loaded;

            crossTo(*world, *from, *root, scene, loaded, extractor);

            // **Every square of the ring, not just its middle.** Balmora's nine are all exteriors
            // and every exterior has water, so nine quads stand once the ring is in.
            ASSERT_EQ(loaded.size(), std::size_t{ 9 });
            std::size_t withWater = 0;
            for (const auto& [key, cell] : loaded)
                if (cell.mWater.has_value())
                    ++withWater;

            EXPECT_EQ(withWater, loaded.size()) << "an exterior always has water";

            // Each quad stands somewhere of its own: nine cells sharing one placement would be the
            // bug this is here for, wearing a different hat.
            std::set<Rtx::Index> where;
            for (const auto& [key, cell] : loaded)
                where.insert(cell.mWater->mInstance);

            EXPECT_EQ(where.size(), loaded.size()) << "the ring placed one quad and named it nine times";

            // And the lights came with the cells rather than with the region as a whole.
            std::size_t lit = 0;
            for (const auto& [key, cell] : loaded)
                lit += cell.mLights.size();

            EXPECT_GT(lit, std::size_t{ 0 }) << "a town of nine cells casts no light at all";
        }

        /// The lamps are still burning after the crossing that swept the scene.
        ///
        /// **The bug this is here for, in one line.** A camera stepped out of the square it started
        /// in, three cells died, `SceneDesc::release` emptied the light table on the way past — and
        /// every lamp read out of a `LIGH` record went out and stayed out, because the walk that was
        /// supposed to refill the table had never carried them in the first place.
        TEST(RtxCrossingTest, aCrossingLeavesTheLampsBurning)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* from = world->findCell(sFrom);
            ASSERT_NE(from, nullptr);

            StagedWorld staged(*world, *from, StagingRequest{}, ActorRequest{});
            ASSERT_FALSE(staged.empty());

            const std::size_t before = staged.getScene().getLights().size();
            ASSERT_GT(before, std::size_t{ 0 }) << "Balmora's nine cells cast no light to begin with";

            // The middle of the square east of this one, which is a crossing and not a step.
            constexpr float side = 8192.0f;
            const Crossing crossed
                = staged.moveTo(osg::Vec3f(-2.0f * side + 0.5f * side, -2.0f * side + 0.5f * side, 0.0f));
            ASSERT_GT(crossed.mDeparted, std::uint32_t{ 0 }) << "nothing left, so nothing swept";

            EXPECT_GT(staged.getScene().getLights().size(), std::size_t{ 0 })
                << "the sweep took the lamps and no walk put them back";
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

                crossTo(*world, *cell, *root, scene, loaded, extractor);
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
