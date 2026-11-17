#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/memorylimits.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/renderer/roomwaits.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/blockedbuffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/slottable.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/scene/bottomlevelstore.hpp>
#include <components/rtxvulkan/scene/structurebuild.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// A level grid of `side` quads a side at height `z`: a cell's ground at sixty-four, which
        /// is large enough that a tight copy of its structure is smaller than the loose one.
        Index addGrid(SceneDesc& scene, const std::uint32_t side, const float z)
        {
            std::vector<osg::Vec3f> positions;
            std::vector<osg::Vec3f> normals;
            std::vector<std::uint32_t> indices;
            for (std::uint32_t y = 0; y <= side; ++y)
                for (std::uint32_t x = 0; x <= side; ++x)
                {
                    positions.emplace_back(static_cast<float>(x), static_cast<float>(y), z);
                    normals.emplace_back(0.0f, 0.0f, 1.0f);
                }
            for (std::uint32_t y = 0; y < side; ++y)
                for (std::uint32_t x = 0; x < side; ++x)
                {
                    const std::uint32_t corner = y * (side + 1) + x;
                    indices.insert(indices.end(),
                        { corner, corner + 1, corner + side + 2, corner, corner + side + 2, corner + side + 1 });
                }
            return scene.addMesh(MeshArrays{ .mPositions = positions, .mNormals = normals, .mIndices = indices });
        }

        struct RtxBottomLevelStoreTest : Testing::DeviceTest
        {
            SceneDesc mScene;
            SlotBlocks mPoses{ Shaders::VERTEX_BLOCK, sizeof(osg::Vec3f) };
            BlockedBuffer mPositions{ Shaders::VERTEX_BLOCK, sizeof(osg::Vec3f) };
            BlockedBuffer mIndices{ Shaders::INDEX_BLOCK, sizeof(std::uint32_t) };
            bool mOpened = false;

            /// Puts the scene's static positions and index runs on the device, and room for its bodies'
            /// poses, as `SceneBuffers` and `SceneAcceleration` do, which is what a build reads through. Again after
            /// meshes are added, which writes every run once more.
            void stage()
            {
                const Device& device = getDevice();
                const SceneDesc& tables = mScene;

                if (!mOpened)
                {
                    mPoses.open(device, 1, sBuildInputUsage, "poses");
                    mPositions.open(device, sBuildInputUsage, "positions");
                    mIndices.open(device, sBuildInputUsage, "indices");
                    mOpened = true;
                }

                Batch setup(getPool());
                mPoses.reserve(setup, tables.deformers().getBindVertexCount());
                mPositions.reserve(setup, static_cast<std::uint32_t>(tables.meshes().getPositions().size()));
                mIndices.reserve(setup, static_cast<std::uint32_t>(tables.meshes().getIndices().size()));
                for (const MeshRange& range : tables.meshes().getRows())
                {
                    if (!range.deforms())
                        mPositions.writeAt(
                            setup, range.mVertices.mOffset, range.mVertices.in(tables.meshes().getPositions()));
                    mIndices.writeAt(setup, range.mIndices.mOffset, range.mIndices.in(tables.meshes().getIndices()));
                }
                orderStagedWrites(setup);
                setup.flush();

                mPoses.settle(FrameSlot{});
            }

            /// Builds and waits, so the answers are on the device when this returns.
            void build(BottomLevelStore& store, std::span<const Index> meshes,
                BuildCompaction compaction = BuildCompaction::Streamed)
            {
                Batch batch(getPool());
                std::vector<Refusal> refused;
                store.build(
                    batch, mScene, meshes, mPoses.at(FrameSlot{}), mPositions, mIndices, 0, compaction, refused);
                EXPECT_TRUE(refused.empty()) << "a device with room refused a structure";
                batch.flush();
            }

            /// Builds into a batch that rides the pool's next submit, which nothing has made yet.
            void buildDeferred(BottomLevelStore& store, std::span<const Index> meshes)
            {
                Batch batch(getPool());
                std::vector<Refusal> refused;
                store.build(batch, mScene, meshes, mPoses.at(FrameSlot{}), mPositions, mIndices, 0,
                    BuildCompaction::Streamed, refused);
                EXPECT_TRUE(refused.empty()) << "a device with room refused a structure";
                batch.defer();
            }

            /// One placement: what it copies is recorded and run, so the next can build on it.
            const SlotSet& place(BottomLevelStore& store)
            {
                const SlotSet& moved = store.prepareCompaction();
                if (!moved.empty())
                {
                    Batch batch(getPool());
                    store.recordCompaction(batch.getCommands(), nullptr);
                    batch.flush();
                }
                return moved;
            }
        };

        /// A structure's answer is read once the submit that carried its question has run, and not before, whatever
        /// was built after it.
        ///
        /// **The store counted placements and called the count a fence**: a route that builds on every frame never
        /// reached the quiet it waited for, and a crossing that placed twice in one frame ran the count ahead of the
        /// queue. A build that waited is readable on the next placement however many builds followed; one deferred
        /// into a submit nothing has made is not readable until that submit has run.
        /// **One spelling of a bottom level's flags**, which the first build and every refit and rebuild take: a still
        /// structure may be compacted and never updated, a refit one updated and never compacted, and both are
        /// traced fast and fetch their corners.
        TEST_F(RtxBottomLevelStoreTest, aStillStructureCompactsAndARefitOneUpdates)
        {
            constexpr VkBuildAccelerationStructureFlagsKHR every
                = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR;
            EXPECT_EQ(BottomLevelStore::buildFlagsOf(StructureMotion::Still),
                every | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
            EXPECT_EQ(BottomLevelStore::buildFlagsOf(StructureMotion::Refit),
                every | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        }

        TEST_F(RtxBottomLevelStoreTest, anAnswerIsReadOnceItsOwnSubmitHasRunAndNotBefore)
        {
            const std::array<Index, 3> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 64, 2.0f) };
            stage();

            BottomLevelStore store(getDevice());

            // Waited for, so the first placement reads it — and the build after it changes nothing.
            build(store, std::span(grids).subspan(0, 1));
            build(store, std::span(grids).subspan(1, 1));
            const SlotSet& atOne = place(store);
            EXPECT_TRUE(atOne.has(grids[0])) << "a question whose submit has run was not read";
            EXPECT_TRUE(atOne.has(grids[1])) << "a question whose submit has run was not read";

            // Deferred and never submitted: the timeline has not passed the value it rides, so the
            // placement reads nothing, and it is not the count of placements that decides.
            buildDeferred(store, std::span(grids).subspan(2, 1));
            EXPECT_TRUE(place(store).empty()) << "an answer read before its submit could have run";
            EXPECT_TRUE(place(store).empty()) << "an answer read before its submit could have run";

            // Submitted and waited, and the next placement reads it.
            getPool().finishDeferred();
            EXPECT_TRUE(place(store).has(grids[2])) << "an answer whose submit has run was not read";

            EXPECT_TRUE(place(store).empty()) << "something was copied twice";
            EXPECT_EQ(store.getCompactableBytes(), 0u) << "an answer outlived its copy";
            EXPECT_EQ(store.getCompactableNowBytes(), 0u) << "an answer outlived its copy";

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// A structure released before its answer is read is neither read nor copied, and what the
        /// report says is left to save does not count it.
        TEST_F(RtxBottomLevelStoreTest, aStructureReleasedBeforeItsAnswerIsReadIsForgotten)
        {
            const std::array<Index, 2> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f) };
            stage();

            BottomLevelStore store(getDevice());
            build(store, grids);

            store.release(std::span(grids).subspan(0, 1));
            EXPECT_EQ(store.getStructure(grids[0]), VK_NULL_HANDLE);

            // **A release of a slot this never built, or built and already released, is nothing**
            // — what the hand-over relies on: a slot the scene took and gave back inside one frame
            // reaches the backend as gone, and the last word of a slot given back and taken over
            // is arrived, so the same slot can be told gone again a frame later.
            const std::array<Index, 2> unbuilt{ grids[0], static_cast<Index>(mScene.meshes().size() + 7) };
            store.release(unbuilt);
            EXPECT_EQ(store.getStructure(grids[0]), VK_NULL_HANDLE);
            EXPECT_NE(store.getStructure(grids[1]), VK_NULL_HANDLE);

            const SlotSet& moved = place(store);
            EXPECT_FALSE(moved.has(grids[0])) << "a released structure was copied";
            EXPECT_TRUE(moved.has(grids[1])) << "the structure still standing was not copied";

            EXPECT_EQ(store.getCompactableBytes(), 0u);
            EXPECT_EQ(store.getCompactableNowBytes(), 0u);

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **A question the query pool is replaced under is asked again through the new one.** The pool holds one
        /// query a mesh slot and is made again where the scene outgrows it, and a question still in a batch the queue
        /// has not reached is lost with the old pool. So the first grid is asked through the pool of two, the scene
        /// grows to five, and the build that outgrows it asks the first grid again beside the three it builds, handed
        /// over out of slot order, around the second grid that was never built. Every answer then reads from the new
        /// pool.
        TEST_F(RtxBottomLevelStoreTest, aQuestionThePoolIsReplacedUnderIsAskedAgain)
        {
            const Index first = addGrid(mScene, 64, 0.0f);
            const Index unbuilt = addGrid(mScene, 64, 1.0f);
            stage();

            BottomLevelStore store(getDevice());
            buildDeferred(store, std::span(&first, 1));

            const std::array<Index, 3> late{ addGrid(mScene, 64, 4.0f), addGrid(mScene, 64, 2.0f),
                addGrid(mScene, 64, 3.0f) };
            stage();
            build(store, late);

            // **A structure is said at its build and not at its copy** (`takeAnswer`): the build's size is the same
            // in every process, and the copy's lands whenever the device answers, so a rest bounded on it would free
            // other rows in a process whose queue waits behind the host.
            DeviceAnswer answer;
            store.takeAnswer(answer);
            EXPECT_EQ(answer.mMeshes.size(), late.size() + 1)
                << "the first grid and the three late ones, at their builds";
            EXPECT_TRUE(answer.mMeshFits.empty()) << "a structure standing loose gave back room a copy takes";
            answer.clear();

            const SlotSet& moved = place(store);
            store.takeAnswer(answer);
            EXPECT_TRUE(answer.mMeshes.empty()) << "a copy made tight said its size";
            EXPECT_TRUE(moved.has(first)) << "a question lost with the pool it was asked through";
            for (const Index grid : late)
                EXPECT_TRUE(moved.has(grid)) << "grid " << grid << " built beside the pool's growth";
            EXPECT_FALSE(moved.has(unbuilt)) << "a slot never built was asked about";
            EXPECT_EQ(moved.getSlots().size(), 4u);

            EXPECT_TRUE(place(store).empty()) << "something was copied twice";
            EXPECT_EQ(store.getCompactableBytes(), 0u) << "an answer outlived its copy";

            // **What each copy gives back once it rests is its room**: the four copied, and nothing else, whose rooms
            // are every unit the kept storage holds once the loose rooms are given back.
            std::vector<Index> fitted;
            VkDeviceSize fits = 0;
            for (const FitBytes& fit : answer.mMeshFits)
            {
                fitted.push_back(fit.mSlot);
                fits += fit.mBytes;
                EXPECT_EQ(fit.mBytes % StructureStorage::sAlignment, 0u) << "a room not in the storage's units";
            }
            std::ranges::sort(fitted);
            std::vector<Index> copied{ first, late[0], late[1], late[2] };
            std::ranges::sort(copied);
            EXPECT_EQ(fitted, copied);
            getDevice().waitIdle();
            getDevice().collectIdle();
            EXPECT_TRUE(place(store).empty());
            EXPECT_EQ(store.getLooseLiveBytes(), 0u);
            EXPECT_EQ(store.getLiveBytes(), fits);
            answer.clear();
            store.takeAnswer(answer);
            EXPECT_TRUE(answer.mMeshFits.empty()) << "a copy's room said twice";

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// A mesh the device has no room for, and no room coming for, is left out: its slot holds no structure, so a
        /// row placing it names none and the top level skips it, and the meshes built before it stand.
        ///
        /// **Room for the frame's memory and none for content's**: the build's scratch and staged positions are the
        /// frame's and are made, and the structure's room is content's and is refused. The store's blocks are a unit,
        /// so the first grid's block is its size and the second has nowhere to go but a block of its own.
        ///
        /// **Room that is coming is waited for**: a texture of 64 bytes rests, which the scene's owner gives back for
        /// what waits, so the mesh refused then waits and is not refused; a loose build waits for the copies, and wants
        /// no resting mesh's room. Once the room is there, `buildWaiting` stands it and it waits no more; and where it
        /// is not there for `sMostRoomWaits` placements, the mesh is refused.
        TEST_F(RtxBottomLevelStoreTest, aMeshTheDeviceHasNoRoomForIsLeftOutAndPlacesNothing)
        {
            const std::array<Index, 3> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 64, 2.0f) };
            stage();

            BottomLevelStore store(getDevice(), sStructureBuildRoom, StructureStorage::sAlignment);
            build(store, std::span(grids).subspan(0, 1));
            EXPECT_FALSE(store.wasRefusedRoom());

            // Nothing resting and nothing buried: no room is coming.
            getDevice().waitIdle();
            getDevice().collectIdle();
            ASSERT_EQ(getDevice().getGraveyard().getBuried(MemoryUse::Structure), 0u);
            ASSERT_EQ(getDevice().getGraveyard().getBuried(MemoryUse::Texture), 0u);

            std::vector<Refusal> refused;
            {
                const Testing::NoRoomForContent full(getDevice());
                Batch batch(getPool());
                store.build(batch, mScene, std::span(grids).subspan(1, 1), mPoses.at(FrameSlot{}), mPositions, mIndices,
                    0, BuildCompaction::Streamed, refused);
                batch.flush();
            }

            ASSERT_EQ(refused.size(), 1u);
            EXPECT_EQ(refused[0].mKind, Refused::Mesh);
            EXPECT_EQ(refused[0].mWhy, "no device memory is left for it");

            EXPECT_FALSE(store.stands(grids[1]));
            EXPECT_EQ(store.getStructure(grids[1]), VK_NULL_HANDLE);
            EXPECT_EQ(store.getAddress(grids[1]), 0u) << "a row placing a mesh left out would name a structure";
            EXPECT_TRUE(store.stands(grids[0])) << "a mesh built before the device ran out was taken down";
            EXPECT_NE(store.getAddress(grids[0]), 0u);

            EXPECT_FALSE(store.isWaiting(grids[1])) << "a mesh no room is coming for waits for it";

            EXPECT_TRUE(store.wasRefusedRoom()) << "a mesh left out for want of room";
            build(store, std::span(grids).subspan(1, 1));
            EXPECT_TRUE(store.stands(grids[1]));
            EXPECT_FALSE(store.wasRefusedRoom()) << "a refusal kept past the mesh that went without, which stands now";

            // Let go of, with a texture resting so room is coming, and refused again.
            store.release(std::span(grids).subspan(1, 1));
            const Index resting = mScene.textures().add(VFS::Path::NormalizedView("textures/tx_resting.dds"));
            mScene.textures().stand(resting, 64, false);
            mScene.drop(mScene.holdTexture(resting));
            mScene.compact();
            ASSERT_EQ(mScene.textures().getResting().getBytes(), 64u);
            {
                const Testing::NoRoomForContent full(getDevice());
                Batch batch(getPool());
                store.build(batch, mScene, std::span(grids).subspan(1, 1), mPoses.at(FrameSlot{}), mPositions, mIndices,
                    0, BuildCompaction::Streamed, refused);
                batch.flush();
            }
            EXPECT_EQ(refused.size(), 1u) << "a mesh room is coming for was refused";
            EXPECT_FALSE(store.stands(grids[1]));
            EXPECT_TRUE(store.isWaiting(grids[1]));
            DeviceAnswer answer;
            store.takeAnswer(answer);
            EXPECT_TRUE(answer.mWants.empty()) << "a loose build wanted a resting mesh's room";
            const auto firstBuilt = std::ranges::find(answer.mMeshes, grids[0], &StoodMesh::mMesh);
            ASSERT_NE(firstBuilt, answer.mMeshes.end());
            const std::uint64_t gridBytes = firstBuilt->mStructureBytes;

            {
                Batch batch(getPool());
                EXPECT_EQ(
                    store.buildWaiting(batch, mScene, mPoses.at(FrameSlot{}), mPositions, mIndices, 0, refused), 1u);
                batch.flush();
            }
            EXPECT_TRUE(store.stands(grids[1]));
            EXPECT_FALSE(store.isWaiting(grids[1]));
            EXPECT_EQ(refused.size(), 1u);

            // **What each structure takes is said once** (`takeAnswer`): the waiting grid's, the first grid's size,
            // whose geometry it shares but for its height, and nothing at the next take.
            answer.clear();
            store.takeAnswer(answer);
            const auto waited = std::ranges::find(answer.mMeshes, grids[1], &StoodMesh::mMesh);
            ASSERT_NE(waited, answer.mMeshes.end());
            EXPECT_EQ(waited->mStructureBytes, gridBytes);
            answer.clear();
            store.takeAnswer(answer);
            EXPECT_TRUE(answer.mMeshes.empty()) << "a structure's size said twice";

            // **And for `sMostRoomWaits` placements at most**, the texture resting all the while: the third grid, which
            // no structure let go of makes room for, waits for a room that does not come back, and is refused on the
            // last.
            {
                const Testing::NoRoomForContent full(getDevice());
                {
                    Batch batch(getPool());
                    store.build(batch, mScene, std::span(grids).subspan(2, 1), mPoses.at(FrameSlot{}), mPositions,
                        mIndices, 0, BuildCompaction::Streamed, refused);
                    batch.flush();
                }
                ASSERT_EQ(refused.size(), 1u) << "the third grid had room";
                for (std::uint32_t placement = 1; placement <= sMostRoomWaits; ++placement)
                {
                    Batch batch(getPool());
                    EXPECT_EQ(
                        store.buildWaiting(batch, mScene, mPoses.at(FrameSlot{}), mPositions, mIndices, 0, refused),
                        0u);
                    batch.flush();
                    EXPECT_EQ(refused.size(), placement < sMostRoomWaits ? 1u : 2u) << placement;
                    EXPECT_EQ(store.isWaiting(grids[2]), placement < sMostRoomWaits) << placement;
                }
            }
            EXPECT_EQ(refused[1].mWhy, "no device memory is left for it");
            EXPECT_FALSE(store.stands(grids[2]));

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **A copy the storage has no room for wants a resting mesh's room as large as its own** (`takeAnswer`). In a
        /// store of blocks a unit long, two grids of sixty-four are copied into blocks of their own sizes, each giving
        /// back its room once it rests; a third of the same geometry, its copy refused under no room for content, wants
        /// a hole of that room, said while its copy is owed and once. A body built to be refitted stands in that
        /// storage too, and refused while a texture rests, wants a hole of its build.
        TEST_F(RtxBottomLevelStoreTest, aCopyTheStorageHasNoRoomForWantsARoomAsLargeAsItsOwn)
        {
            const std::array<Index, 3> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 64, 2.0f) };
            stage();

            BottomLevelStore store(getDevice(), sStructureBuildRoom, StructureStorage::sAlignment);
            build(store, std::span(grids).subspan(0, 2));
            EXPECT_EQ(place(store).getSlots().size(), 2u);
            DeviceAnswer answer;
            store.takeAnswer(answer);
            ASSERT_EQ(answer.mMeshFits.size(), 2u);
            const std::uint64_t room = answer.mMeshFits[0].mBytes;
            EXPECT_EQ(answer.mMeshFits[1].mBytes, room) << "one geometry copied to two sizes";
            EXPECT_TRUE(answer.mWants.empty());

            build(store, std::span(grids).subspan(2, 1));
            {
                const Testing::NoRoomForContent full(getDevice());
                EXPECT_TRUE(place(store).empty()) << "a copy was made with no room for it";
            }
            answer.clear();
            store.takeAnswer(answer);
            EXPECT_EQ(answer.mWants,
                (std::vector<RoomWant>{ RoomWant{ .mKind = RestKind::Mesh, .mSlot = grids[2], .mHole = room } }));
            answer.clear();
            store.takeAnswer(answer);
            EXPECT_TRUE(answer.mWants.empty()) << "a want said twice";

            EXPECT_TRUE(place(store).has(grids[2])) << "the copy owed was not made once the room was there";

            const std::vector<osg::Vec3f> corners{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            const std::vector<osg::Vec3f> up(3, osg::Vec3f(0, 0, 1));
            const std::vector<std::uint32_t> triangle{ 0, 1, 2 };
            const Index body = Testing::addOneBoneBody(
                mScene, MeshArrays{ .mPositions = corners, .mNormals = up, .mIndices = triangle })
                                   .mMesh;
            const Index resting = mScene.textures().add(VFS::Path::NormalizedView("textures/tx_resting.dds"));
            mScene.textures().stand(resting, 64, false);
            mScene.drop(mScene.holdTexture(resting));
            mScene.compact();
            stage();
            std::vector<Refusal> refused;
            {
                const Testing::NoRoomForContent full(getDevice());
                Batch batch(getPool());
                store.build(batch, mScene, std::span(&body, 1), mPoses.at(FrameSlot{}), mPositions, mIndices, 0,
                    BuildCompaction::Streamed, refused);
                batch.flush();
            }
            EXPECT_TRUE(refused.empty()) << "a body room is coming for was refused";
            answer.clear();
            store.takeAnswer(answer);
            ASSERT_EQ(answer.mWants.size(), 1u);
            const RoomWant bodyWant = answer.mWants[0];
            {
                Batch batch(getPool());
                EXPECT_EQ(
                    store.buildWaiting(batch, mScene, mPoses.at(FrameSlot{}), mPositions, mIndices, 0, refused), 1u);
                batch.flush();
            }
            answer.clear();
            store.takeAnswer(answer);
            const auto built = std::ranges::find(answer.mMeshes, body, &StoodMesh::mMesh);
            ASSERT_NE(built, answer.mMeshes.end());
            EXPECT_EQ(bodyWant,
                (RoomWant{ .mKind = RestKind::Mesh,
                    .mSlot = body,
                    .mHole = alignUp(built->mStructureBytes, StructureStorage::sAlignment) }));
            EXPECT_TRUE(answer.mWants.empty()) << "a body that stands wanted room";

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **A build runs in runs that fit its room, each over the room the one before used.** Four grids of
        /// sixty-four built under a room of a byte are four runs, so the scratch is one grid's, `s`; the same four
        /// under the whole room are one run and the scratch is `4s`, which says the room is what splits them. Every
        /// structure stands either way, and a run that wrote the scratch or the positions under the build before it
        /// would be a reported hazard.
        TEST_F(RtxBottomLevelStoreTest, aBuildRunsInRunsThatFitItsRoom)
        {
            const std::array<Index, 4> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 64, 2.0f), addGrid(mScene, 64, 3.0f) };
            stage();

            const auto scratchOf = [&](const VkDeviceSize room) {
                BottomLevelStore store(getDevice(), room);
                build(store, grids);
                for (const Index grid : grids)
                    EXPECT_TRUE(store.stands(grid)) << "room " << room << ", grid " << grid;
                const VkDeviceSize scratch = store.getScratchBytes();
                getDevice().waitIdle();
                getDevice().collectIdle();
                return scratch;
            };

            const VkDeviceSize alone = scratchOf(1);
            const VkDeviceSize together = scratchOf(sStructureBuildRoom);
            ASSERT_GT(alone, 0u);
            EXPECT_EQ(together, 4 * alone) << "four grids in one run do not share one scratch";
        }

        /// **Every structure leaves the loose storage, which then holds nothing**: a body built to be refitted is
        /// built where it stays, so building it takes no loose room, and two grids of sixty-four and one of a single
        /// quad, whose tight copy the driver may answer no smaller for, are each copied out, tight or as they stand,
        /// and the loose block they were built in holds nothing once the copies have run, and stays for the next.
        TEST_F(RtxBottomLevelStoreTest, everyStructureLeavesTheLooseStorageWhichThenHoldsNothing)
        {
            const std::vector<osg::Vec3f> corners{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            const std::vector<osg::Vec3f> up(3, osg::Vec3f(0, 0, 1));
            const std::vector<std::uint32_t> triangle{ 0, 1, 2 };
            const Index body = Testing::addOneBoneBody(
                mScene, MeshArrays{ .mPositions = corners, .mNormals = up, .mIndices = triangle })
                                   .mMesh;
            const std::array<Index, 3> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 1, 2.0f) };
            stage();

            BottomLevelStore store(getDevice());
            build(store, std::span(&body, 1));
            ASSERT_TRUE(store.isUpdatable(body));
            EXPECT_GT(store.getBytes(), 0u);
            EXPECT_EQ(store.getLooseBytes(), 0u) << "a structure built to be refitted took loose room";

            build(store, grids);
            EXPECT_GT(store.getLooseBytes(), 0u);

            const SlotSet& moved = place(store);
            for (const Index grid : grids)
                EXPECT_TRUE(moved.has(grid)) << "grid " << grid << " stayed in the loose storage";
            EXPECT_FALSE(moved.has(body)) << "a structure built to be refitted was copied";

            // The loose rooms cool under the copies' submit, and the next placement hands them back.
            getPool().submitAndWait([](VkCommandBuffer) {});
            getDevice().waitIdle();
            getDevice().collectIdle();
            EXPECT_TRUE(place(store).empty());
            EXPECT_EQ(store.getLooseLiveBytes(), 0u) << "a loose structure outlived its copy";
            EXPECT_EQ(store.getLooseBytes(), sContentBlockBytes) << "the loose block was not kept";

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **The rota rebuilds the posed body built whole longest ago, one a placement**, never one built whole on the
        /// placement asking, and of two built at once the first posed. And the refit's scratch holds every update at
        /// the aligned end of the one before, with room for the costliest rebuild where its update was.
        TEST_F(RtxBottomLevelStoreTest, theRotaRebuildsTheBodyBuiltWholeLongestAgo)
        {
            const std::vector<osg::Vec3f> corners{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            const std::vector<osg::Vec3f> up(3, osg::Vec3f(0, 0, 1));
            const std::vector<std::uint32_t> triangle{ 0, 1, 2 };
            const MeshArrays arrays{ .mPositions = corners, .mNormals = up, .mIndices = triangle };
            const std::array<Index, 2> bodies{ Testing::addOneBoneBody(mScene, arrays).mMesh,
                Testing::addOneBoneBody(mScene, arrays).mMesh };
            stage();

            BottomLevelStore store(getDevice());
            build(store, bodies);

            // Both arrived at placement nought: the first posed goes first, then the other, then the first again.
            EXPECT_EQ(store.pickRebuild(bodies, 1), bodies[0]);
            EXPECT_EQ(store.pickRebuild(bodies, 2), bodies[1]);
            EXPECT_EQ(store.pickRebuild(bodies, 3), bodies[0]);
            EXPECT_EQ(store.pickRebuild(std::span(bodies.data(), 1), 3), sNoIndex) << "a body rebuilt twice at once";
            EXPECT_EQ(store.pickRebuild({}, 4), sNoIndex);

            const VkDeviceSize alignment = getDevice().getPhysicalDevice().getStructureScratchAlignment();
            VkDeviceSize updates = 0;
            VkDeviceSize rebuild = 0;
            for (const Index body : bodies)
            {
                const VkDeviceSize update
                    = alignUp(store.getScratchFor(body, VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR), alignment);
                const VkDeviceSize whole
                    = alignUp(store.getScratchFor(body, VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR), alignment);
                updates += update;
                rebuild = std::max(rebuild, whole > update ? whole - update : 0);
            }
            ASSERT_GT(updates, 0u);
            EXPECT_EQ(store.getRefitScratchBytes(), updates + rebuild);

            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **A settled build returns with every structure copied out of the loose storage**, run by run: four grids of
        /// sixty-four under a room of a byte are four runs, each built, copied and given back before the next. Nothing
        /// is left loose, so the first placement after copies nothing, and the copies, each a block of its own size
        /// in a store of blocks a unit long, leave no room between them. Streamed, the same grids stand loose until a
        /// placement copies them.
        TEST_F(RtxBottomLevelStoreTest, aSettledBuildReturnsWithEveryStructureCopiedOut)
        {
            const std::array<Index, 4> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 64, 2.0f), addGrid(mScene, 64, 3.0f) };
            stage();

            {
                BottomLevelStore store(getDevice(), 1, StructureStorage::sAlignment);
                build(store, grids, BuildCompaction::Settled);
                EXPECT_EQ(store.getLooseLiveBytes(), 0u) << "a loose structure outlived a settled build";
                EXPECT_EQ(store.getLiveBytes(), store.getBytes() - store.getLooseBytes())
                    << "copies left room between them";
                for (const Index grid : grids)
                {
                    EXPECT_TRUE(store.stands(grid)) << "grid " << grid;
                    EXPECT_NE(store.getAddress(grid), 0u) << "grid " << grid;
                }
                EXPECT_TRUE(place(store).empty()) << "a settled build left a copy for a placement";
                getDevice().waitIdle();
                getDevice().collectIdle();
            }

            BottomLevelStore streamed(getDevice(), 1);
            build(streamed, grids);
            EXPECT_GT(streamed.getLooseBytes(), 0u);
            EXPECT_EQ(place(streamed).getSlots().size(), grids.size()) << "a streamed build copied before a placement";
            getDevice().waitIdle();
            getDevice().collectIdle();
        }

        /// **Every block is content's block size**, apart from the pools textures stand in: two grids of sixty-four
        /// stand loose in one, one goes, and an arrival of a small grid and another of sixty-four takes the hole it
        /// left and the block's rest, and no second block.
        TEST_F(RtxBottomLevelStoreTest, everyBlockIsContentsBlockAndAnArrivalTakesItsHolesFirst)
        {
            const std::array<Index, 4> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 8, 2.0f), addGrid(mScene, 64, 3.0f) };
            stage();

            BottomLevelStore store(getDevice());
            build(store, std::span(grids).subspan(0, 2));
            EXPECT_EQ(store.getBytes(), sContentBlockBytes);

            // The room comes back once the submit its structure was buried under has run.
            store.release(std::span(grids).subspan(0, 1));
            getPool().submitAndWait([](VkCommandBuffer) {});
            getDevice().waitIdle();
            getDevice().collectIdle();

            build(store, std::span(grids).subspan(2, 2));
            EXPECT_EQ(store.getBytes(), sContentBlockBytes) << "an arrival made a block beside the room it had";

            // Before the store goes: a buried structure gives its room back to the store's storage.
            getDevice().waitIdle();
            getDevice().collectIdle();
        }
    }
}
