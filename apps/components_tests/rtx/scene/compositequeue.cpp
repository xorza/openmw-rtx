#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/layers.hpp>
#include <apps/components_tests/rtx/support/sceneholds.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// A layer texture at `path`, the device handed it whole where `stood`, as a file read and uploaded is.
        TextureIndex addLayerTexture(SceneDesc& scene, const VFS::Path::NormalizedView path, const bool stood)
        {
            const TextureIndex slot = scene.textures().add(path);
            if (stood)
                scene.textures().stand(slot, 64, false, 0);
            return slot;
        }

        /// A distant chunk of two ground types, which is a chunk that wants flattening. `reflecting`
        /// makes the top one an authored layer, which is a chunk that wants a gloss too; `overStood`
        /// false leaves the top one's texture standing in, as one still read from its file is.
        MaterialIndex addChunk(SceneDesc& scene, const VFS::Path::NormalizedView under,
            const VFS::Path::NormalizedView over, const bool reflecting = false, const bool overStood = true)
        {
            constexpr std::array<float, 4> weights{ 1.0f, 0.0f, 0.0f, 1.0f };

            std::array layers{
                Testing::layerOf(addLayerTexture(scene, under, true)),
                Testing::layerOf(addLayerTexture(scene, over, overStood), scene.materials().addMask(weights), 2, 2),
            };
            if (reflecting)
                layers[1].mFlags = Shaders::LAYER_AUTHORED;

            Material material;
            material.mKind = MaterialKind::Terrain;
            material.mFlatten = GroundFlattening::Every;
            material.mLayers = scene.materials().addLayers(layers);

            return scene.addMaterial(material);
        }

        /// Fills `textures` with textures of their own until `left` slots stand free, and answers
        /// their names, by which a test frees one: a name added again answers its slot.
        std::vector<VFS::Path::Normalized> fillTextures(TextureTable& textures, const std::size_t left)
        {
            std::vector<VFS::Path::Normalized> fillers;
            fillers.reserve(TextureTable::sCapacity);
            while (textures.getLiveCount() + left < TextureTable::sCapacity)
            {
                fillers.emplace_back("textures/fill" + std::to_string(fillers.size()) + ".dds");
                if (textures.add(fillers.back()) == sNoRow)
                {
                    ADD_FAILURE() << "the table refused a texture with room left";
                    break;
                }
            }
            return fillers;
        }

        /// One frame of the uploader's sequence around the queue: take, then let go of the
        /// arrivals, so the next frame's gather sees only what the next walk writes.
        std::size_t frame(CompositeQueue& queue, SceneDesc& scene)
        {
            const std::size_t taken = queue.advance(scene);
            scene.clearArrivals();
            return taken;
        }

        /// A chunk that asks is given its slot on the frame it asked, and asks once — and **a chunk
        /// with a layer that reflects is given its gloss beside it**, as its material's specular,
        /// where one with none is given no gloss at all.
        ///
        /// The slot is the composite the device fills in the placement after, so the frame is a
        /// count and never a baker's finishing time: no thread, no sleep and no timing anywhere.
        TEST(RtxCompositeQueueTest, aChunkIsGivenItsSlotOnTheFrameItAskedAndAsksOnce)
        {
            for (const bool reflecting : { false, true })
            {
                SceneIdentities identities;
                SceneDesc scene(identities);
                const MaterialIndex chunk = addChunk(scene, VFS::Path::NormalizedView("textures/under.dds"),
                    VFS::Path::NormalizedView("textures/over.dds"), reflecting);

                CompositeQueue queue;
                EXPECT_EQ(queue.advance(scene), 1u) << "the chunk that asked was not given its slot";

                const Material& given = scene.materials().getRows()[chunk];
                const TextureIndex baked = given.mDiffuse;
                ASSERT_NE(baked, sNoRow) << "the chunk still shades from its stack";
                EXPECT_EQ(scene.textures().getRows()[baked].mGroundOf, chunk)
                    << "the slot the chunk was given is not named as its ground";
                EXPECT_EQ(scene.textures().getRows()[baked].mKind, TextureKind::GroundAlbedo);

                // **The row says what the slot is read as**, and the description reads the row: the albedo a
                // colour, the gloss data. A row saying colour beside a description saying data would make the
                // scene's digest hash the row.
                EXPECT_EQ(scene.textures().getRows()[baked].mEncoding, TextureEncoding::Colour);

                if (reflecting)
                {
                    ASSERT_NE(given.mSpecular, sNoRow) << "a chunk that reflects was given no gloss";
                    EXPECT_NE(given.mSpecular, baked);
                    EXPECT_EQ(scene.textures().getRows()[given.mSpecular].mGroundOf, chunk);
                    EXPECT_EQ(scene.textures().getRows()[given.mSpecular].mKind, TextureKind::GroundGloss)
                        << "the gloss is named as the albedo";
                    EXPECT_EQ(scene.textures().getRows()[given.mSpecular].mEncoding, TextureEncoding::Data);
                }
                else
                    EXPECT_EQ(given.mSpecular, sNoRow) << "a chunk that reflects nowhere was given a gloss";

                // A chunk with its ground asks for no more: the rewrite that gave it the slot is a
                // row written, and the gather has to read it as answered rather than as asking again.
                scene.clearArrivals();
                EXPECT_EQ(frame(queue, scene), 0u) << "a chunk with its ground asked again";
            }
        }

        /// **A chunk is flattened once every layer's texture stands**, and shades from its stack until then: the bake
        /// reads each layer's diffuse, and over one still read from its file it held the stand-in's grey in the
        /// distance. A chunk asked after it is taken meanwhile, and the waiting one is taken on the frame its last
        /// layer stands, once.
        TEST(RtxCompositeQueueTest, aChunkIsFlattenedOnceEveryLayersTextureStands)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            const MaterialIndex reading = addChunk(scene, VFS::Path::NormalizedView("textures/under.dds"),
                VFS::Path::NormalizedView("textures/read.dds"), false, false);
            const MaterialIndex stood = addChunk(
                scene, VFS::Path::NormalizedView("textures/under.dds"), VFS::Path::NormalizedView("textures/over.dds"));

            CompositeQueue queue;
            EXPECT_EQ(frame(queue, scene), 1u) << "the chunk behind the waiting one was held up";
            EXPECT_EQ(scene.materials().getRows()[reading].mDiffuse, sNoRow) << "flattened over a stand-in";
            EXPECT_NE(scene.materials().getRows()[stood].mDiffuse, sNoRow);
            EXPECT_EQ(frame(queue, scene), 0u) << "taken while its layer still stands in";

            const TextureIndex read = scene.textures().add(VFS::Path::NormalizedView("textures/read.dds"));
            scene.textures().stand(read, 64, false, 0);
            EXPECT_EQ(frame(queue, scene), 1u) << "not taken once its last layer stood";
            EXPECT_NE(scene.materials().getRows()[reading].mDiffuse, sNoRow);
            EXPECT_EQ(frame(queue, scene), 0u) << "asked again once given";
        }

        /// **A frame with nothing asked allocates nothing**, once a chunk has been taken: every frame of a walk
        /// that stands still.
        TEST(RtxCompositeQueueTest, aFrameWithNothingAskedAllocatesNothing)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            addChunk(
                scene, VFS::Path::NormalizedView("textures/under.dds"), VFS::Path::NormalizedView("textures/over.dds"));
            CompositeQueue queue;
            ASSERT_EQ(frame(queue, scene), 1u);
            ASSERT_EQ(frame(queue, scene), 0u);

            std::size_t taken = 1;
            EXPECT_EQ(Testing::allocationsIn([&] { taken = frame(queue, scene); }), 0u);
            EXPECT_EQ(taken, 0u);
        }

        /// A frame takes `sCompositesPerFrame` and no more, in the order the chunks asked.
        ///
        /// **The bound is on what an arrival frame pays**, a texture stood and a dispatch over it
        /// apiece — so a walk that queued a region's worth still takes them a couple at a time, and
        /// the first asked is the first flattened.
        ///
        /// **Two waves, so the order survives the ring wrapping and growing.** The first six fill a
        /// ring of eight from the front and a frame takes two, which leaves four from slot two on;
        /// of the second five, four wrap round into slots six, seven, nought and one, and the fifth
        /// finds the ring full and grows it with the oldest ask anywhere but slot nought.
        TEST(RtxCompositeQueueTest, aFrameTakesNoMoreThanItsBoundInTheOrderAsked)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            constexpr std::size_t first = sCompositesPerFrame * 3;
            constexpr std::size_t second = sCompositesPerFrame * 2 + 1;

            // Held rather than made per call: a view does not own its path, and each chunk wants two
            // of its own so that no two share a texture slot.
            std::vector<VFS::Path::Normalized> paths;
            std::vector<MaterialIndex> materials;
            paths.reserve((first + second) * 2);
            const auto ask = [&](const std::size_t count) {
                for (std::size_t at = 0; at < count; ++at)
                {
                    const std::size_t chunk = materials.size();
                    paths.emplace_back("textures/ground" + std::to_string(chunk) + "a.dds");
                    paths.emplace_back("textures/ground" + std::to_string(chunk) + "b.dds");
                    materials.push_back(addChunk(scene, paths[chunk * 2], paths[chunk * 2 + 1]));
                }
            };

            CompositeQueue queue;
            std::size_t taken = 0;
            const auto expectTaken = [&](const std::size_t now) {
                EXPECT_EQ(frame(queue, scene), now) << "a frame took other than its bound";
                taken += now;

                for (std::size_t at = 0; at < materials.size(); ++at)
                    EXPECT_EQ(scene.materials().getRows()[materials[at]].mDiffuse != sNoRow, at < taken)
                        << "chunk " << at << " after " << taken << " were taken";
            };

            ask(first);
            expectTaken(sCompositesPerFrame);

            ask(second);
            while (taken < materials.size())
                expectTaken(std::min(sCompositesPerFrame, materials.size() - taken));

            EXPECT_EQ(frame(queue, scene), 0u) << "more were given out than ever asked";
        }

        /// A chunk whose slot another material took over while it waited is not flattened as
        /// what it asked for: the ask is dropped, and what stands there now asks for itself.
        TEST(RtxCompositeQueueTest, aSlotTakenOverWhileItWaitedIsNotGivenTheFirstAskersGround)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            constexpr std::size_t chunks = sCompositesPerFrame + 1;

            std::vector<VFS::Path::Normalized> paths;
            std::vector<MaterialIndex> materials;
            paths.reserve(chunks * 2 + 3);
            for (std::size_t at = 0; at < chunks; ++at)
            {
                paths.emplace_back("textures/ground" + std::to_string(at) + "a.dds");
                paths.emplace_back("textures/ground" + std::to_string(at) + "b.dds");
                materials.push_back(addChunk(scene, paths[at * 2], paths[at * 2 + 1]));
            }

            CompositeQueue queue;
            EXPECT_EQ(frame(queue, scene), sCompositesPerFrame);

            // The one still waiting goes away, and a wall takes its slot.
            const MaterialIndex waiting = materials.back();
            materials.pop_back();
            Testing::letGoMaterial(scene, waiting);
            ASSERT_FALSE(scene.materials().isLive(waiting)) << "the material was not freed";

            paths.emplace_back("textures/wall.dds");
            Material wall;
            wall.mDiffuse = scene.textures().add(paths.back());
            const MaterialIndex newcomer = scene.addMaterial(wall);
            ASSERT_EQ(newcomer, waiting) << "the newcomer did not take the freed slot";

            EXPECT_EQ(frame(queue, scene), 0u) << "the chunk that went away was flattened onto the wall";
            EXPECT_EQ(scene.materials().getRows()[newcomer].mDiffuse, wall.mDiffuse);

            // And a chunk that takes the slot after that asks for itself, off the row it wrote.
            Testing::letGoMaterial(scene, newcomer);
            ASSERT_FALSE(scene.materials().isLive(newcomer)) << "the wall was not freed";
            paths.emplace_back("textures/newcomer-a.dds");
            paths.emplace_back("textures/newcomer-b.dds");
            const MaterialIndex chunk = addChunk(scene, paths[chunks * 2 + 1], paths[chunks * 2 + 2]);
            ASSERT_EQ(chunk, waiting);

            EXPECT_EQ(queue.advance(scene), 1u);
            const TextureIndex baked = scene.materials().getRows()[chunk].mDiffuse;
            ASSERT_NE(baked, sNoRow);
            EXPECT_EQ(scene.textures().getRows()[baked].mGroundOf, chunk);
        }

        /// **A chunk the texture table refuses keeps its place, and the queue asks again only once the table frees a
        /// slot.** A dropped chunk would never ask again (its material is not written again) and the ground would
        /// stay a stack for good; asked every frame, each frame would pay a refusal for nothing.
        ///
        /// Counted by the table's refusals: the first frame tries the first chunk and stops, one;
        /// the second tries nothing, still one. A freed slot goes to the first chunk, and the same
        /// frame tries the second and stops, two; then nothing again until the next freed slot,
        /// which the second takes.
        TEST(RtxCompositeQueueTest, aRefusedChunkKeepsItsPlaceUntilTheTableFreesASlot)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            const std::array<MaterialIndex, 2> chunks{
                addChunk(scene, VFS::Path::NormalizedView("textures/one-a.dds"),
                    VFS::Path::NormalizedView("textures/one-b.dds")),
                addChunk(scene, VFS::Path::NormalizedView("textures/two-a.dds"),
                    VFS::Path::NormalizedView("textures/two-b.dds")),
            };

            TextureTable& textures = scene.textures();
            const std::vector<VFS::Path::Normalized> fillers = fillTextures(textures, 0);

            const auto diffuse
                = [&](const std::size_t chunk) { return scene.materials().getRows()[chunks[chunk]].mDiffuse; };

            CompositeQueue queue;
            EXPECT_EQ(frame(queue, scene), 0u);
            EXPECT_EQ(textures.getRefused(), 1u) << "the take went on past a refusal";
            EXPECT_EQ(frame(queue, scene), 0u);
            EXPECT_EQ(textures.getRefused(), 1u) << "a refused take was asked again before anything was freed";

            const TextureIndex first = textures.add(fillers[0]);
            scene.drop(scene.holdTexture(first, HoldTier::Shown));
            EXPECT_EQ(frame(queue, scene), 1u);
            EXPECT_EQ(diffuse(0), first) << "the chunk refused first was not first in line";
            EXPECT_EQ(diffuse(1), sNoRow);
            EXPECT_EQ(textures.getRefused(), 2u);
            EXPECT_EQ(frame(queue, scene), 0u);
            EXPECT_EQ(textures.getRefused(), 2u);

            const TextureIndex second = textures.add(fillers[1]);
            scene.drop(scene.holdTexture(second, HoldTier::Shown));
            EXPECT_EQ(frame(queue, scene), 1u);
            EXPECT_EQ(diffuse(1), second) << "the second chunk was dropped at its refusal";
            EXPECT_EQ(frame(queue, scene), 0u);
        }

        /// **A chunk that reflects, taken where the table has room for its albedo and not its gloss, is flattened
        /// with no lobe**: its albedo stands, its specular names nothing, and it does not ask again. One slot is
        /// left after the chunk's own layers stood.
        TEST(RtxCompositeQueueTest, aReflectingChunkWithRoomForItsAlbedoAloneIsFlattenedWithNoLobe)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            const MaterialIndex chunk = addChunk(scene, VFS::Path::NormalizedView("textures/under.dds"),
                VFS::Path::NormalizedView("textures/over.dds"), true);

            TextureTable& textures = scene.textures();
            fillTextures(textures, 1);
            ASSERT_EQ(textures.getLiveCount() + 1, TextureTable::sCapacity);

            CompositeQueue queue;
            EXPECT_EQ(frame(queue, scene), 1u) << "the chunk was not taken on its albedo's room";

            const Material& given = scene.materials().getRows()[chunk];
            ASSERT_NE(given.mDiffuse, sNoRow);
            EXPECT_EQ(textures.getRows()[given.mDiffuse].mKind, TextureKind::GroundAlbedo);
            EXPECT_EQ(given.mSpecular, sNoRow) << "a gloss with no room was named";
            EXPECT_EQ(frame(queue, scene), 0u) << "a chunk flattened with no lobe asked again";
        }
    }
}
