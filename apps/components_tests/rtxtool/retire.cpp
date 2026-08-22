#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <osg/Group>

#include <gtest/gtest.h>

#include <boost/program_options/variables_map.hpp>

#include <components/esm3/loadcell.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

#include <apps/rtxtool/cellscene.hpp>
#include <apps/rtxtool/lighting.hpp>
#include <apps/rtxtool/placement.hpp>
#include <apps/rtxtool/world.hpp>

#include "../rtx/harness.hpp"
#include "installation.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// Two interiors far enough apart in the content to share little and near enough in kind to
        /// share something: both are Imperial buildings in the same town, so the same barrels,
        /// tables and lamps stand in each.
        constexpr std::string_view sFirst = "Seyda Neen, Census and Excise Office";
        constexpr std::string_view sSecond = "Seyda Neen, Arrille's Tradehouse";

        /// Builds one room's graph and mirrors it, keeping the graph alive in `kept`.
        ///
        /// **A root per room, and the caller holds them.** Walking a second room's root does not
        /// stamp the first one's placements, which is exactly how a cell goes: the sweep finds them
        /// unmet. And the roots have to outlive the mirror, because it keys its meshes on the nodes
        /// in them and a freed address is one the allocator can hand back holding something else.
        RtxBridge::ExtractionStats readCell(World& world, const ESM::Cell& cell,
            std::vector<osg::ref_ptr<osg::Group>>& kept, RtxBridge::SceneExtractor& extractor)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            kept.push_back(root);

            LoadedCells loaded;
            readRegion(world, cell, *root, loaded, /*liveProps=*/false);
            return extractor.extract(*root, osg::Matrixf::identity(), 0);
        }

        /// A fresh root, kept alive by the caller for as long as the mirror names its nodes.
        osg::ref_ptr<osg::Group> keepRoot(std::vector<osg::ref_ptr<osg::Group>>& kept)
        {
            kept.push_back(new osg::Group);
            return kept.back();
        }

        /// Builds one room into its own root, mirrors it, and hands back what lit it.
        CellLighting loadAndMirror(World& world, const ESM::Cell& cell, std::vector<osg::ref_ptr<osg::Group>>& kept,
            Rtx::SceneDesc& scene, RtxBridge::SceneExtractor& extractor)
        {
            const osg::ref_ptr<osg::Group> root = keepRoot(kept);

            LoadedCells loaded;
            const CellLighting lit = loadRegion(world, cell, *root, scene, extractor, loaded, "Clear", 12.0f, false).mLighting;
            extractor.extract(*root, osg::Matrixf::identity(), 0);

            return lit;
        }

        /// One number per mesh, over the vertices and the triangles it actually holds.
        ///
        /// Sorted into a multiset by the caller, this says whether two scenes are made of the same
        /// geometry whatever order they put it in — which is the one thing a compaction can get
        /// wrong and an offset check cannot see: a range that moved while its offset did not leaves
        /// every count consistent and every mesh wearing its neighbour's triangles.
        std::multiset<std::uint64_t> meshFingerprints(const Rtx::SceneDesc& scene)
        {
            std::multiset<std::uint64_t> prints;
            for (Rtx::Index mesh = 0; mesh < scene.getMeshes().size(); ++mesh)
            {
                std::uint64_t print = 0xcbf29ce484222325ull;
                const auto fold = [&](std::uint32_t word) { print = (print ^ word) * 0x100000001b3ull; };

                for (const osg::Vec3f& vertex : scene.getMeshPositions(mesh))
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        std::uint32_t bits = 0;
                        std::memcpy(&bits, vertex.ptr() + axis, sizeof(bits));
                        fold(bits);
                    }

                for (const std::uint32_t index : scene.getMeshIndices(mesh))
                    fold(index);

                prints.insert(print);
            }

            return prints;
        }

        /// A cell the walk stopped finding leaves the scene, and what the next cell shares with it
        /// stays exactly where it was.
        ///
        /// **Two rooms out of the shipped content, because the interesting case cannot be built by
        /// hand.** A synthetic graph either overlaps completely or not at all; two Imperial
        /// interiors share their barrels and their lamps and nothing else, so the sweep has to drop
        /// one room's geometry while the shared models — met again in the second walk, through the
        /// very pointers the resource cache handed out for the first — carry through the compaction
        /// and go on resolving.
        TEST(RtxRetireTest, aCellTheWalkStoppedFindingLeavesAndWhatIsSharedStays)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* first = world->findCell(std::string(sFirst));
            const ESM::Cell* second = world->findCell(std::string(sSecond));
            ASSERT_NE(first, nullptr);
            ASSERT_NE(second, nullptr);

            std::vector<osg::ref_ptr<osg::Group>> kept;
            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);

            const RtxBridge::ExtractionStats one = readCell(*world, *first, kept, extractor);
            ASSERT_GT(one.mMeshesAdded, 0u);

            const std::size_t held = scene.getMeshes().size();
            const std::size_t vertices = scene.getPositions().size();
            const std::uint64_t built = scene.getStructureRevision();

            // Nothing has gone: the walk that just happened is the epoch everything survives.
            ASSERT_TRUE(extractor.retire().empty());
            EXPECT_EQ(scene.getStructureRevision(), built) << "a sweep that dropped nothing must not ask for a rebuild";
            EXPECT_EQ(scene.getMeshes().size(), held);

            // The second room, and only the second room. The first is still in the resource cache,
            // so its drawables are alive and would be recognised if anything walked them.
            scene.clearPlacement();
            const RtxBridge::ExtractionStats two = readCell(*world, *second, kept, extractor);

            ASSERT_GT(two.mMeshesReused, 0u) << "two Imperial interiors that share no model at all";
            ASSERT_GT(two.mMeshesAdded, 0u);
            ASSERT_EQ(scene.getMeshes().size(), held + two.mMeshesAdded);

            const RtxBridge::Retirement went = extractor.retire();

            // What survived is what the second walk met, whether it was new or already here — so
            // the two numbers have to add back up to what was in the scene before the sweep. The
            // count of *distinct* shared models is not something the stats can say, since a reuse
            // is a lookup and a room holds a dozen of the same barrel.
            EXPECT_EQ(scene.getMeshes().size() + went.mMeshes, held + two.mMeshesAdded);
            EXPECT_GT(went.mMeshes, 0u) << "the room that was walked away from is still in the scene";
            EXPECT_LT(went.mMeshes, held) << "the models the two rooms share were dropped along with it";
            EXPECT_NE(scene.getStructureRevision(), built);

            // The buffers are exactly what the survivors need and not a vertex more, which is what
            // says the gaps were closed rather than merely skipped.
            std::size_t survivingVertices = 0;
            std::size_t survivingIndices = 0;
            for (const Rtx::MeshRange& range : scene.getMeshes())
            {
                survivingVertices += range.mVertexCount;
                survivingIndices += range.mIndexCount;
            }

            EXPECT_EQ(scene.getPositions().size(), survivingVertices);
            EXPECT_EQ(scene.getNormals().size(), survivingVertices);
            EXPECT_EQ(scene.getTexCoords().size(), survivingVertices);
            EXPECT_EQ(scene.getIndices().size(), survivingIndices);
            EXPECT_LT(survivingVertices, vertices) << "the first room's geometry is still being carried";

            // Every surviving offset has to still describe its own mesh, which is the whole of what
            // the compaction can get wrong: a range that moved and an offset that did not is a mesh
            // wearing its neighbour's triangles.
            for (Rtx::Index mesh = 0; mesh < scene.getMeshes().size(); ++mesh)
            {
                const Rtx::MeshRange& range = scene.getMeshes()[mesh];
                ASSERT_LE(std::size_t{ range.mVertexOffset } + range.mVertexCount, scene.getPositions().size());
                ASSERT_LE(std::size_t{ range.mIndexOffset } + range.mIndexCount, scene.getIndices().size());

                for (const std::uint32_t index : scene.getMeshIndices(mesh))
                    ASSERT_LT(index, range.mVertexCount) << "mesh " << mesh << " indexes past its own vertices";
            }

            // **Not one of them emptied.** A path is what the texture array is built from, and a
            // survivor written to the slot it already occupied is where one gets lost — the unit
            // tests reach that case deliberately and a cell reaches it whenever its first texture
            // outlives the walk.
            for (const VFS::Path::Normalized& texture : scene.getTextures())
                EXPECT_FALSE(texture.value().empty()) << "a surviving texture lost its path";

            for (const Rtx::Material& material : scene.getMaterials())
            {
                if (material.mDiffuse != Rtx::sNoIndex)
                {
                    EXPECT_LT(material.mDiffuse, scene.getTextures().size());
                }

                EXPECT_LE(std::size_t{ material.mLayerOffset } + material.mLayerCount, scene.getLayers().size());
            }

            // **The same geometry a scene that had never heard of the first room would hold.** This
            // is the assertion the offsets above only approach: two scenes agreeing mesh for mesh on
            // what is in them, whatever order they arrived in.
            Rtx::SceneDesc alone;
            {
                RtxBridge::SceneExtractor fresh(alone);
                readCell(*world, *second, kept, fresh);
            }

            EXPECT_EQ(meshFingerprints(scene), meshFingerprints(alone));
            EXPECT_EQ(scene.getMeshes().size(), alone.getMeshes().size());
            EXPECT_EQ(scene.getPositions().size(), alone.getPositions().size());
            EXPECT_EQ(scene.getIndices().size(), alone.getIndices().size());

            // And the survivors go on resolving through the identity map, which is the assertion the
            // compaction is really for: a third walk of the same room adds nothing at all.
            scene.clearPlacement();
            const RtxBridge::ExtractionStats again = readCell(*world, *second, kept, extractor);

            EXPECT_EQ(again.mMeshesAdded, 0u) << "a survivor was not recognised after the tables moved under it";
            EXPECT_EQ(again.mMaterialsAdded, 0u);
            EXPECT_EQ(again.mMeshesReused, two.mMeshesReused + two.mMeshesAdded)
                << "the same room, and every drawable in it resolving to something already here";
        }

        /// The renderer builds the same picture out of a compacted scene as out of one that never
        /// lost anything.
        ///
        /// **What the checks above cannot reach.** They say the two scenes hold the same geometry;
        /// this says a bottom-level structure built over a moved range still describes the mesh
        /// whose offset names it. An offset that moved by one vertex passes every count and comes
        /// out as a room made of somebody else's triangles.
        ///
        /// The two scenes name their meshes and their textures in different orders — one is what a
        /// compaction left and the other what a walk produced — so a picture that matches is the
        /// indices agreeing all the way through the build and not the tables happening to be equal.
        TEST(RtxRetireTest, aCompactedSceneRendersAsOneThatNeverLostAnything)
        {
            if (const std::string obstacle = Rtx::Testing::findInstanceObstacle(); !obstacle.empty())
                GTEST_SKIP() << obstacle;

            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            const ESM::Cell* first = world->findCell(std::string(sFirst));
            const ESM::Cell* second = world->findCell(std::string(sSecond));
            ASSERT_NE(first, nullptr);
            ASSERT_NE(second, nullptr);

            // The sequence the game runs: a walk, a sweep, a walk of somewhere else, a sweep that
            // drops what the first walk had — and then the walk that puts the placements back,
            // because compacting takes them with it.
            std::vector<osg::ref_ptr<osg::Group>> kept;
            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);
            CellLighting lighting;
            loadAndMirror(*world, *first, kept, scene, extractor);

            ASSERT_TRUE(extractor.retire().empty());

            for (int pass = 0; pass < 2; ++pass)
            {
                scene.clearPlacement();
                lighting = loadAndMirror(*world, *second, kept, scene, extractor);

                if (pass == 0)
                {
                    EXPECT_FALSE(extractor.retire().empty()) << "the first room is still in the scene";
                }
            }

            Rtx::SceneDesc alone;
            RtxBridge::SceneExtractor fresh(alone);
            CellLighting freshly;
            freshly = loadAndMirror(*world, *second, kept, alone, fresh);

            // The last walk put the second room back; this is the sweep that takes the first one's
            // placements with it, and without it the scene is still holding both.
            extractor.retire();

            // By what is placed and not by how many slots exist: a compacted scene has gaps where a
            // fresh one has none, and a gap is a name nothing is standing in rather than a placement.
            ASSERT_EQ(scene.getPlacedCount(), alone.getPlacedCount());

            Rtx::RendererOptions options;
            options.mShaderDirectory = Rtx::Testing::getShaderDirectory();
            options.mWidth = 640;
            options.mHeight = 360;
            options.mValidation.mEnabled = true;
            options.mValidation.mAbortOnError = false;

            std::string reason;
            const std::unique_ptr<Rtx::Renderer> renderer = Rtx::createRenderer(options, reason);
            if (renderer == nullptr)
                GTEST_SKIP() << reason;

            const Rtx::FrameExtents extents = renderer->getExtents();

            // **One camera for both**, derived from the scene that never lost anything: the two hold
            // the same geometry, so the same view of it is what makes the pictures comparable at all.
            const Placement placement = placeCamera(alone.getBounds(), 60.0f, std::nullopt, std::nullopt);

            const auto draw = [&](const Rtx::SceneDesc& drawn, const CellLighting& lit,
                                  std::vector<std::uint8_t>& out) {
                const RtxBridge::SceneTextures described(drawn, world->getImageManager());
                renderer->setScene(drawn, described.getDescriptions(), Rtx::SeaState{});

                Rtx::Shaders::VisibilityConstants camera = Rtx::makeCamera(placement.mOrigin, placement.mTarget, 60.0f,
                    extents.mRenderWidth, extents.mRenderHeight, 100000.0f);
                applyLighting(lit, camera);

                // Held rather than measured: an exposure taken off the frame turns any difference at
                // all into a difference everywhere, which is a worse instrument than the pixels.
                const Rtx::FrameResult result = renderer->renderFrame(camera, Rtx::FrameOptions{ .mExposure = 1.0f });
                renderer->readPixels(out);
                return result.mHits;
            };

            std::vector<std::uint8_t> compacted;
            std::vector<std::uint8_t> whole;
            const std::uint32_t hitsCompacted = draw(scene, lighting, compacted);
            const std::uint32_t hitsWhole = draw(alone, freshly, whole);

            ASSERT_GT(hitsWhole, 0u) << "the camera faced away from the room";
            EXPECT_EQ(hitsCompacted, hitsWhole);
            ASSERT_EQ(compacted.size(), whole.size());
            // **Within a display step or two, and not to the byte.** A compaction reorders the
            // bottom-level structures, so traversal reaches a pixel's candidates in a different
            // order and a sum of the same terms in a different order differs in its last bit. What
            // that cannot do is move a surface: measured here, 1,193 of 921,600 bytes differ and
            // none by more than 2 of 255, where a mesh built over the wrong range shows up as a
            // wall in the wrong place and a hit count that disagrees.
            std::size_t differing = 0;
            int worst = 0;
            for (std::size_t at = 0; at < compacted.size(); ++at)
            {
                const int gap = std::abs(static_cast<int>(compacted[at]) - static_cast<int>(whole[at]));
                worst = std::max(worst, gap);
                differing += gap != 0 ? 1 : 0;
            }

            EXPECT_LE(worst, 2) << "a difference this size is a surface somewhere else, not a rounded sum";
            EXPECT_LT(differing, compacted.size() / 100);
        }

    }
}
