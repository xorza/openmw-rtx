#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/scenestander.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/blockedbuffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/slottable.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/scene/bottomlevelstore.hpp>

namespace Rtx
{
    namespace
    {
        /// A level grid of `side` quads a side at height `z`: a cell's ground at sixty-four, which
        /// is large enough that a tight copy of its structure is smaller than the loose one.
        MeshIndex addGrid(SceneDesc& scene, const std::uint32_t side, const float z)
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
            return Testing::addMesh(
                scene, MeshArrays{ .mPositions = positions, .mNormals = normals, .mIndices = indices });
        }

        /// A store's scene, its geometry on the device, and a ledger of the device's arena to reserve the slots an
        /// operation names, as residency would.
        struct RtxBottomLevelStoreTest : Testing::DeviceTest
        {
            SceneIdentities mIdentities;
            SceneDesc mScene{ mIdentities };
            SlotBlocks<MeshIndex> mPoses{ Shaders::VERTEX_BLOCK, sizeof(osg::Vec3f) };
            BlockedBuffer mPositions{ Shaders::VERTEX_BLOCK, sizeof(osg::Vec3f) };
            BlockedBuffer mIndices{ Shaders::INDEX_BLOCK, sizeof(std::uint32_t) };
            bool mOpened = false;
            std::optional<Testing::ArenaDevice> mArena;
            std::optional<SlotLedger> mLedger;
            std::vector<BlockIndex> mBlocks;
            DeviceEvents mEvents;

            void SetUp() override
            {
                Testing::DeviceTest::SetUp();
                mArena.emplace(getDevice());
                SlotLayout layout;
                layout.mArenaOf = { mArena->arenaOf(SlotKind::Image), mArena->arenaOf(SlotKind::Buffer) };
                mLedger.emplace(layout);
            }

            void TearDown() override
            {
                getDevice().waitIdle();
                getDevice().collectIdle();
                for (const BlockIndex block : mBlocks)
                    mArena->releaseBlock(block);
                mBlocks.clear();
                Testing::DeviceTest::TearDown();
            }

            /// Puts the scene's static positions and index runs on the device, and room for its bodies' poses, as
            /// `SceneBuffers` and `SceneAcceleration` do, which is what a build reads through. Again after meshes are
            /// added, which writes every run once more.
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
                mPositions.reserve(setup, tables.meshes().getVertexRuns().getEnd());
                mIndices.reserve(setup, tables.meshes().getIndexRuns().getEnd());
                const auto rows = tables.meshes().getRows();
                for (const MeshIndex at : everyRow<MeshIndex>(rows.size()))
                {
                    const MeshRange& range = rows[at];
                    if (range.mVertices.empty())
                        continue;
                    const MeshGeometry geometry = tables.meshes().getArrival(at);
                    if (!range.deforms())
                        mPositions.writeAt(setup, range.mVertices.mOffset, geometry.mPositions);
                    mIndices.writeAt(setup, range.mIndices.mOffset, geometry.mIndices);
                }
                orderStagedWrites(setup);
                setup.flush();

                mPoses.settle(FrameSlot{});
            }

            /// A slot of `bytes` at `alignment` in the arena's structures, the block asked and waited for where none
            /// has room, as residency reserves one.
            SlotPlace reserve(const std::uint64_t bytes, const std::uint64_t alignment)
            {
                SlotLedger& ledger = *mLedger;
                const ClassIndex of = ledger.classOf(SlotKind::Buffer, bytes, alignment);
                if (!ledger.hasRoom(of))
                {
                    const BlockIndex block = ledger.getTier(of) == SlabTier::Large
                        ? ledger.askLarge(of)
                        : ledger.askChunk(ledger.getArena(of));
                    mArena->acquireBlock(block, ledger.getArena(of), ledger.getBlockBytes(block));
                    mArena->awaitBlocks();
                    std::ignore = mArena->openFrame(mEvents);
                    for (const BlockIndex arrived : mEvents.mBlocksArrived)
                    {
                        ledger.blockArrived(arrived);
                        mBlocks.push_back(arrived);
                    }
                }
                const Slot slot
                    = ledger.reserve(of, SlotOwner{ static_cast<std::uint32_t>(mBlocks.size() * 4096 + mReserved++) });
                ledger.commit(slot);
                return SlotPlace{
                    .mSlot = slot, .mBlock = ledger.getBlockOf(slot), .mOffset = ledger.getOffsetOf(slot)
                };
            }

