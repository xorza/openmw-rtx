#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/resource/imagemanager.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneuploader.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "countingrenderer.hpp"

namespace RtxBridge
{
    namespace
    {
        /// What one call to `addModel` put in the scene, so a sweep can name it again.
        struct Model
        {
            Rtx::Index mMesh = 0;
            Rtx::Index mMaterial = 0;
            Rtx::Index mSlot = 0;
        };

        /// One triangle and one material naming one texture, so a mesh, a material and a texture all
        /// arrive together the way a model does.
        ///
        /// The paths are made up: `SceneTextures` answers a path that names nothing with the
        /// stand-in and counts it unreadable, which is exactly the description a decision needs and
        /// costs no content files to produce.
        Model addModel(Rtx::SceneDesc& scene, VFS::Path::NormalizedView texture)
        {
            const osg::Vec3f positions[3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            const osg::Vec3f normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            const osg::Vec2f uvs[3] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
            const std::uint32_t indices[3] = { 0, 1, 2 };

            Model made;
            made.mMesh = scene.addMesh(positions, normals, uvs, indices);

            Rtx::Material material;
            material.mDiffuse = scene.addTexture(texture);
            made.mMaterial = scene.addMaterial(material);
            made.mSlot = scene.addInstance(Rtx::MeshInstance{ .mMesh = made.mMesh, .mMaterial = made.mMaterial });

            return made;
        }

        /// The three branches in the order a session takes them, each proved by what the renderer
        /// was asked to do and by how much describing it cost.
        TEST(RtxSceneUploaderTest, aSceneIsRebuiltThenPlacedThenAppendedToThenRebuiltAgain)
        {
            VFS::Manager vfs;
            Resource::ImageManager images(&vfs, 0);

            Rtx::SceneDesc scene;
            SceneUploader uploader;
            Testing::CountingRenderer renderer;

            const Model first = addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            const Model second = addModel(scene, VFS::Path::NormalizedView("textures/two.dds"));

            // **First time through there is nothing to append to**, so two textures arriving is a
            // build of everything even though nothing was renumbered.
            const SceneUpload built = uploader.hand(renderer, scene, images, Rtx::SeaState{});
            EXPECT_EQ(built.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(built.mDescribed, std::size_t{ 2 });
            EXPECT_EQ(built.mUnreadable, 2u) << "a path that names nothing is described as the stand-in";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 2u);

            // Nothing has changed, which is every ordinary frame: the transforms are rewritten and
            // not one texture is looked at again.
            const SceneUpload still = uploader.hand(renderer, scene, images, Rtx::SeaState{});
            EXPECT_EQ(still.mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(still.mDescribed, std::size_t{ 0 });
            EXPECT_EQ(renderer.mPlaced, 1u);
            EXPECT_EQ(renderer.mRebuilt, 1u) << "an unchanged scene must not cost a rebuild";

            // A ring arrives: a third model, so the tables grew and nothing moved.
            const Model third = addModel(scene, VFS::Path::NormalizedView("textures/three.dds"));

            const SceneUpload grown = uploader.hand(renderer, scene, images, Rtx::SeaState{});
            EXPECT_EQ(grown.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(grown.mDescribed, std::size_t{ 1 }) << "only the arrival is described, not the table";
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 3u);
            EXPECT_FALSE(renderer.mAppendedToWrongEnd) << "the arrivals began somewhere other than the array's end";

            // The first model goes, in the order a sweep goes in: its placement first, then the
            // tables. **Nothing is renumbered by that any more**, so it is not a rebuild — the frame
            // after a cell leaves costs the top level and nothing else.
            scene.dropInstance(first.mSlot);

            const Rtx::Index keptMeshes[2] = { second.mMesh, third.mMesh };
            const Rtx::Index keptMaterials[2] = { second.mMaterial, third.mMaterial };
            ASSERT_TRUE(scene.release(keptMeshes, keptMaterials, {}));

            EXPECT_EQ(uploader.hand(renderer, scene, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Placed)
                << "a cell leaving cost a build";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mTextures, 3u);

            // **And replacing the scene outright is what a rebuild is for.** Travel, not a boundary:
            // `clear` starts every index again, so everything built from one has to be built again.
            scene.clear();
            addModel(scene, VFS::Path::NormalizedView("textures/four.dds"));

            const SceneUpload travelled = uploader.hand(renderer, scene, images, Rtx::SeaState{});
            EXPECT_EQ(travelled.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(travelled.mDescribed, std::size_t{ 1 });
            EXPECT_EQ(renderer.mRebuilt, 2u);
            EXPECT_EQ(renderer.mTextures, 1u);

            // And back to the ordinary frame, so the rebuild above left the uploader agreeing with
            // the scene rather than one revision behind it.
            EXPECT_EQ(uploader.hand(renderer, scene, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Placed);
        }

        /// Two uploaders over one scene do not share a decision, which is what makes one per renderer
        /// the rule rather than a habit.
        TEST(RtxSceneUploaderTest, anUploaderThatHasBuiltNothingRebuildsASceneTheOtherOnlyPlaces)
        {
            VFS::Manager vfs;
            Resource::ImageManager images(&vfs, 0);

            Rtx::SceneDesc scene;
            addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));

            SceneUploader built;
            Testing::CountingRenderer first;
            EXPECT_EQ(built.hand(first, scene, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(built.hand(first, scene, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Placed);

            SceneUploader fresh;
            Testing::CountingRenderer second;
            EXPECT_EQ(fresh.hand(second, scene, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Rebuilt);
        }

        /// A renderer carrying somebody else's textures is built from nothing, not appended to.
        ///
        /// **This is `bench`, and it is what a crash looked like.** It runs several places through
        /// one renderer with a scene of its own for each, so the second place meets an array holding
        /// the first place's images. An uploader that read that count as its own would start
        /// describing at 375 in a table 231 long, which is not a wrong picture but an overrun — and
        /// it surfaced as an allocation failure from somewhere with nothing to do with textures.
        TEST(RtxSceneUploaderTest, aSecondSceneOnOneRendererIsBuiltRatherThanAppendedTo)
        {
            VFS::Manager vfs;
            Resource::ImageManager images(&vfs, 0);
            Testing::CountingRenderer renderer;

            // The longer scene first, so appending onto its count would run off the end of the
            // shorter one — which is the failure, rather than merely describing the wrong images.
            Rtx::SceneDesc crowded;
            addModel(crowded, VFS::Path::NormalizedView("textures/one.dds"));
            addModel(crowded, VFS::Path::NormalizedView("textures/two.dds"));
            addModel(crowded, VFS::Path::NormalizedView("textures/three.dds"));

            SceneUploader place;
            ASSERT_EQ(place.hand(renderer, crowded, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(renderer.mTextures, 3u);

            Rtx::SceneDesc sparse;
            addModel(sparse, VFS::Path::NormalizedView("textures/four.dds"));

            SceneUploader next;
            const SceneUpload second = next.hand(renderer, sparse, images, Rtx::SeaState{});
            EXPECT_EQ(second.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(second.mDescribed, std::size_t{ 1 }) << "the descriptions began past the end of the table";
            EXPECT_EQ(renderer.mTextures, 1u);

            // And the same uploader carries on with the scene it did build, so the guard costs the
            // ordinary frame nothing.
            EXPECT_EQ(next.hand(renderer, sparse, images, Rtx::SeaState{}).mKind, SceneUpload::Kind::Placed);
        }
    }
}
