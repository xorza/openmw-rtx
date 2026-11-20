#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/countingrenderer.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/layers.hpp>
#include <components/rtx/renderer/nulldevice.hpp>
#include <components/rtx/renderer/residency.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// One renderer's residency and feed beside a counting renderer, and one frame of hand-overs over them: what
        /// the game opens and closes around every hand-over.
        struct Rig
        {
            Testing::CountingRenderer mRenderer;
            NullFeed mFeed;
            Residency mResidency{ mRenderer, SlotLayout{} };

            Rig() = default;
            ~Rig() = default;
            Rig(const Rig&) = delete;
            Rig& operator=(const Rig&) = delete;

            SceneUpload hand(SceneUploader& uploader, SceneDesc& scene, const SceneSlot slot = SceneSlot::world())
            {
                mResidency.openFrame(mRenderer, mFeed);
                const SceneUpload handed = uploader.hand(mRenderer,
                    SceneUploader::Handing{ .mSlot = slot, .mScene = scene, .mResidency = mResidency, .mFeed = mFeed });
                mResidency.closeFrame(mRenderer);
                mFeed.nextFrame();
                return handed;
            }
        };

        /// The three branches in the order a session takes them, each proved by what the renderer was asked to do.
        TEST(RtxSceneUploaderTest, aSessionTakesEachBranchInTheOrderItReachesThem)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            SceneUploader uploader;
            Rig rig;
            Testing::CountingRenderer& renderer = rig.mRenderer;

            const Testing::Model first = Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/two.dds"));

            // **First time through there is nothing to append to**, so two textures arriving is a build of everything
            // even though nothing was renumbered. A path that names nothing stands as nothing, and is refused.
            const SceneUpload built = rig.hand(uploader, scene);
            EXPECT_EQ(built.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 2u);
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 2u);

            // Nothing has changed, which is every ordinary frame: placed, and nothing built.
            const SceneUpload still = rig.hand(uploader, scene);
            EXPECT_EQ(still.mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(renderer.mRebuilt, 1u) << "an unchanged scene must not cost a rebuild";

            // A ring arrives: a third model, so the tables grew and nothing moved.
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/three.dds"));
            const SceneUpload grown = rig.hand(uploader, scene);
            EXPECT_EQ(grown.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 3u);

            // The first model goes: its placement, and with it the last holds on its mesh and its material, which
            // rests, residency standing its structure. **Nothing is renumbered by that**, so it is not a rebuild, and
            // its texture is given back on that same frame.
            scene.dropInstance(first.mPlacement, Stander::Walk);
            ASSERT_TRUE(scene.meshes().getRows()[first.mMesh].mRest.mResting);
            const SceneUpload left = rig.hand(uploader, scene);
            EXPECT_EQ(left.mKind, SceneUpload::Kind::Placed) << "a cell leaving cost a build";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mTextures, 3u) << "the array shrank, so an append would begin in the wrong place";

            // **A ring arriving is still an append**, and a crossing, one ring arriving as another goes, is one too.
            const Testing::Model fourth = Testing::addModel(scene, VFS::Path::NormalizedView("textures/four.dds"));
            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Extended);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/five.dds"));
            scene.dropInstance(fourth.mPlacement, Stander::Walk);
            ASSERT_TRUE(scene.meshes().getRows()[fourth.mMesh].mRest.mResting);
            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(renderer.mRebuilt, 1u) << "nothing renumbered, so nothing was built again";
        }

        /// **A frame that only places allocates nothing**, the uploader's, residency's and the scene's: once a scene
        /// is built, every ordinary frame is this branch.
        TEST(RtxSceneUploaderTest, aFrameThatOnlyPlacesAllocatesNothing)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            SceneUploader uploader;
            Rig rig;
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            ASSERT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Placed);
            ASSERT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Placed);

            SceneUpload placed;
            EXPECT_EQ(Testing::allocationsIn([&] { placed = rig.hand(uploader, scene); }), 0u);
            EXPECT_EQ(placed.mKind, SceneUpload::Kind::Placed);
        }

        /// **A material that gains layer and mask runs, and no mesh or texture, is an extension**: runs are staged on
        /// an extension alone, and the revisions of the meshes and textures did not move.
        TEST(RtxSceneUploaderTest, aMaterialThatGainsRunsAndNothingElseIsAnExtension)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            SceneUploader uploader;
            Rig rig;
            const Testing::Model model = Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            ASSERT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Rebuilt);

            constexpr std::array<float, 4> weights{ 1.0f, 0.0f, 0.0f, 1.0f };
            const std::array layers{ Testing::layerOf(model.mTexture),
                Testing::layerOf(model.mTexture, scene.materials().addMask(weights), 2, 2) };
            Material layered;
            layered.mLayers = scene.materials().addLayers(layers);
            scene.addMaterial(layered);

            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Extended)
                << "runs arrived and the device was told to place";
            EXPECT_EQ(rig.mRenderer.mExtended, 1u);
            EXPECT_EQ(rig.mRenderer.mRebuilt, 1u);
            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Placed)
                << "runs staged once were staged again";
        }

        /// **What residency stands of a mesh's structure stands the mesh in the scene**, and what fails is reported
        /// with the scene's refusals: a model whose path names nothing. The first hand-over builds the slot and asks
        /// the device a chunk; the second builds the structure loose into it, which the scene is told; the texture is
        /// refused once, on the hand-over that met it.
        TEST(RtxSceneUploaderTest, whatResidencyStandsOfAMeshStandsItInTheScene)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            SceneUploader uploader;
            Rig rig;
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            const MeshIndex mesh = scene.placements().getRows()[PlacementIndex(0)].mInstance.mMesh;

            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_FALSE(scene.meshes().getRows()[mesh].mStands) << "a structure stood before its chunk came";
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 1u) << "a path that names nothing";

            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Placed);
            EXPECT_TRUE(scene.meshes().getRows()[mesh].mStands);
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 1u) << "a refusal taken was taken again";
            EXPECT_EQ(scene.refusals().count(Refused::Mesh), 0u);
        }

        /// **The array's limit is reported on the hand-over that met it, whichever branch it went**: a texture the full
        /// table refused arrives nowhere, so the hand-over after it only places.
        TEST(RtxSceneUploaderTest, theTextureArraysLimitIsReportedOnAHandOverThatOnlyPlaces)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            SceneUploader uploader;
            Rig rig;

            TextureTable& textures = scene.textures();
            for (std::size_t filled = 0; textures.getLiveCount() < TextureTable::sCapacity; ++filled)
                ASSERT_NE(
                    textures.add(VFS::Path::Normalized("textures/fill" + std::to_string(filled) + ".dds")), sNoRow);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/fill0.dds"));
            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Rebuilt);
            const std::uint32_t before = scene.refusals().count(Refused::Texture);

            EXPECT_EQ(textures.add(VFS::Path::NormalizedView("textures/over.dds")), sNoRow);
            EXPECT_EQ(rig.hand(uploader, scene).mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(scene.refusals().count(Refused::Texture), before + 1) << "the limit went unreported";
            EXPECT_EQ(textures.getRefusedArrivals(), 0u) << "the hand-over kept its refusals for the next";
        }

        /// Two uploaders over one scene do not share a decision, which is what makes one per renderer the rule
        /// rather than a habit.
        TEST(RtxSceneUploaderTest, anUploaderThatHasBuiltNothingRebuildsASceneTheOtherOnlyPlaces)
        {
            SceneIdentities identities;
            SceneDesc scene(identities);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));

            SceneUploader built;
            Rig first;
            EXPECT_EQ(first.hand(built, scene).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(first.hand(built, scene).mKind, SceneUpload::Kind::Placed);

            SceneUploader fresh;
            Rig second;
            EXPECT_EQ(second.hand(fresh, scene).mKind, SceneUpload::Kind::Rebuilt);
        }

        /// **A renderer carrying somebody else's scene is built from nothing, not appended to**, and residency forgets
        /// what stood of the scene it held: this is `bench`, several places through one renderer with a scene of its
        /// own each. A description moved keeps its identity, and the slot built from it is still its own.
        TEST(RtxSceneUploaderTest, aSecondSceneOnOneRendererIsBuiltRatherThanAppendedTo)
        {
            SceneIdentities identities;
            Rig rig;

            SceneDesc crowded(identities);
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/one.dds"));
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/two.dds"));
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/three.dds"));
            SceneUploader place;
            ASSERT_EQ(rig.hand(place, crowded).mKind, SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(rig.mRenderer.mTextures, 3u);

            SceneDesc sparse(identities);
            Testing::addModel(sparse, VFS::Path::NormalizedView("textures/four.dds"));
            SceneUploader next;
            EXPECT_EQ(rig.hand(next, sparse).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(rig.mRenderer.mTextures, 1u);
            EXPECT_EQ(rig.hand(next, sparse).mKind, SceneUpload::Kind::Placed);

            SceneDesc moved = std::move(sparse);
            EXPECT_EQ(moved.getIdentity(), rig.mRenderer.describeHeld(SceneSlot::world()).mIdentity);
            EXPECT_EQ(rig.hand(next, moved).mKind, SceneUpload::Kind::Placed)
                << "a move was taken for another description";

            const SceneDesc another(identities);
            EXPECT_NE(another.getIdentity(), moved.getIdentity());
            EXPECT_NE(another.getIdentity(), SceneIdentity{});
        }

        /// **A picture inside the interface is handed over the same way a cell is, and neither disturbs the other**:
        /// what a race-creation slider drag costs, the doll walked again on every frame of the drag and nothing
        /// arriving, so every hand-over after the first places.
        TEST(RtxSceneUploaderTest, aPictureIsHandedOverTheSameWayACellIsAndNeitherDisturbsTheOther)
        {
            SceneIdentities identities;
            Rig rig;

            SceneDesc world(identities);
            Testing::addModel(world, VFS::Path::NormalizedView("textures/ground.dds"));
            Testing::addModel(world, VFS::Path::NormalizedView("textures/wall.dds"));
            SceneDesc doll(identities);
            const Testing::Model body = Testing::addModel(doll, VFS::Path::NormalizedView("textures/skin.dds"));
            const SceneSlot slot = rig.mRenderer.addViewScene();

            SceneUploader ofTheWorld;
            SceneUploader ofTheDoll;
            ASSERT_EQ(rig.hand(ofTheWorld, world).mKind, SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(rig.hand(ofTheDoll, doll, slot).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(rig.mRenderer.textureCountOf(SceneSlot::world()), 2u);
            EXPECT_EQ(rig.mRenderer.textureCountOf(slot), 1u);

            for (std::int32_t frame = 0; frame < 3; ++frame)
            {
                doll.clearPlacement();
                doll.addInstance(MeshInstance{ .mMesh = body.mMesh, .mMaterial = body.mMaterial });
                EXPECT_EQ(rig.hand(ofTheDoll, doll, slot).mKind, SceneUpload::Kind::Placed)
                    << "redraw " << frame << " rebuilt a subject that did not change";
            }

            world.clearPlacement();
            world.addInstance(MeshInstance{ .mMesh = MeshIndex(0), .mMaterial = MaterialIndex(0) });
            EXPECT_EQ(rig.hand(ofTheWorld, world).mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(rig.mRenderer.textureCountOf(SceneSlot::world()), 2u);
            EXPECT_EQ(rig.mRenderer.textureCountOf(slot), 1u);
        }
    }
}