            /// The build residency would issue of `mesh`: priced by the store, into a slot of its own.
            BuildStructure buildOf(const MeshIndex mesh)
            {
                const MeshRange& range = mScene.meshes().getRows()[mesh];
                const ContentPrice price = BottomLevelStore::price(getDevice(),
                    StructureShape{ .mTriangles = range.getTriangleCount(),
                        .mVertices = range.mVertices.mCount,
                        .mRefits = range.deforms() });
                return BuildStructure{ .mMesh = mesh,
                    .mGeneration = mScene.meshes().generationOf(mesh),
                    .mLoose = reserve(price.mBytes, price.mAlignment),
                    .mBytes = price.mBytes };
            }

            /// `ops` done into a batch that waits, or rides the pool's next submit where `deferred`.
            void execute(BottomLevelStore& store, std::span<const ResidencyOp> ops, const bool deferred = false)
            {
                Batch batch(getPool());
                store.execute(BottomLevelStore::Executing{ .mBatch = batch,
                                  .mArena = getDevice().getContent(),
                                  .mPoses = mPoses.at(FrameSlot{}),
                                  .mPositions = mPositions,
                                  .mIndices = mIndices },
                    mScene, ops);
                if (deferred)
                    batch.defer();
                else
                    batch.flush();
            }

            /// Each mesh of `meshes` built.
            std::vector<ResidencyOp> builds(std::span<const MeshIndex> meshes)
            {
                std::vector<ResidencyOp> ops;
                for (const MeshIndex mesh : meshes)
                    ops.emplace_back(buildOf(mesh));
                return ops;
            }

            std::vector<TightSize> answers(BottomLevelStore& store)
            {
                std::vector<TightSize> read;
                store.readAnswers(SceneSlot::world(), read);
                return read;
            }

