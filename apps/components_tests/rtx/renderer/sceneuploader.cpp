#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/countingrenderer.hpp>
#include <apps/components_tests/rtx/support/ddsfile.hpp>
#include <apps/components_tests/rtx/support/derivedheaders.hpp>
#include <apps/components_tests/rtx/support/firsttexel.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/layers.hpp>
#include <components/resource/cachebudget.hpp>
#include <components/resource/imageheader.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/renderer/roomwaits.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// The three branches in the order a session takes them, each proved by what the renderer
        /// was asked to do and by how much describing it cost.
        TEST(RtxSceneUploaderTest, aSessionTakesEachBranchInTheOrderItReachesThem)
        {
            Rtx::SceneIdentities identities;

            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;

            const Testing::Model first = Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/two.dds"));

            // **First time through there is nothing to append to**, so two textures arriving is a
            // build of everything even though nothing was renumbered.
            const SceneUpload built = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(built.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(built.mDescribed, std::size_t{ 2 });
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 2u)
                << "a path that names nothing is described as the stand-in, and refused";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 2u);

            // Nothing has changed, which is every ordinary frame: the transforms are rewritten and
            // not one texture is looked at again.
            const SceneUpload still = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(still.mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(still.mDescribed, std::size_t{ 0 });
            EXPECT_EQ(renderer.mPlaced, 1u);
            EXPECT_EQ(renderer.mRebuilt, 1u) << "an unchanged scene must not cost a rebuild";

            // A ring arrives: a third model, so the tables grew and nothing moved.
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/three.dds"));

            const SceneUpload grown = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(grown.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(grown.mDescribed, std::size_t{ 1 }) << "only the arrival is described, not the table";
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mPlaced, 2u) << "an extension appends, and the uploader places after it";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mTextures, 3u);
            EXPECT_FALSE(renderer.mAppendedToWrongEnd) << "the arrivals began somewhere other than the array's end";

            // The first model goes: its placement, and with it the last holds on its mesh and its material.
            // **Nothing is renumbered by that**, so it is not a rebuild: the frame after a cell leaves costs the top
            // level and nothing else.
            scene.dropInstance(first.mPlacement, Stander::Walk);
            ASSERT_FALSE(scene.meshes().isLive(first.mMesh));

            const SceneUpload left = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(left.mKind, SceneUpload::Kind::Placed) << "a cell leaving cost a build";
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mTextures, 3u);

            // **And its texture is given back on that same frame.** Nothing arrived, so this is the
            // branch that could do only the placing — and waiting for an arrival to hand the memory
            // over is a whole grid of cells on a route that keeps moving.
            EXPECT_EQ(left.mDropped, std::size_t{ 1 });
            EXPECT_EQ(renderer.mDropped, (std::vector{ first.mTexture }));
            EXPECT_EQ(renderer.mTextures, 3u) << "the array shrank, so an append would begin in the wrong place";

            // Named once and then forgotten, which is what the arrivals being cleared on this branch
            // buys: a slot dropped every frame until something took it over would be a drop per
            // frame for as long as the region was gone.
            EXPECT_EQ(
                uploader
                    .hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene })
                    .mDropped,
                std::size_t{ 0 });
            EXPECT_EQ(renderer.mDropped.size(), std::size_t{ 1 });

            // **A ring arriving into the slot one left is still an append.** Nothing renumbered, so
            // the rebuild that opened this test stays the only one — which is the whole of what an
            // incremental mirror is worth.
            const Testing::Model fourth = Testing::addModel(scene, VFS::Path::NormalizedView("textures/four.dds"));

            const SceneUpload grew = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(grew.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(grew.mDescribed, std::size_t{ 1 }) << "the arrival, and not the whole table";
            EXPECT_EQ(renderer.mRebuilt, 1u) << "nothing renumbered, so nothing was built again";

            // **The loader is the uploader's own and outlives every scene it describes**, so what
            // it hands over here has to be this frame's arrival and nothing held from the frames
            // behind it. Four hand-overs stand behind this one.
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ fourth.mTexture }))
                << "the loader answered with what it held from an earlier frame";

            // **A crossing, which is the two at once**: one ring arrives as another goes, on one
            // frame. Both lists are applied and neither costs a rebuild.
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/five.dds"));
            scene.dropInstance(fourth.mPlacement, Stander::Walk);
            ASSERT_FALSE(scene.meshes().isLive(fourth.mMesh));

            const SceneUpload crossed = uploader.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(crossed.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(crossed.mDescribed, std::size_t{ 1 }) << "the arrival, and not the one that went";
            EXPECT_EQ(crossed.mDropped, std::size_t{ 1 });
            EXPECT_EQ(renderer.mDropped.back(), fourth.mTexture);
            EXPECT_FALSE(renderer.mAppendedToWrongEnd);
        }

        /// **A material that gains layer and mask runs, and no mesh or texture, is an extension.**
        /// Runs are staged on an extend alone, and the sum of the mesh and texture revisions that
        /// decided it did not move: the device was told to place, and read layers it was never
        /// handed. Over a texture already uploaded, so nothing is described, and placed again after.
        /// **A frame that only places allocates nothing**, the uploader's and the scene's: once a scene is built,
        /// every ordinary frame is this branch.
        TEST(RtxSceneUploaderTest, aFrameThatOnlyPlacesAllocatesNothing)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            const Rtx::SceneUploader::Handing handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene };
            ASSERT_EQ(uploader.hand(renderer, handing).mKind, SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(uploader.hand(renderer, handing).mKind, SceneUpload::Kind::Placed);

            SceneUpload placed;
            EXPECT_EQ(Testing::allocationsIn([&] { placed = uploader.hand(renderer, handing); }), 0u);
            EXPECT_EQ(placed.mKind, SceneUpload::Kind::Placed);
        }

        TEST(RtxSceneUploaderTest, aMaterialThatGainsRunsAndNothingElseIsAnExtension)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const Testing::Model model = Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };
            ASSERT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);

            constexpr std::array<float, 4> weights{ 1.0f, 0.0f, 0.0f, 1.0f };
            const std::array layers{ Testing::layerOf(model.mTexture),
                Testing::layerOf(model.mTexture, scene.materials().addMask(weights), 2, 2) };
            Material layered;
            layered.mLayers = scene.materials().addLayers(layers);
            scene.addMaterial(layered);

            const SceneUpload staged = hand();
            EXPECT_EQ(staged.mKind, SceneUpload::Kind::Extended) << "runs arrived and the device was told to place";
            EXPECT_EQ(staged.mDescribed, std::size_t{ 0 }) << "no texture arrived";
            EXPECT_EQ(renderer.mExtended, 1u);
            EXPECT_EQ(renderer.mRebuilt, 1u);
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Placed) << "runs staged once were staged again";
        }

        /// What the device could not stand is reported with what the describe could not, taken after every
        /// hand-over: a build's, an arrival's, and a placement's, which builds what waited for room and may find
        /// it is not coming.
        ///
        /// **The describe refuses the path nothing reads, and the device refuses a mesh and a
        /// texture of its own**: two textures and a mesh, each named once. A placement's mesh is a third
        /// refusal, and a refusal taken is not taken again, so the arrival after it adds only its own
        /// unreadable texture.
        TEST(RtxSceneUploaderTest, whatTheDeviceCouldNotStandIsReportedWithTheScenesRefusals)
        {
            Rtx::SceneIdentities identities;

            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            renderer.mRefusing = {
                Refusal{ .mKind = Refused::Mesh, .mWhy = "no device memory is left for it" },
                Refusal{ .mKind = Refused::Texture,
                    .mName = "textures/vast.dds",
                    .mWhy = "no device memory is left for it" },
            };

            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(scene.refusals().count(Refused::Mesh), 1u);
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 2u);

            // And what each mesh's structure takes, which the scene stands the mesh on: its structure of 100, and not
            // its geometry, which an eviction gives the device none of.
            const MeshIndex mesh = scene.placements().getRows()[PlacementIndex(0)].mInstance.mMesh;
            renderer.mStanding.push_back(StoodMesh{ .mMesh = mesh, .mStructureBytes = 100 });
            renderer.mRefusing.push_back(Refusal{ .mKind = Refused::Mesh, .mWhy = "no room is coming for it" });
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(scene.meshes().getRows()[mesh].mStoodBytes, 100u);
            EXPECT_EQ(scene.refusals().count(Refused::Mesh), 2u) << "a placement's refusal was not taken";

            Testing::addModel(scene, VFS::Path::NormalizedView("textures/two.dds"));
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(scene.refusals().count(Refused::Mesh), 2u) << "a refusal taken was taken again";
            EXPECT_EQ(scene.refusals().count(Refused::Texture), 3u);
        }

        /// **A slot stands in until its image is made, and then stands once, with it**: a normal map is
        /// described as the stand-in and named pending; once the image is made, the next hand-over
        /// describes the slot with it, its second level the direction of its normals' mean, (122, 130,
        /// 255, 75), within BC7's step, with nothing pending, and the slot is stood, the host letting go
        /// of the chain the device now keeps; and the one after only places. **Settled, the first
        /// hand-over waits for the image**, under a service that has made nothing yet, and describes the
        /// slot with it, as a run that compares frames needs.
        TEST(RtxSceneUploaderTest, aSlotStandsInUntilItsImageIsMadeAndThenStandsOnce)
        {
            Rtx::SceneIdentities identities;
            const std::uint8_t bytes[] = {
                128, 128, 255, 0, 128, 136, 255, 0, 120, 128, 247, 100, 112, 128, 251, 200, //
                0, 0, 0, 0 //
            };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new std::uint8_t[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            file->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            file->setMipmapLevels(osg::Image::MipmapDataType{ 16 });
            file->setFileName("textures/tx_test_nh.dds");

            Rtx::SceneDesc scene(identities);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));
            TextureHold held = scene.takeTexture(VFS::Path::NormalizedView("textures/tx_test_nh.dds"), *file,
                TextureWrap::Repeat, TextureEncoding::Normal);
            const TextureIndex slot = held.get();

            PreparedImages prepared(nullptr, 1);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&](bool settled) {
                return uploader.hand(renderer,
                    Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(),
                        .mScene = scene,
                        .mPrepared = &prepared,
                        .mSettled = settled });
            };
            const auto secondOf = [&](TextureIndex described) {
                const auto at = std::find(renderer.mDescribedSlots.begin(), renderer.mDescribedSlots.end(), described);
                EXPECT_NE(at, renderer.mDescribedSlots.end());
                return renderer.mDescribedSecondTexels[static_cast<std::size_t>(at - renderer.mDescribedSlots.begin())];
            };

            constexpr Testing::Texel box{ 122, 130, 255, 75 };
            EXPECT_EQ(hand(false).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(renderer.mDescribedSources[slot], TextureSource::StandIn) << "and not the file as it stands";
            EXPECT_EQ(scene.textures().getRows()[slot].mStoodBytes, 0u);
            EXPECT_EQ(uploader.getPendingCount(), 1u);

            prepared.flush();
            EXPECT_EQ(hand(false).mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector<TextureIndex>{ slot }));
            EXPECT_TRUE(Testing::isNear(secondOf(slot), box, 2));
            EXPECT_EQ(uploader.getPendingCount(), 0u);
            const TextureRow& stood = scene.textures().getRows()[slot];
            EXPECT_TRUE(stood.mStoodMade);
            // Its chain made whole in BC7, a block for each of its two levels: 2 × 16 = 32 bytes.
            EXPECT_EQ(stood.mStoodBytes, 32u);
            EXPECT_EQ(renderer.mDescribedBytes, (std::vector<std::uint64_t>{ 32 }));

            EXPECT_EQ(hand(false).mKind, SceneUpload::Kind::Placed);
            scene.drop(std::move(held));

            Rtx::SceneDesc picture(identities);
            Testing::addModel(picture, VFS::Path::NormalizedView("textures/one.dds"));
            TextureHold pictured = picture.takeTexture(VFS::Path::NormalizedView("textures/tx_test_nh.dds"), *file,
                TextureWrap::Repeat, TextureEncoding::Normal);
            PreparedImages fresh(nullptr, 1);
            SceneUploader once;
            Testing::CountingRenderer drawn;
            once.hand(drawn,
                Rtx::SceneUploader::Handing{
                    .mSlot = Rtx::SceneSlot::world(), .mScene = picture, .mPrepared = &fresh, .mSettled = true });
            const auto at = std::find(drawn.mDescribedSlots.begin(), drawn.mDescribedSlots.end(), pictured.get());
            ASSERT_NE(at, drawn.mDescribedSlots.end());
            EXPECT_TRUE(Testing::isNear(
                drawn.mDescribedSecondTexels[static_cast<std::size_t>(at - drawn.mDescribedSlots.begin())], box, 2));
            EXPECT_EQ(once.getPendingCount(), 0u);
            picture.drop(std::move(pictured));
        }

        /// A 4x4 RGBA8 image of one level under `path`: 4 × 4 × 4 = 64 bytes, handed to the device as it stands.
        osg::ref_ptr<osg::Image> sixtyFourBytes(std::string_view path)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            image->setInternalTextureFormat(GL_RGBA8);
            std::fill_n(image->data(), image->getTotalSizeInBytes(), static_cast<std::uint8_t>(0));
            image->setFileName(std::string(path));
            return image;
        }

        /// **A file nothing names rests on the device, and is taken again without an upload**: stood whole,
        /// 64 bytes of it, it keeps its slot when its model goes, nothing freed and the host's image let go
        /// of; the model back finds the slot it had, a mesh arriving and no texture described.
        TEST(RtxSceneUploaderTest, aFileNothingNamesRestsAndIsTakenAgainWithoutAnUpload)
        {
            Rtx::SceneIdentities identities;
            constexpr VFS::Path::NormalizedView path("textures/tx_rest.dds");
            const osg::ref_ptr<osg::Image> image = sixtyFourBytes(path.value());
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            const Testing::Model first = Testing::addModel(scene, path, image.get());
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(scene.textures().getRows()[first.mTexture].mStoodBytes, 64u);
            EXPECT_FALSE(scene.textures().getRows()[first.mTexture].mStoodMade);

            scene.dropInstance(first.mPlacement, Stander::Walk);
            EXPECT_TRUE(scene.textures().isLive(first.mTexture));
            EXPECT_EQ(scene.textures().getResting().getCount(), 1u);
            EXPECT_EQ(scene.textures().getResting().getBytes(), 64u);
            EXPECT_EQ(scene.textures().getRows()[first.mTexture].mImage, nullptr) << "the host's image let go of";
            EXPECT_EQ(hand().mDropped, 0u);
            EXPECT_TRUE(renderer.mDropped.empty());

            const Testing::Model again = Testing::addModel(scene, path, image.get());
            EXPECT_EQ(again.mTexture, first.mTexture);
            EXPECT_EQ(scene.textures().getResting().getCount(), 0u);
            EXPECT_EQ(scene.textures().getRows()[again.mTexture].mImage, image) << "the taker's, which a rebuild reads";
            const SceneUpload woke = hand();
            EXPECT_EQ(woke.mKind, SceneUpload::Kind::Extended) << "a mesh arrived";
            EXPECT_EQ(woke.mDescribed, 0u) << "and the device stands the texture already";

            // **One the device moves down while it rests goes**, as a rest holds the file whole and the host has none
            // to hand over again; one the scene let go of before the device said so is passed over.
            constexpr VFS::Path::NormalizedView otherPath("textures/tx_rest_other.dds");
            const osg::ref_ptr<osg::Image> otherImage = sixtyFourBytes(otherPath.value());
            const Testing::Model other = Testing::addModel(scene, otherPath, otherImage.get());
            hand();
            scene.dropInstance(other.mPlacement, Stander::Walk);
            hand();
            EXPECT_EQ(scene.evictResting(std::numeric_limits<std::uint64_t>::max()), 64u);
            ASSERT_FALSE(scene.textures().isLive(other.mTexture));
            scene.dropInstance(again.mPlacement, Stander::Walk);
            ASSERT_EQ(scene.textures().getResting().getCount(), 1u);
            renderer.mShort.push_back(ShortTexture{ .mSlot = again.mTexture, .mStoodBytes = 16, .mWholeBytes = 64 });
            renderer.mShort.push_back(ShortTexture{ .mSlot = other.mTexture, .mStoodBytes = 16, .mWholeBytes = 64 });
            hand();
            EXPECT_TRUE(scene.textures().isLive(again.mTexture)) << "freed after the placement that drops what goes";
            EXPECT_EQ(hand().mDropped, 1u) << "freed ahead of the next hand-over's placement, and dropped there";
            EXPECT_FALSE(scene.textures().isLive(again.mTexture));
            EXPECT_EQ(scene.textures().getResting().getCount(), 0u);

            // One that stands, moved down, rests on nothing while it waits for the 48 it lacks of its 64, and is
            // handed over again once the room holds them.
            const Testing::Model standing = Testing::addModel(scene, otherPath, otherImage.get());
            hand();
            ASSERT_EQ(scene.textures().getRows()[standing.mTexture].mStoodBytes, 64u);
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 47, .mBudget = std::uint64_t{ 1 } << 30 };
            renderer.mShort.push_back(ShortTexture{ .mSlot = standing.mTexture, .mStoodBytes = 16, .mWholeBytes = 64 });
            hand();
            EXPECT_EQ(scene.textures().getRows()[standing.mTexture].mStoodBytes, 0u);
            renderer.mDescribedSlots.clear();
            hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty());
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 48, .mBudget = std::uint64_t{ 1 } << 30 };
            hand();
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ standing.mTexture }));
            EXPECT_EQ(scene.textures().getRows()[standing.mTexture].mStoodBytes, 64u);
        }

        /// **The textures that rest give the device room back, the oldest to rest first**, as much as keeps room
        /// for the arrival at hand — its textures' whole files, as described, in the pools' room — beside the most any
        /// recent arrival's structures took, in the storages' room, and what waits: three files of 64 bytes arrived at
        /// once beside 8 bytes of structures, and rest in the order their models go. Nothing arriving keeps the 8 in
        /// the storages; a file of 64 arriving keeps 64 in the pools beside them; a structure waiting for 64 keeps 64
        /// besides. Unbounded, as a renderer that says none, frees none. **A rebuild frees
        /// every one**, as it describes from the host's images, which a slot that rests has let go of, **and is no
        /// arrival**: the same three built from nothing and let go of rest under no room at all.
        TEST(RtxSceneUploaderTest, theTexturesThatRestGiveTheDeviceRoomOldestFirst)
        {
            Rtx::SceneIdentities identities;
            // A budget whose share, a quarter of 4 GiB, is past anything these rest, so the room alone frees them.
            constexpr std::uint64_t sBudget = std::uint64_t{ 4 } << 30;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&](SceneUploader& by, Testing::CountingRenderer& to) {
                return by.hand(to, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            std::vector<osg::ref_ptr<osg::Image>> images;
            std::vector<Testing::Model> models;
            const auto addThree = [&] {
                models.clear();
                for (const VFS::Path::NormalizedView path :
                    { VFS::Path::NormalizedView("textures/tx_b.dds"), VFS::Path::NormalizedView("textures/tx_a.dds"),
                        VFS::Path::NormalizedView("textures/tx_c.dds") })
                {
                    images.push_back(sixtyFourBytes(path.value()));
                    models.push_back(Testing::addModel(scene, path, images.back().get()));
                }
            };

            // Built from nothing first, so the three are an extension.
            const Testing::Model first = Testing::addModel(scene, VFS::Path::NormalizedView("textures/tx_first.dds"));
            EXPECT_EQ(hand(uploader, renderer).mKind, SceneUpload::Kind::Rebuilt);
            // The extension's structures take 8 bytes beside its three files' 192, which the room kept counts: the
            // device's answer, said here of a mesh that stays. Each file's image gives back 64 once it rests.
            addThree();
            renderer.mStanding.push_back(StoodMesh{ .mMesh = first.mMesh, .mStructureBytes = 8 });
            for (const Testing::Model& model : models)
                renderer.mTextureFits.push_back(FitBytes<TextureIndex>{ .mSlot = model.mTexture, .mBytes = 64 });
            renderer.mArrivalRoom
                = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mStructureFree = 1u << 20, .mBudget = sBudget };
            const SceneUpload extended = hand(uploader, renderer);
            renderer.mArrivalRoom.reset();
            EXPECT_EQ(extended.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(extended.mDescribed, 3u);

            // The second, the first and the third, in that order, a hand-over apart, unbounded.
            for (const std::size_t going : { 1u, 0u, 2u })
            {
                scene.dropInstance(models[going].mPlacement, Stander::Walk);
                EXPECT_EQ(hand(uploader, renderer).mDropped, 0u) << "unbounded";
            }
            EXPECT_EQ(scene.textures().getResting().getCount(), 3u);
            EXPECT_EQ(scene.textures().getResting().getBytes(), 192u);

            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mStructureFree = 8, .mBudget = sBudget };
            EXPECT_EQ(hand(uploader, renderer).mDropped, 0u) << "room for the structures' 8, nothing arriving";
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mStructureFree = 7, .mBudget = sBudget };
            EXPECT_EQ(hand(uploader, renderer).mDropped, 1u) << "the structures' 8 bytes were not kept in the storages";

            // A file of 64 arriving in 63 of the pools', the structures' 8 held in the storages: the next oldest goes
            // for it.
            constexpr VFS::Path::NormalizedView arriving("textures/tx_arriving.dds");
            images.push_back(sixtyFourBytes(arriving.value()));
            Testing::addModel(scene, arriving, images.back().get());
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 63, .mStructureFree = 8, .mBudget = sBudget };
            const SceneUpload arrival = hand(uploader, renderer);
            EXPECT_EQ(arrival.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(arrival.mDropped, 1u) << "the arrival's 64 in 63";
            EXPECT_EQ(renderer.mDropped, (std::vector{ models[1].mTexture, models[0].mTexture }));
            EXPECT_EQ(scene.textures().getResting().getCount(), 1u);
            EXPECT_EQ(scene.textures().getResting().getBytes(), 64u);
            EXPECT_EQ(scene.textures().getRows()[models[1].mTexture].mPath, arriving)
                << "the arrival takes the lowest slot given back";
            EXPECT_TRUE(scene.textures().isLive(models[2].mTexture));

            // **A texture that waits for room is given it**: the last that rests, whose 64 hold the 64 it wants, goes
            // for it, the room holding the structures' 8.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 64, .mStructureFree = 8, .mBudget = sBudget };
            renderer.mWants.push_back(RoomWant{ .mWaiter = models[1].mTexture, .mHole = 64 });
            EXPECT_EQ(hand(uploader, renderer).mDropped, 0u) << "a want served before the answer saying it was read";
            EXPECT_TRUE(scene.textures().isLive(models[2].mTexture));
            EXPECT_EQ(hand(uploader, renderer).mDropped, 1u) << "the want, served ahead of the next placement";
            EXPECT_FALSE(scene.textures().isLive(models[2].mTexture));
            renderer.mArrivalRoom.reset();

            SceneUploader fresh;
            Testing::CountingRenderer other;
            EXPECT_EQ(hand(fresh, other).mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(scene.textures().getResting().getCount(), 0u);
            EXPECT_FALSE(scene.textures().isLive(models[2].mTexture));
            EXPECT_EQ(other.mDescribedSlots.size(), 2u) << "the first model's and the arrival's, which textures name";

            addThree();
            SceneUploader built;
            Testing::CountingRenderer roomless;
            roomless.mArrivalRoom = Rtx::ArrivalRoom{ .mBudget = sBudget };
            EXPECT_EQ(hand(built, roomless).mDescribed, 5u);
            for (const Testing::Model& model : models)
                scene.dropInstance(model.mPlacement, Stander::Walk);
            EXPECT_EQ(hand(built, roomless).mDropped, 0u) << "a rebuild taken for an arrival";
            EXPECT_EQ(scene.textures().getResting().getCount(), 3u);
        }

        /// **A texture the device had no room for waits as a pending slot** (`DeviceAnswer::mRoomless`): it rests on
        /// nothing, keeps its texels, and is counted in the room kept; once the device's room holds its 64 bytes it is
        /// handed over again, and stands. One whose room never comes is refused after `sMostRoomWaits` hand-overs.
        TEST(RtxSceneUploaderTest, aTextureTheDeviceHadNoRoomForWaitsAndIsHandedOverAgain)
        {
            Rtx::SceneIdentities identities;
            constexpr VFS::Path::NormalizedView path("textures/tx_waits.dds");
            const osg::ref_ptr<osg::Image> image = sixtyFourBytes(path.value());
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };
            constexpr std::uint64_t sBudget = std::uint64_t{ 4 } << 30;

            // Built first, so the rest are extensions; its texture reads no image, one refusal of its own.
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/tx_first.dds"));
            hand();
            const std::uint32_t refusedBefore = scene.refusals().count(Refused::Texture);
            const Testing::Model model = Testing::addModel(scene, path, image.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = model.mTexture, .mStoodBytes = 0 });
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = sBudget };
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 0u) << "a texture that waits stood";
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore);

            // No room yet: it waits, and is not handed over.
            renderer.mDescribedSlots.clear();
            hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty());

            // Room for its 64 bytes: handed over again, and stood.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 64, .mBudget = sBudget };
            hand();
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ model.mTexture }));
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 64u);

            // **A smaller side waits on and is never refused**, and is handed over again once the room holds what it
            // lacks of its file: 16 of 64 stood, so 48, which 47 does not hold.
            constexpr VFS::Path::NormalizedView smaller("textures/tx_smaller.dds");
            const osg::ref_ptr<osg::Image> smallerImage = sixtyFourBytes(smaller.value());
            const Testing::Model held = Testing::addModel(scene, smaller, smallerImage.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = held.mTexture, .mStoodBytes = 16 });
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 47, .mBudget = sBudget };
            hand();
            EXPECT_EQ(scene.textures().getRows()[held.mTexture].mStoodBytes, 0u) << "a smaller side rests on nothing";
            renderer.mDescribedSlots.clear();
            for (std::uint32_t waited = 0; waited <= sMostRoomWaits; ++waited)
                hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty()) << "handed over without room for what it lacks";
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore) << "a smaller side was refused";
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 48, .mBudget = sBudget };
            hand();
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ held.mTexture }));
            EXPECT_EQ(scene.textures().getRows()[held.mTexture].mStoodBytes, 64u);

            // **A smaller side the device stands short every time it is handed over stands as it is** after
            // `sMostRoomWaits` tries, and is handed over no more.
            constexpr VFS::Path::NormalizedView tries("textures/tx_tries.dds");
            const osg::ref_ptr<osg::Image> triesImage = sixtyFourBytes(tries.value());
            const Testing::Model trying = Testing::addModel(scene, tries, triesImage.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = trying.mTexture, .mStoodBytes = 16 });
            hand();
            for (std::uint32_t tried = 0; tried < sMostRoomWaits; ++tried)
            {
                renderer.mDescribedSlots.clear();
                renderer.mShort.push_back(ShortTexture{ .mSlot = trying.mTexture, .mStoodBytes = 16 });
                hand();
                EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ trying.mTexture })) << tried;
            }
            renderer.mShort.clear();
            renderer.mDescribedSlots.clear();
            hand();
            hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty()) << "handed over past its tries";
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore) << "a smaller side was refused";

            // Another whose room never comes: refused once the bound is reached, and not before.
            constexpr VFS::Path::NormalizedView never("textures/tx_never.dds");
            const osg::ref_ptr<osg::Image> neverImage = sixtyFourBytes(never.value());
            const Testing::Model stranded = Testing::addModel(scene, never, neverImage.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = stranded.mTexture, .mStoodBytes = 0 });
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = sBudget };
            hand();
            for (std::uint32_t waited = 1; waited < sMostRoomWaits; ++waited)
            {
                hand();
                EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore) << waited;
            }
            hand();
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore + 1)
                << "a room that never came ended in no refusal";

            // **A wait goes with its file**: one whose slot the scene gives back is forgotten and never refused.
            constexpr VFS::Path::NormalizedView gone("textures/tx_gone.dds");
            const osg::ref_ptr<osg::Image> goneImage = sixtyFourBytes(gone.value());
            const Testing::Model going = Testing::addModel(scene, gone, goneImage.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = going.mTexture, .mStoodBytes = 0 });
            hand();
            scene.dropInstance(going.mPlacement, Stander::Walk);
            for (std::uint32_t waited = 0; waited <= sMostRoomWaits; ++waited)
                hand();
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore + 1)
                << "a texture given back was refused";

            // **A rebuild describes every slot**, so a wait the device stood there is over: refused never, and it
            // stands what it was handed in.
            constexpr VFS::Path::NormalizedView rebuilt("textures/tx_rebuilt.dds");
            const osg::ref_ptr<osg::Image> rebuiltImage = sixtyFourBytes(rebuilt.value());
            const Testing::Model rebuilding = Testing::addModel(scene, rebuilt, rebuiltImage.get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = rebuilding.mTexture, .mStoodBytes = 0 });
            hand();
            renderer.heldAt(Rtx::SceneSlot::world()).mIdentity = SceneIdentity{};
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(scene.textures().getRows()[rebuilding.mTexture].mStoodBytes, 64u)
                << "a slot stood went on waiting";
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = sBudget };
            for (std::uint32_t waited = 0; waited <= sMostRoomWaits; ++waited)
                hand();
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore + 1)
                << "a texture the rebuild stood was refused";
        }

        /// **What rests stands under nine tenths of what the budget leaves of what is in use** (`restLimitOf`), where
        /// in use is the driver's usage less what rests, freed oldest first though the room wants nothing. A budget of
        /// 6,000 with 4,000 used, 1,000 of it resting, has 3,000 in use and leaves 3,000, of which 3,000 - 300 = 2,700
        /// may rest; with nothing resting, 2,000 left and 1,800; a usage past the budget nothing.
        TEST(RtxSceneUploaderTest, whatRestsStandsUnderNineTenthsOfWhatTheBudgetLeaves)
        {
            Rtx::SceneIdentities identities;
            EXPECT_EQ(SceneUploader::restLimitOf(Rtx::ArrivalRoom{ .mBudget = 6000, .mHeld = 4000 }, 1000), 2700u);
            EXPECT_EQ(SceneUploader::restLimitOf(Rtx::ArrivalRoom{ .mBudget = 6000, .mHeld = 4000 }, 0), 1800u);
            EXPECT_EQ(SceneUploader::restLimitOf(Rtx::ArrivalRoom{ .mBudget = 6000, .mHeld = 7000 }, 0), 0u);
            EXPECT_EQ(SceneUploader::restLimitOf(Rtx::ArrivalRoom{ .mBudget = 6000, .mHeld = 500 }, 1000), 5400u)
                << "a usage under what rests is nothing in use";

            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            std::vector<osg::ref_ptr<osg::Image>> images;
            std::vector<Testing::Model> models;
            for (const VFS::Path::NormalizedView path : { VFS::Path::NormalizedView("textures/tx_b.dds"),
                     VFS::Path::NormalizedView("textures/tx_a.dds"), VFS::Path::NormalizedView("textures/tx_c.dds") })
            {
                images.push_back(sixtyFourBytes(path.value()));
                models.push_back(Testing::addModel(scene, path, images.back().get()));
            }
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            for (const Testing::Model& model : models)
            {
                scene.dropInstance(model.mPlacement, Stander::Walk);
                hand();
            }
            ASSERT_EQ(scene.textures().getResting().getBytes(), 192u);

            // A usage of what rests alone is nothing in use: 300 left, 270 may rest.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mBudget = 300, .mHeld = 192 };
            EXPECT_EQ(hand().mDropped, 0u);

            // 108 in use beside it: 192 left, 173 may rest, and the oldest goes.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mBudget = 300, .mHeld = 300 };
            EXPECT_EQ(hand().mDropped, 1u);
            EXPECT_EQ(scene.textures().getResting().getBytes(), 128u);

            // A budget of 100: 90 may rest.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mBudget = 100, .mHeld = 128 };
            EXPECT_EQ(hand().mDropped, 1u);
            EXPECT_FALSE(scene.textures().isLive(models[0].mTexture)) << "rested first";
            EXPECT_FALSE(scene.textures().isLive(models[1].mTexture));
            EXPECT_TRUE(scene.textures().isLive(models[2].mTexture)) << "rested last";
        }

        /// **What rests comes down to a limit that dropped a step a hand-over**, the most one extension handed the
        /// device, and the room an arrival needs at once. Five files of 64 bytes, handed over one a hand-over, rest
        /// 320 under a limit of nought: the first hand-over frees the 64 a texture wants and a step of 64 more, two
        /// files; each after it one.
        TEST(RtxSceneUploaderTest, whatRestsComesDownToALimitThatDroppedAStepAHandOver)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            std::vector<osg::ref_ptr<osg::Image>> images;
            std::vector<Testing::Model> models;
            for (const VFS::Path::NormalizedView path : { VFS::Path::NormalizedView("textures/tx_1.dds"),
                     VFS::Path::NormalizedView("textures/tx_2.dds"), VFS::Path::NormalizedView("textures/tx_3.dds"),
                     VFS::Path::NormalizedView("textures/tx_4.dds"), VFS::Path::NormalizedView("textures/tx_5.dds") })
            {
                images.push_back(sixtyFourBytes(path.value()));
                models.push_back(Testing::addModel(scene, path, images.back().get()));
                hand();
            }
            for (const Testing::Model& model : models)
            {
                scene.dropInstance(model.mPlacement, Stander::Walk);
                hand();
            }
            ASSERT_EQ(scene.textures().getResting().getBytes(), 320u);

            // A texture's want, which no resting texture surely holds, frees the oldest by its 64 once the answer is
            // taken.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = 0 };
            renderer.mWants.push_back(RoomWant{ .mWaiter = TextureIndex(0), .mHole = 64 });
            EXPECT_EQ(hand().mDropped, 1u) << "the step";
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mBudget = 0 };
            EXPECT_EQ(hand().mDropped, 2u) << "the want's, and the step";
            EXPECT_EQ(hand().mDropped, 1u);
            EXPECT_EQ(hand().mDropped, 1u);
            EXPECT_EQ(hand().mDropped, 0u);
            EXPECT_EQ(scene.textures().getResting().getBytes(), 0u);
        }

        /// **A want is served once, ahead of the placement after its answer, and again only once its room was the
        /// device's to take** (`SceneUploader::serveWants`). Three files of 64 bytes rest, giving back 32, 128 and 64:
        /// a texture's want of 64, answered once, frees the second at the next hand-over, the oldest that holds it.
        /// Answered on each hand-over after, as a device asks while it waits, it frees nothing for the two that the
        /// room takes to come back, and the third on the one after. A want of another slot is its own, and frees at
        /// once what rested longest, as no row holds its 256. **A texture that waits is not handed over again while
        /// what went for it comes back**: stood in with a want of its own and room to spare, it is handed over on the
        /// third hand-over after the want was served, and not before.
        TEST(RtxSceneUploaderTest, aWantIsServedOnceAndAgainOnlyOnceItsRoomCameBack)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 1u << 20, .mBudget = std::uint64_t{ 4 } << 30 };
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            std::vector<osg::ref_ptr<osg::Image>> images;
            std::vector<Testing::Model> models;
            for (const VFS::Path::NormalizedView path : { VFS::Path::NormalizedView("textures/tx_1.dds"),
                     VFS::Path::NormalizedView("textures/tx_2.dds"), VFS::Path::NormalizedView("textures/tx_3.dds") })
            {
                images.push_back(sixtyFourBytes(path.value()));
                models.push_back(Testing::addModel(scene, path, images.back().get()));
                hand();
            }
            constexpr std::array<std::uint64_t, 3> sFits{ 32, 128, 64 };
            for (std::size_t at = 0; at < models.size(); ++at)
                renderer.mTextureFits.push_back(
                    FitBytes<TextureIndex>{ .mSlot = models[at].mTexture, .mBytes = sFits[at] });
            hand();
            for (const Testing::Model& model : models)
            {
                scene.dropInstance(model.mPlacement, Stander::Walk);
                hand();
            }
            ASSERT_EQ(scene.textures().getResting().getCount(), 3u);

            const auto live = [&] {
                std::vector<bool> standing;
                standing.reserve(models.size());
                for (const Testing::Model& model : models)
                    standing.push_back(scene.textures().isLive(model.mTexture));
                return standing;
            };
            const RoomWant waiting{ .mWaiter = TextureIndex(9), .mHole = 64 };
            renderer.mWants.push_back(waiting);
            EXPECT_EQ(hand().mDropped, 0u);
            EXPECT_EQ(hand().mDropped, 1u);
            EXPECT_EQ(live(), (std::vector<bool>{ true, false, true }));

            for (const std::size_t dropped : { 0u, 0u, 1u })
            {
                renderer.mWants.push_back(waiting);
                EXPECT_EQ(hand().mDropped, dropped);
            }
            EXPECT_EQ(live(), (std::vector<bool>{ true, false, false }));

            renderer.mWants.push_back(RoomWant{ .mWaiter = TextureIndex(9), .mHole = 64 });
            renderer.mWants.push_back(RoomWant{ .mWaiter = TextureIndex(10), .mHole = 256 });
            hand();
            EXPECT_EQ(hand().mDropped, 1u) << "the other slot's, the first slot's cooling";
            EXPECT_EQ(live(), (std::vector<bool>{ false, false, false }));

            constexpr VFS::Path::NormalizedView late("textures/tx_late.dds");
            images.push_back(sixtyFourBytes(late.value()));
            const Testing::Model waits = Testing::addModel(scene, late, images.back().get());
            renderer.mShort.push_back(ShortTexture{ .mSlot = waits.mTexture, .mStoodBytes = 0 });
            renderer.mWants.push_back(RoomWant{ .mWaiter = waits.mTexture, .mHole = 64 });
            hand();
            renderer.mDescribedSlots.clear();
            for (std::uint32_t cooling = 0; cooling < sFramesInFlight + 1; ++cooling)
                hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty()) << "handed over while what went for it was coming back";
            hand();
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector{ waits.mTexture }));
        }

        /// **A model's header is read on the service's thread, stands in until then, and is handed over once, the
        /// host letting go of its texels after**: a DXT1 colour of 8 by 8 in three levels, 48 bytes, held by the
        /// scene as its header, stands in on the first hand-over; once read, the next hands it over whole, 48 bytes,
        /// and the manager's cache holds nothing of it after. **A rebuild that stands it in again leaves it nothing
        /// whole**, so let go of it goes, where resting it would wake the stand-in.
        TEST(RtxSceneUploaderTest, aHeaderIsHandedOverOnceItsTexelsAreReadAndLetGoOfAfter)
        {
            Rtx::SceneIdentities identities;
            TestingOpenMW::VFSTestFile file(Testing::ddsDxt1(8, 8, 3, 0x55));
            constexpr VFS::Path::NormalizedView path("textures/tx_eight.dds");
            const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({ { path, &file } });
            Resource::CacheBudget budget(Resource::CacheBudget::sUnbounded);
            Resource::ImageManager images(vfs.get(), budget);
            const Testing::DerivedHeaders derived(images);
            const osg::ref_ptr<osg::Image> header = images.getModelImage(path);
            ASSERT_TRUE(Resource::isHeader(*header));

            Rtx::SceneDesc scene(identities);
            PreparedImages prepared(nullptr, 1);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(renderer,
                    Rtx::SceneUploader::Handing{
                        .mSlot = Rtx::SceneSlot::world(), .mScene = scene, .mPrepared = &prepared });
            };

            const Testing::Model model = Testing::addModel(scene, path, header.get());
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(renderer.mDescribedSources[model.mTexture], TextureSource::StandIn);
            EXPECT_EQ(uploader.getPendingCount(), 1u);

            prepared.flush();
            const SceneUpload stood = hand();
            EXPECT_EQ(stood.mKind, SceneUpload::Kind::Extended);
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector<TextureIndex>{ model.mTexture }));
            EXPECT_EQ(renderer.mDescribedSources[model.mTexture], TextureSource::File);
            EXPECT_EQ(renderer.mDescribedBytes, (std::vector<std::uint64_t>{ 48 }));
            EXPECT_EQ(uploader.getPendingCount(), 0u);
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 48u);
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mImage, header) << "the slot keeps the header";
            EXPECT_EQ(budget.getHeld(), 0u) << "the host kept the texels the device has";

            SceneUploader fresh;
            Testing::CountingRenderer other;
            EXPECT_EQ(fresh
                          .hand(other,
                              Rtx::SceneUploader::Handing{
                                  .mSlot = Rtx::SceneSlot::world(), .mScene = scene, .mPrepared = &prepared })
                          .mKind,
                SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(other.mDescribedSources[model.mTexture], TextureSource::StandIn) << "read again";
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 0u);
            scene.dropInstance(model.mPlacement, Stander::Walk);
            EXPECT_FALSE(scene.textures().isLive(model.mTexture));
            EXPECT_EQ(scene.textures().getResting().getCount(), 0u);
            prepared.flush();
        }

        /// **A header stood short lets go of its texels, and is read again once the room comes back**: the DXT1 of 8 by
        /// 8 in three levels, 32 + 8 + 8 = 48 bytes, read, handed over and named short holds nothing in the manager's
        /// cache after; with room for its 48 bytes it is read again, handed over again when read, and holds nothing
        /// once stood. A stand-in whose room never comes is refused after `sMostRoomWaits` hand-overs.
        TEST(RtxSceneUploaderTest, aHeaderStoodShortLetsGoOfItsTexelsAndIsReadAgainForTheRoom)
        {
            Rtx::SceneIdentities identities;
            TestingOpenMW::VFSTestFile file(Testing::ddsDxt1(8, 8, 3, 0x55));
            constexpr VFS::Path::NormalizedView path("textures/tx_waiting.dds");
            const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS({ { path, &file } });
            Resource::CacheBudget budget(Resource::CacheBudget::sUnbounded);
            Resource::ImageManager images(vfs.get(), budget);
            const Testing::DerivedHeaders derived(images);
            const osg::ref_ptr<osg::Image> header = images.getModelImage(path);

            Rtx::SceneDesc scene(identities);
            PreparedImages prepared(nullptr, 1);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(renderer,
                    Rtx::SceneUploader::Handing{
                        .mSlot = Rtx::SceneSlot::world(), .mScene = scene, .mPrepared = &prepared });
            };
            const Testing::Model model = Testing::addModel(scene, path, header.get());
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = std::uint64_t{ 4 } << 30 };
            hand();
            prepared.flush();
            renderer.mShort.push_back(ShortTexture{ .mSlot = model.mTexture, .mStoodBytes = 0 });
            hand();
            EXPECT_EQ(renderer.mDescribedBytes, (std::vector<std::uint64_t>{ 48 }));
            EXPECT_EQ(budget.getHeld(), 0u) << "a header stood short kept its texels";
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 0u) << "a short texture rests on nothing";

            // The room for its 48: read again, nothing handed over until it is read.
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 48, .mBudget = std::uint64_t{ 4 } << 30 };
            renderer.mDescribedSlots.clear();
            hand();
            EXPECT_TRUE(renderer.mDescribedSlots.empty()) << "handed over before it was read";
            prepared.flush();
            hand();
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector<TextureIndex>{ model.mTexture }));
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 48u);
            EXPECT_EQ(budget.getHeld(), 0u) << "a header stood whole kept its texels";

            // **A settled hand-over hands it over on the hand-over the room comes**, its description reading and
            // waiting for the texels, so two runs agree on the frame it lands.
            renderer.mShort.push_back(ShortTexture{ .mSlot = model.mTexture, .mStoodBytes = 0 });
            scene.textures().arriveAgain(model.mTexture);
            hand();
            renderer.mDescribedSlots.clear();
            uploader.hand(renderer,
                Rtx::SceneUploader::Handing{
                    .mSlot = Rtx::SceneSlot::world(), .mScene = scene, .mPrepared = &prepared, .mSettled = true });
            EXPECT_EQ(renderer.mDescribedSlots, (std::vector<TextureIndex>{ model.mTexture }))
                << "a settled hand-over waited for a read of its own";
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 48u);

            // Another whose room never comes: refused once it waited its most, and not before.
            TestingOpenMW::VFSTestFile otherFile(Testing::ddsDxt1(8, 8, 3, 0x66));
            constexpr VFS::Path::NormalizedView otherPath("textures/tx_never_room.dds");
            const std::unique_ptr<VFS::Manager> otherVfs = TestingOpenMW::createTestVFS({ { otherPath, &otherFile } });
            Resource::ImageManager otherImages(otherVfs.get(), budget);
            const Testing::DerivedHeaders otherImagesDerived(otherImages);
            const osg::ref_ptr<osg::Image> otherHeader = otherImages.getModelImage(otherPath);
            const Testing::Model stranded = Testing::addModel(scene, otherPath, otherHeader.get());
            renderer.mArrivalRoom = Rtx::ArrivalRoom{ .mFree = 0, .mBudget = std::uint64_t{ 4 } << 30 };
            hand();
            prepared.flush();
            renderer.mShort.push_back(ShortTexture{ .mSlot = stranded.mTexture, .mStoodBytes = 0 });
            hand();
            const std::uint32_t refusedBefore = scene.refusals().count(Refused::Texture);
            for (std::uint32_t waited = 1; waited < sMostRoomWaits; ++waited)
                hand();
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore);
            hand();
            EXPECT_EQ(scene.refusals().count(Refused::Texture), refusedBefore + 1);
            prepared.flush();
        }

        /// **An arrival the device refused a texture of does not rest**, since the device names what it
        /// refused and not its slot: the file of 64 bytes is stood by nothing, and freed when its model goes.
        TEST(RtxSceneUploaderTest, anArrivalTheDeviceRefusedATextureOfDoesNotRest)
        {
            Rtx::SceneIdentities identities;
            constexpr VFS::Path::NormalizedView path("textures/tx_refused.dds");
            const osg::ref_ptr<osg::Image> image = sixtyFourBytes(path.value());
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            renderer.mRefusing.push_back(
                Refusal{ .mKind = Refused::Texture, .mName = std::string(path.value()), .mWhy = "no room" });

            const Testing::Model model = Testing::addModel(scene, path, image.get());
            uploader.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            EXPECT_EQ(scene.textures().getRows()[model.mTexture].mStoodBytes, 0u);

            scene.dropInstance(model.mPlacement, Stander::Walk);
            EXPECT_FALSE(scene.textures().isLive(model.mTexture));
            EXPECT_EQ(scene.textures().getResting().getCount(), 0u);
        }

        /// **The array's limit is reported on the hand-over that met it, whichever branch it went**:
        /// a texture the full table refused arrives nowhere, so the hand-over after it only places,
        /// and a placement describes nothing.
        TEST(RtxSceneUploaderTest, theTextureArraysLimitIsReportedOnAHandOverThatOnlyPlaces)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            SceneUploader uploader;
            Testing::CountingRenderer renderer;
            const auto hand = [&] {
                return uploader.hand(
                    renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene });
            };

            Rtx::TextureTable& textures = scene.textures();
            for (std::size_t filled = 0; textures.getLiveCount() < Rtx::TextureTable::sCapacity; ++filled)
                ASSERT_NE(textures.add(VFS::Path::Normalized("textures/fill" + std::to_string(filled) + ".dds")),
                    Rtx::sNoRow);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/fill0.dds"));
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Rebuilt);
            const std::uint32_t before = scene.refusals().count(Refused::Texture);

            EXPECT_EQ(textures.add(VFS::Path::NormalizedView("textures/over.dds")), Rtx::sNoRow);
            EXPECT_EQ(hand().mKind, SceneUpload::Kind::Placed);
            EXPECT_EQ(scene.refusals().count(Refused::Texture), before + 1) << "the limit went unreported";
            EXPECT_EQ(textures.getRefusedArrivals(), 0u) << "the hand-over kept its refusals for the next";
        }

        /// Two uploaders over one scene do not share a decision, which is what makes one per renderer
        /// the rule rather than a habit.
        TEST(RtxSceneUploaderTest, anUploaderThatHasBuiltNothingRebuildsASceneTheOtherOnlyPlaces)
        {
            Rtx::SceneIdentities identities;

            Rtx::SceneDesc scene(identities);
            Testing::addModel(scene, VFS::Path::NormalizedView("textures/one.dds"));

            SceneUploader built;
            Testing::CountingRenderer first;
            EXPECT_EQ(
                built.hand(first, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene })
                    .mKind,
                SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(
                built.hand(first, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene })
                    .mKind,
                SceneUpload::Kind::Placed);

            SceneUploader fresh;
            Testing::CountingRenderer second;
            EXPECT_EQ(
                fresh.hand(second, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = scene })
                    .mKind,
                SceneUpload::Kind::Rebuilt);
        }

        /// A renderer carrying somebody else's textures is built from nothing, not appended to.
        ///
        /// **This is `bench`.** It runs several places through one renderer with a scene of its own for each, so
        /// the second place meets an array holding the first place's images. An uploader reading that count as its
        /// own would describe at 375 in a table 231 long: an overrun, not a wrong picture, which surfaces as an
        /// allocation failure from somewhere unrelated to textures.
        TEST(RtxSceneUploaderTest, aSecondSceneOnOneRendererIsBuiltRatherThanAppendedTo)
        {
            Rtx::SceneIdentities identities;
            Testing::CountingRenderer renderer;

            // The longer scene first, so appending onto its count would run off the end of the
            // shorter one — which is the failure, rather than merely describing the wrong images.
            Rtx::SceneDesc crowded(identities);
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/one.dds"));
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/two.dds"));
            Testing::addModel(crowded, VFS::Path::NormalizedView("textures/three.dds"));

            SceneUploader place;
            ASSERT_EQ(
                place.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = crowded })
                    .mKind,
                SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(renderer.mTextures, 3u);

            Rtx::SceneDesc sparse(identities);
            Testing::addModel(sparse, VFS::Path::NormalizedView("textures/four.dds"));

            SceneUploader next;
            const SceneUpload second = next.hand(
                renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = sparse });
            EXPECT_EQ(second.mKind, SceneUpload::Kind::Rebuilt);
            EXPECT_EQ(second.mDescribed, std::size_t{ 1 }) << "the descriptions began past the end of the table";
            EXPECT_EQ(renderer.mTextures, 1u);

            // And the same uploader carries on with the scene it did build, so the guard costs the
            // ordinary frame nothing.
            EXPECT_EQ(
                next.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = sparse })
                    .mKind,
                SceneUpload::Kind::Placed);

            // **A description is what it is and not where it is.** Moved, it keeps its identity
            // and the slot built from it is still its own; a description made after it is
            // another, at whatever address, and gets a build of its own.
            Rtx::SceneDesc moved = std::move(sparse);
            EXPECT_EQ(moved.getIdentity(), renderer.describeHeld(Rtx::SceneSlot::world()).mIdentity);
            EXPECT_EQ(
                next.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = moved })
                    .mKind,
                SceneUpload::Kind::Placed)
                << "a move was taken for another description";

            Rtx::SceneDesc another(identities);
            EXPECT_NE(another.getIdentity(), moved.getIdentity());
            EXPECT_NE(another.getIdentity(), Rtx::SceneIdentity{});
        }
        /// A picture inside the interface is handed over the same way a cell is, and neither
        /// disturbs the other.
        ///
        /// **This is what a race-creation slider drag costs.** The doll is walked again on every frame of the drag,
        /// the placements thrown away and refilled, and nothing arrives or goes, so the second hand and every one
        /// after it is a placement. Rebuilding each time would make an acceleration structure and a texture array
        /// from nothing sixty times a second.
        TEST(RtxSceneUploaderTest, aPictureIsHandedOverTheSameWayACellIsAndNeitherDisturbsTheOther)
        {
            Rtx::SceneIdentities identities;
            Testing::CountingRenderer renderer;

            Rtx::SceneDesc world(identities);
            Testing::addModel(world, VFS::Path::NormalizedView("textures/ground.dds"));
            Testing::addModel(world, VFS::Path::NormalizedView("textures/wall.dds"));

            Rtx::SceneDesc doll(identities);
            const Testing::Model body = Testing::addModel(doll, VFS::Path::NormalizedView("textures/skin.dds"));

            const Rtx::SceneSlot slot = renderer.addViewScene();

            SceneUploader ofTheWorld;
            SceneUploader ofTheDoll;

            ASSERT_EQ(
                ofTheWorld
                    .hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = world })
                    .mKind,
                SceneUpload::Kind::Rebuilt);
            ASSERT_EQ(ofTheDoll.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = slot, .mScene = doll }).mKind,
                SceneUpload::Kind::Rebuilt);

            // Each table is its own length, and building the doll did not append onto the world's.
            EXPECT_EQ(renderer.textureCountOf(Rtx::SceneSlot::world()), 2u);
            EXPECT_EQ(renderer.textureCountOf(slot), 1u);
            EXPECT_FALSE(renderer.mAppendedToWrongEnd);

            // The drag: the placements are thrown away and the same body walked back in, which is
            // what `SceneExtractor` does when its identity maps recognise every drawable — it places
            // and adds nothing.
            for (std::int32_t frame = 0; frame < 3; ++frame)
            {
                doll.clearPlacement();
                doll.addInstance(Rtx::MeshInstance{ .mMesh = body.mMesh, .mMaterial = body.mMaterial });

                EXPECT_EQ(ofTheDoll.hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = slot, .mScene = doll }).mKind,
                    SceneUpload::Kind::Placed)
                    << "redraw " << frame << " rebuilt a subject that did not change";
            }

            // And the world is still the world's: the doll's frames did not make its next one a
            // rebuild.
            world.clearPlacement();
            world.addInstance(Rtx::MeshInstance{ .mMesh = MeshIndex(0), .mMaterial = MaterialIndex(0) });
            EXPECT_EQ(
                ofTheWorld
                    .hand(renderer, Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(), .mScene = world })
                    .mKind,
                SceneUpload::Kind::Placed);
            EXPECT_EQ(renderer.textureCountOf(Rtx::SceneSlot::world()), 2u);
            EXPECT_EQ(renderer.textureCountOf(slot), 1u);
        }

    }
}