            std::uint32_t mReserved = 0;
        };

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

        /// **A still structure is built loose where it is handed, answered tight, and copied tight where the copy is
        /// handed; a body's is built where it stays and never answered**: a grid of sixty-four and a body. The build
        /// stands each in its slot at the price it was handed, both changed; once the submit has run, the grid alone
        /// is answered, smaller than built, as the scene's tenant it was built for. Its compaction into a slot of that
        /// size stands it there, the live bytes the body's and the tight size, and is answered no more.
        TEST_F(RtxBottomLevelStoreTest, aStillStructureIsAnsweredTightAndCopiedWhereItIsHanded)
        {
            const MeshIndex grid = addGrid(mScene, 64, 0.0f);
            const MeshArrays arrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices };
            const MeshIndex body = Testing::addOneBoneBody(mScene, arrays).mMesh;
            stage();

            BottomLevelStore store(getDevice());
            const std::vector<ResidencyOp> built = builds(std::array{ grid, body });
            execute(store, built);
            const VkDeviceSize gridBuilt = std::get<BuildStructure>(built[0]).mBytes;
            const VkDeviceSize bodyBuilt = std::get<BuildStructure>(built[1]).mBytes;
            ASSERT_TRUE(store.stands(grid));
            ASSERT_TRUE(store.stands(body));
            EXPECT_FALSE(store.isUpdatable(grid));
            EXPECT_TRUE(store.isUpdatable(body));
            EXPECT_EQ(store.getLiveBytes(), gridBuilt + bodyBuilt) << "a structure built at another size than priced";
            std::vector<MeshIndex> changed(store.getChanged().begin(), store.getChanged().end());
            std::ranges::sort(changed);
            EXPECT_EQ(changed, (std::vector<MeshIndex>{ grid, body }));
            store.clearChanged();

            const std::vector<TightSize> answered = answers(store);
            ASSERT_EQ(answered.size(), 1u) << "a body that refits was asked its tight size";
            EXPECT_EQ(answered[0].mMesh, grid);
            EXPECT_EQ(answered[0].mGeneration, mScene.meshes().generationOf(grid));
            EXPECT_EQ(answered[0].mScene, SceneSlot::world());
            ASSERT_LT(answered[0].mBytes, gridBuilt) << "a grid of sixty-four came out no smaller tight";
            EXPECT_EQ(store.getCompactableNowBytes(), gridBuilt);
            EXPECT_EQ(store.getCompactableBytes(), answered[0].mBytes);

            const VkDeviceAddress loose = store.getAddress(grid);
            const CopyStructure compact{ .mMesh = grid,
                .mGeneration = answered[0].mGeneration,
                .mFrom = std::get<BuildStructure>(built[0]).mLoose.mSlot,
                .mTo = reserve(answered[0].mBytes, sStructureOffsetAlignment),
                .mKind = StructureCopy::Compact };
            execute(store, std::array{ ResidencyOp(compact) });
            EXPECT_NE(store.getAddress(grid), loose) << "the copy stood where the loose structure did";
            EXPECT_EQ(store.getLiveBytes(), answered[0].mBytes + bodyBuilt);
            EXPECT_EQ(store.getCompactableBytes(), 0u) << "an answer outlived its copy";
            EXPECT_EQ(store.getCompactableNowBytes(), 0u);
            EXPECT_EQ(std::vector<MeshIndex>(store.getChanged().begin(), store.getChanged().end()),
                (std::vector<MeshIndex>{ grid }));
            EXPECT_TRUE(answers(store).empty()) << "a structure answered twice";
        }

        /// A structure's answer is read once the submit that carried its question has run, and not before, whatever
        /// was built after it: one deferred into a submit nothing has made is not readable until that submit has run.
        TEST_F(RtxBottomLevelStoreTest, anAnswerIsReadOnceItsOwnSubmitHasRunAndNotBefore)
        {
            const std::array<MeshIndex, 2> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f) };
            stage();

            BottomLevelStore store(getDevice());
            execute(store, builds(std::span(grids).subspan(0, 1)));
            execute(store, builds(std::span(grids).subspan(1, 1)), true);
            const std::vector<TightSize> first = answers(store);
            ASSERT_EQ(first.size(), 1u) << "an answer read before its submit could have run";
            EXPECT_EQ(first[0].mMesh, grids[0]);
            EXPECT_TRUE(answers(store).empty()) << "an answer read before its submit could have run";

            getPool().finishDeferred();
            const std::vector<TightSize> second = answers(store);
            ASSERT_EQ(second.size(), 1u) << "an answer whose submit has run was not read";
            EXPECT_EQ(second[0].mMesh, grids[1]);
        }

        /// **A structure dropped or released before its answer is read is forgotten**: two grids built into a submit
        /// nothing has made, the first dropped by residency, its slot named, and a slot never built released. Once the
        /// submit runs, the second alone is answered, and a drop that names another slot than the one standing drops
        /// nothing.
        TEST_F(RtxBottomLevelStoreTest, aStructureDroppedBeforeItsAnswerIsReadIsForgotten)
        {
            const std::array<MeshIndex, 2> grids{ addGrid(mScene, 64, 0.0f), addGrid(mScene, 64, 1.0f) };
            stage();

            BottomLevelStore store(getDevice());
            const std::vector<ResidencyOp> built = builds(grids);
            execute(store, built, true);
            const DropStructure dropped{ .mMesh = grids[0], .mFrom = std::get<BuildStructure>(built[0]).mLoose.mSlot };
            const DropStructure elsewhere{ .mMesh = grids[1],
                .mFrom = std::get<BuildStructure>(built[0]).mLoose.mSlot };
            execute(store, std::array{ ResidencyOp(dropped), ResidencyOp(elsewhere) }, true);
            EXPECT_FALSE(store.stands(grids[0]));
            EXPECT_TRUE(store.stands(grids[1])) << "a drop of another slot dropped what stands";
            const std::array<MeshIndex, 1> unbuilt{ MeshIndex(static_cast<std::uint32_t>(mScene.meshes().size() + 7)) };
            store.release(unbuilt);

            getPool().finishDeferred();
            const std::vector<TightSize> answered = answers(store);
            ASSERT_EQ(answered.size(), 1u);
            EXPECT_EQ(answered[0].mMesh, grids[1]);
            EXPECT_EQ(store.getLiveBytes(), std::get<BuildStructure>(built[1]).mBytes);
        }

        /// **A question the query pool is replaced under is asked again through the new one.** The pool holds one
        /// query a mesh slot and is made again where the scene outgrows it, and a question still in a batch the queue
        /// has not reached is lost with the old pool. So the first grid is asked through the pool of two, the scene
        /// grows to five, and the build that outgrows it asks the first grid again beside the three it builds, around
        /// the second grid that was never built. Every answer then reads from the new pool.
        TEST_F(RtxBottomLevelStoreTest, aQuestionThePoolIsReplacedUnderIsAskedAgain)
        {
            const MeshIndex first = addGrid(mScene, 64, 0.0f);
            const MeshIndex unbuilt = addGrid(mScene, 64, 1.0f);
            stage();

            BottomLevelStore store(getDevice());
            execute(store, builds(std::span(&first, 1)), true);

            const std::array<MeshIndex, 3> late{ addGrid(mScene, 64, 4.0f), addGrid(mScene, 64, 2.0f),
                addGrid(mScene, 64, 3.0f) };
            stage();
            execute(store, builds(late));

            std::vector<MeshIndex> answered;
            for (const TightSize& size : answers(store))
                answered.push_back(size.mMesh);
            std::ranges::sort(answered);
            std::vector<MeshIndex> built{ first, late[0], late[1], late[2] };
            std::ranges::sort(built);
            EXPECT_EQ(answered, built) << "a question lost with the pool it was asked through";
            EXPECT_EQ(std::ranges::count(answered, unbuilt), 0) << "a slot never built was asked about";
            EXPECT_TRUE(answers(store).empty()) << "something was answered twice";
        }

        /// **A structure moved out of a draining slab is cloned whole**: a grid built, then moved to a slot of its own
        /// size, stands there at the size it was built, its move counted, and is still answered tight once its build
        /// ran, for the slot it stands in now.
        TEST_F(RtxBottomLevelStoreTest, aStructureMovedIsClonedWholeToItsNewSlot)
        {
            const MeshIndex grid = addGrid(mScene, 64, 0.0f);
            stage();

            BottomLevelStore store(getDevice());
            const std::vector<ResidencyOp> built = builds(std::span(&grid, 1));
            execute(store, built);
            const auto& build = std::get<BuildStructure>(built[0]);
            const VkDeviceAddress loose = store.getAddress(grid);
            store.clearChanged();

            const CopyStructure moved{ .mMesh = grid,
                .mGeneration = build.mGeneration,
                .mFrom = build.mLoose.mSlot,
                .mTo = reserve(build.mBytes, sStructureOffsetAlignment),
                .mKind = StructureCopy::Clone };
            execute(store, std::array{ ResidencyOp(moved) });
            EXPECT_NE(store.getAddress(grid), loose);
            EXPECT_EQ(store.getLiveBytes(), build.mBytes);
            EXPECT_EQ(store.getMovedOutBytes(), build.mBytes);
            EXPECT_EQ(std::vector<MeshIndex>(store.getChanged().begin(), store.getChanged().end()),
                (std::vector<MeshIndex>{ grid }));
            const std::vector<TightSize> answered = answers(store);
            ASSERT_EQ(answered.size(), 1u);
            EXPECT_LT(answered[0].mBytes, build.mBytes);
        }

        /// **Builds run in runs whose scratch fits the build room**, one mesh past it a run of its own: four grids
        /// under a room of a byte are four runs over the one scratch, which settles at the largest grid's, and every
        /// grid stands.
        TEST_F(RtxBottomLevelStoreTest, aBuildRunsInRunsThatFitItsRoom)
        {
            const std::array<MeshIndex, 4> grids{ addGrid(mScene, 16, 0.0f), addGrid(mScene, 64, 1.0f),
                addGrid(mScene, 32, 2.0f), addGrid(mScene, 8, 3.0f) };
            stage();

            BottomLevelStore whole(getDevice());
            execute(whole, builds(std::span(grids).subspan(1, 1)));
            const VkDeviceSize largest = whole.getScratchBytes();

            BottomLevelStore store(getDevice(), 1);
            execute(store, builds(grids));
            for (const MeshIndex grid : grids)
                EXPECT_TRUE(store.stands(grid)) << "grid " << grid.at();
            EXPECT_EQ(store.getScratchBytes(), largest) << "a run held more than one mesh's scratch";
        }

        /// **The rota builds whole again the body built whole longest ago**, one a placement, and the refit's scratch
        /// holds every update and the largest rebuild past its update.
        TEST_F(RtxBottomLevelStoreTest, theRotaRebuildsTheBodyBuiltWholeLongestAgo)
        {
            const std::vector<osg::Vec3f> corners{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
            const std::vector<osg::Vec3f> up(3, osg::Vec3f(0, 0, 1));
            const std::vector<std::uint32_t> triangle{ 0, 1, 2 };
            const MeshArrays arrays{ .mPositions = corners, .mNormals = up, .mIndices = triangle };
            const std::array<MeshIndex, 2> bodies{ Testing::addOneBoneBody(mScene, arrays).mMesh,
                Testing::addOneBoneBody(mScene, arrays).mMesh };
            stage();

            BottomLevelStore store(getDevice());
            execute(store, builds(bodies));

            // Both arrived at placement nought: the first posed goes first, then the other, then the first again.
            EXPECT_EQ(store.pickRebuild(bodies, 1), bodies[0]);
            EXPECT_EQ(store.pickRebuild(bodies, 2), bodies[1]);
            EXPECT_EQ(store.pickRebuild(bodies, 3), bodies[0]);
            EXPECT_EQ(store.pickRebuild(std::span(bodies.data(), 1), 3), sNoRow) << "a body rebuilt twice at once";
            EXPECT_EQ(store.pickRebuild({}, 4), sNoRow);

            const VkDeviceSize alignment = getDevice().getPhysicalDevice().getStructureScratchAlignment();
            VkDeviceSize updates = 0;
            VkDeviceSize rebuild = 0;
            for (const MeshIndex body : bodies)
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
        }
    }
}
