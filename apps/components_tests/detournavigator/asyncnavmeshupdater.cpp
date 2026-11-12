#include "settings.hpp"
#include <memory>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/detournavigator/asyncnavmeshupdater.hpp>
#include <components/detournavigator/clock.hpp>
#include <components/detournavigator/makenavmesh.hpp>
#include <components/detournavigator/navmeshtilestore.hpp>
#include <components/detournavigator/preparednavmeshdata.hpp>
#include <components/files/conversion.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/testing/util.hpp>

#include <BulletCollision/CollisionShapes/btBoxShape.h>

#include <DetourNavMesh.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace
{
    using namespace testing;
    using namespace DetourNavigator;
    using namespace DetourNavigator::Tests;

    void addHeightFieldPlane(
        TileCachedRecastMeshManager& recastMeshManager, const osg::Vec2i cellPosition = osg::Vec2i(0, 0))
    {
        const int cellSize = 8192;
        recastMeshManager.addHeightfield(cellPosition, cellSize, HeightfieldPlane{ 0 }, nullptr);
    }

    void addObject(const btBoxShape& shape, TileCachedRecastMeshManager& recastMeshManager,
        const std::string& fileHash = "test_hash")
    {
        const ObjectId id(&shape);
        auto bulletShape = std::make_shared<Resource::BulletShape>();
        constexpr VFS::Path::NormalizedView test("test.nif");
        bulletShape->mFileName = test;
        bulletShape->mFileHash = fileHash;
        const CollisionShape collisionShape(std::make_shared<Resource::BulletShapeInstance>(bulletShape), shape);
        recastMeshManager.addObject(id, collisionShape, btTransform::getIdentity(), AreaType_ground, nullptr);
    }

    /// A clock a test moves itself, read by the updater's workers as well as by the test.
    class ManualClock final : public Clock
    {
    public:
        std::chrono::steady_clock::time_point now() const override { return mNow.load(); }

        void advance(std::chrono::steady_clock::duration by) { mNow.store(mNow.load() + by); }

    private:
        /// Past the epoch, which a job's process time holds where it is not delayed.
        std::atomic<std::chrono::steady_clock::time_point> mNow{ std::chrono::steady_clock::time_point(
            std::chrono::hours(1)) };
    };

    struct DetourNavigatorAsyncNavMeshUpdaterTest : Test
    {
        Settings mSettings = makeSettings();
        TileCachedRecastMeshManager mRecastMeshManager{ mSettings.mRecast };
        OffMeshConnectionsManager mOffMeshConnectionsManager{ mSettings.mRecast };
        const AgentBounds mAgentBounds{ CollisionShapeType::Aabb, { 29, 29, 66 } };
        const TilePosition mPlayerTile{ 0, 0 };
        const ESM::RefId mWorldspace = ESM::RefId::stringRefId("sys::default");
        const btBoxShape mBox{ btVector3(100, 100, 20) };
        Loading::Listener mListener;
    };

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, for_all_jobs_done_when_empty_wait_should_terminate)
    {
        AsyncNavMeshUpdater updater{ mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr };
        updater.wait(WaitConditionType::allJobsDone, &mListener);
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, for_required_tiles_present_when_empty_wait_should_terminate)
    {
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        updater.wait(WaitConditionType::requiredTilesPresent, &mListener);
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, post_should_generate_navmesh_tile)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);
        EXPECT_EQ(updater.getStats().mPosted, 1);
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, wait_requires_rebuilt_tile)
    {
        struct UnlockOnLoad : Loading::Listener
        {
            ScopedUpdateGuard mGuard;

            void loadingOn() override { mGuard.reset(); }
        };

        mSettings.mMinUpdateInterval = std::chrono::milliseconds(0);
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);

        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, { { mPlayerTile, ChangeType::add } });
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        ASSERT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);

        mRecastMeshManager.removeHeightfield(osg::Vec2i(0, 0), nullptr);
        mRecastMeshManager.takeChangedTiles(nullptr);
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, { { mPlayerTile, ChangeType::remove } });
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        ASSERT_EQ(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);

        addHeightFieldPlane(mRecastMeshManager);
        UnlockOnLoad listener;
        listener.mGuard = mRecastMeshManager.makeUpdateGuard();
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, { { mPlayerTile, ChangeType::update } });
        updater.wait(WaitConditionType::requiredTilesPresent, &listener);
        EXPECT_FALSE(listener.mGuard);
        EXPECT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);

        listener.mGuard.reset();
        updater.wait(WaitConditionType::allJobsDone, &mListener);
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, repeated_post_should_lead_to_cache_hit)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            ASSERT_EQ(stats.mCache.mGetCount, 1);
            ASSERT_EQ(stats.mCache.mHitCount, 0);
        }
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            EXPECT_EQ(stats.mCache.mGetCount, 2);
            EXPECT_EQ(stats.mCache.mHitCount, 1);
        }
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, post_for_update_change_type_should_not_update_cache)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::update } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            ASSERT_EQ(stats.mCache.mGetCount, 1);
            ASSERT_EQ(stats.mCache.mHitCount, 0);
        }
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            EXPECT_EQ(stats.mCache.mGetCount, 2);
            EXPECT_EQ(stats.mCache.mHitCount, 0);
        }
    }

    /// **A generated tile is written to the store under the key of its mesh**, read back as the store's, a tile
    /// of a model with no file hash too: the key is the triangles, whatever file made them.
    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, post_should_write_generated_tile_to_store)
    {
        for (const std::string fileHash : { "test_hash", "" })
        {
            SCOPED_TRACE(fileHash);
            TileCachedRecastMeshManager recastMeshManager(mSettings.mRecast);
            recastMeshManager.setWorldspace(mWorldspace, nullptr);
            addHeightFieldPlane(recastMeshManager);
            addObject(mBox, recastMeshManager, fileHash);
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / ("navmesh" + fileHash);
            const TilePosition tilePosition{ 0, 0 };
            {
                AsyncNavMeshUpdater updater(mSettings, recastMeshManager, mOffMeshConnectionsManager,
                    std::make_unique<NavMeshTileStore>(folder));
                const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
                const std::map<TilePosition, ChangeType> changedTiles{ { tilePosition, ChangeType::add } };
                updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
                updater.wait(WaitConditionType::allJobsDone, &mListener);
                EXPECT_EQ(updater.getStats().mStoreWrites, 1u);
                updater.stop();
            }
            const auto recastMesh = recastMeshManager.getMesh(mWorldspace, tilePosition);
            ASSERT_NE(recastMesh, nullptr);
            const ContentStore::Key key
                = NavMeshTileStore::keyOf(mSettings.mRecast, mAgentBounds, mWorldspace, tilePosition, *recastMesh);
            NavMeshTileStore store(folder);
            ASSERT_TRUE(store.holds(key));
            const std::unique_ptr<PreparedNavMeshData> stored = store.read(key);
            ASSERT_NE(stored, nullptr);
            EXPECT_EQ(stored->mUserId, 1u) << "a tile the store answered";
        }
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, post_should_read_from_store_on_cache_miss)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mMaxNavMeshTilesCacheSize = 0;
        auto store = std::make_unique<NavMeshTileStore>(TestingOpenMW::currentTestDirPath() / "navmesh");
        NavMeshTileStore* const storePtr = store.get();
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, std::move(store));
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            ASSERT_EQ(stats.mCache.mGetCount, 1);
            ASSERT_EQ(stats.mCache.mHitCount, 0);
            ASSERT_TRUE(stats.mStore.has_value());
            ASSERT_EQ(stats.mStore->mReads, 1);
            ASSERT_EQ(stats.mStoreHits, 0);
            ASSERT_EQ(stats.mStoreWrites, 1);
        }
        // The write is the store's own thread's, and done before the second read.
        storePtr->flush();
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        {
            const auto stats = updater.getStats();
            EXPECT_EQ(stats.mCache.mGetCount, 2);
            EXPECT_EQ(stats.mCache.mHitCount, 0);
            ASSERT_TRUE(stats.mStore.has_value());
            EXPECT_EQ(stats.mStore->mReads, 2);
            EXPECT_EQ(stats.mStoreHits, 1);
            EXPECT_EQ(stats.mStoreWrites, 1) << "a tile the store answered is not written again";
        }
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, on_changing_player_tile_post_should_remove_tiles_out_of_range)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTilesAdd{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTilesAdd);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        ASSERT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);
        const std::map<TilePosition, ChangeType> changedTilesRemove{ { TilePosition{ 0, 0 }, ChangeType::remove } };
        const TilePosition playerTile(100, 100);
        updater.post(mAgentBounds, navMeshCacheItem, playerTile, mWorldspace, changedTilesRemove);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_EQ(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);
    }

    // Timed by a clock the test moves, so the tiles posted again stand within the interval of their last
    // update however long the first batch took to build: on the wall clock, a slow runner under
    // ThreadSanitizer built it for longer than the interval, and no tile was delayed.
    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, repeated_tile_updates_should_be_delayed)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);

        mSettings.mMaxTilesNumber = 9;
        mSettings.mMinUpdateInterval = std::chrono::milliseconds(250);

        ManualClock clock;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr, clock);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);

        std::map<TilePosition, ChangeType> changedTiles;

        for (int x = -3; x <= 3; ++x)
            for (int y = -3; y <= 3; ++y)
                changedTiles.emplace(TilePosition{ x, y }, ChangeType::update);

        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);

        updater.wait(WaitConditionType::allJobsDone, &mListener);

        {
            const AsyncNavMeshUpdaterStats stats = updater.getStats();
            EXPECT_EQ(stats.mJobs, 0);
            EXPECT_EQ(stats.mWaiting.mDelayed, 0);
        }

        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);

        {
            const AsyncNavMeshUpdaterStats stats = updater.getStats();
            EXPECT_EQ(stats.mJobs, 49);
            EXPECT_EQ(stats.mWaiting.mDelayed, 49);
        }

        clock.advance(mSettings.mMinUpdateInterval);
        updater.wait(WaitConditionType::allJobsDone, &mListener);

        {
            const AsyncNavMeshUpdaterStats stats = updater.getStats();
            EXPECT_EQ(stats.mJobs, 0);
            EXPECT_EQ(stats.mWaiting.mDelayed, 0);
        }

        // Posted again once the interval has passed since they were built, none is delayed.
        clock.advance(mSettings.mMinUpdateInterval);
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        EXPECT_EQ(updater.getStats().mWaiting.mDelayed, 0);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_EQ(updater.getStats().mJobs, 0);
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, should_write_debug_recast_mesh)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteRecastMeshToFile = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
        mSettings.mRecastMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_TRUE(std::filesystem::exists(dir / "0.0.recastmesh.obj"));
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, should_write_debug_recast_mesh_with_revision)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteRecastMeshToFile = true;
        mSettings.mEnableRecastMeshFileNameRevision = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
        mSettings.mRecastMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_TRUE(std::filesystem::exists(dir / "0.0.recastmesh.1.2.obj"));
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, writing_recast_mesh_to_absent_file_should_not_fail_tile_generation)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteRecastMeshToFile = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath() / "absent";
        mSettings.mRecastMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);
        EXPECT_FALSE(std::filesystem::exists(dir));
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, should_write_debug_navmesh)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteNavMeshToFile = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
        mSettings.mNavMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_TRUE(std::filesystem::exists(dir / "all_tiles_navmesh.bin"));
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, should_write_debug_navmesh_with_revision)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteNavMeshToFile = true;
        mSettings.mEnableNavMeshFileNameRevision = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
        mSettings.mNavMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_TRUE(std::filesystem::exists(dir / "all_tiles_navmesh.1.1.bin"));
    }

    TEST_F(DetourNavigatorAsyncNavMeshUpdaterTest, writing_navmesh_to_absent_file_should_not_fail_tile_generation)
    {
        mRecastMeshManager.setWorldspace(mWorldspace, nullptr);
        addHeightFieldPlane(mRecastMeshManager);
        mSettings.mEnableWriteNavMeshToFile = true;
        const std::filesystem::path dir = TestingOpenMW::currentTestDirPath() / "absent";
        mSettings.mNavMeshPathPrefix = Files::pathToUnicodeString(dir) + "/";
        Log(Debug::Verbose) << mSettings.mRecastMeshPathPrefix;
        AsyncNavMeshUpdater updater(mSettings, mRecastMeshManager, mOffMeshConnectionsManager, nullptr);
        const auto navMeshCacheItem = std::make_shared<GuardedNavMeshCacheItem>(1, mSettings);
        const std::map<TilePosition, ChangeType> changedTiles{ { TilePosition{ 0, 0 }, ChangeType::add } };
        updater.post(mAgentBounds, navMeshCacheItem, mPlayerTile, mWorldspace, changedTiles);
        updater.wait(WaitConditionType::allJobsDone, &mListener);
        EXPECT_NE(navMeshCacheItem->lockConst()->getImpl().getTileRefAt(0, 0, 0), 0u);
        EXPECT_FALSE(std::filesystem::exists(dir));
    }

    struct DetourNavigatorSpatialJobQueueTest : Test
    {
        const AgentBounds mAgentBounds{ CollisionShapeType::Aabb, osg::Vec3f(1, 1, 1) };
        const std::shared_ptr<GuardedNavMeshCacheItem> mNavMeshCacheItemPtr;
        const std::weak_ptr<GuardedNavMeshCacheItem> mNavMeshCacheItem = mNavMeshCacheItemPtr;
        const ESM::RefId mWorldspace = ESM::RefId::stringRefId("worldspace");
        const TilePosition mChangedTile{ 0, 0 };
        const std::chrono::steady_clock::time_point mProcessTime{};
        const TilePosition mPlayerTile{ 0, 0 };
        const int mMaxTiles = 9;
    };

    TEST_F(DetourNavigatorSpatialJobQueueTest, should_store_multiple_jobs_per_tile)
    {
        std::list<Job> jobs;
        SpatialJobQueue queue;

        const ESM::RefId worldspace1 = ESM::RefId::stringRefId("worldspace1");
        const ESM::RefId worldspace2 = ESM::RefId::stringRefId("worldspace2");

        queue.push(jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, worldspace1, mChangedTile, ChangeType::remove, mProcessTime));
        queue.push(jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, worldspace2, mChangedTile, ChangeType::update, mProcessTime));

        ASSERT_EQ(queue.size(), 2);

        const auto job1 = queue.pop(mChangedTile);
        ASSERT_TRUE(job1.has_value());
        EXPECT_EQ((*job1)->mWorldspace, worldspace1);

        const auto job2 = queue.pop(mChangedTile);
        ASSERT_TRUE(job2.has_value());
        EXPECT_EQ((*job2)->mWorldspace, worldspace2);

        EXPECT_EQ(queue.size(), 0);
    }

    struct DetourNavigatorJobQueueTest : DetourNavigatorSpatialJobQueueTest
    {
        /// The time a test asks the queue at where it states none of its own: every job's `mProcessTime`, so no
        /// job is delayed.
        const std::chrono::steady_clock::time_point mNow = mProcessTime;
    };

    TEST_F(DetourNavigatorJobQueueTest, pop_should_return_nullptr_from_empty)
    {
        JobQueue queue;
        ASSERT_FALSE(queue.hasJob(mNow));
        ASSERT_FALSE(queue.pop(mPlayerTile, mNow).has_value());
    }

    TEST_F(DetourNavigatorJobQueueTest, push_on_change_type_remove_should_add_to_removing)
    {
        const std::chrono::steady_clock::time_point processTime{};

        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::remove, processTime);

        JobQueue queue;
        queue.push(job, mNow);

        EXPECT_EQ(queue.getStats().mRemoving, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, pop_should_return_last_removing)
    {
        std::list<Job> jobs;
        JobQueue queue;

        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(0, 0),
                       ChangeType::remove, mProcessTime),
            mNow);
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(1, 0),
                       ChangeType::remove, mProcessTime),
            mNow);

        ASSERT_TRUE(queue.hasJob(mNow));
        const auto job = queue.pop(mPlayerTile, mNow);
        ASSERT_TRUE(job.has_value());
        EXPECT_EQ((*job)->mChangedTile, TilePosition(1, 0));
    }

    TEST_F(DetourNavigatorJobQueueTest, push_on_change_type_not_remove_should_add_to_updating)
    {
        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::update, mProcessTime);

        JobQueue queue;
        queue.push(job, mNow);

        EXPECT_EQ(queue.getStats().mUpdating, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, pop_should_return_nearest_to_player_tile)
    {
        std::list<Job> jobs;

        JobQueue queue;
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(0, 0),
                       ChangeType::update, mProcessTime),
            mNow);
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(1, 0),
                       ChangeType::update, mProcessTime),
            mNow);

        ASSERT_TRUE(queue.hasJob(mNow));
        const auto job = queue.pop(TilePosition(1, 0), mNow);
        ASSERT_TRUE(job.has_value());
        EXPECT_EQ((*job)->mChangedTile, TilePosition(1, 0));
    }

    TEST_F(DetourNavigatorJobQueueTest, push_on_processing_time_more_than_now_should_add_to_delayed)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::update, processTime);

        JobQueue queue;
        queue.push(job, now);

        EXPECT_EQ(queue.getStats().mDelayed, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, pop_should_return_when_delayed_job_is_ready)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::update, processTime);

        JobQueue queue;
        queue.push(job, now);

        ASSERT_FALSE(queue.hasJob(now));
        ASSERT_FALSE(queue.pop(mPlayerTile, now).has_value());

        ASSERT_TRUE(queue.hasJob(processTime));
        EXPECT_TRUE(queue.pop(mPlayerTile, processTime).has_value());
    }

    TEST_F(DetourNavigatorJobQueueTest, pop_should_return_ready_delayed_nearer_to_player_before_farther_updating)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        JobQueue queue;
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(5, 0),
                       ChangeType::update, mProcessTime),
            now);
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(0, 0),
                       ChangeType::update, processTime),
            now);

        // Before it is due, the delayed job waits and the farther one goes.
        const auto early = queue.pop(mPlayerTile, now);
        ASSERT_TRUE(early.has_value());
        EXPECT_EQ((*early)->mChangedTile, TilePosition(5, 0));

        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(5, 0),
                       ChangeType::update, mProcessTime),
            now);
        const auto due = queue.pop(mPlayerTile, processTime);
        ASSERT_TRUE(due.has_value());
        EXPECT_EQ((*due)->mChangedTile, TilePosition(0, 0));
        EXPECT_EQ(queue.getStats().mDelayed, 0);
        EXPECT_EQ(queue.getStats().mUpdating, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, update_should_move_ready_delayed_to_updating)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::update, processTime);

        JobQueue queue;
        queue.push(job, now);

        ASSERT_EQ(queue.getStats().mDelayed, 1);

        queue.update(mPlayerTile, mMaxTiles, processTime);

        EXPECT_EQ(queue.getStats().mDelayed, 0);
        EXPECT_EQ(queue.getStats().mUpdating, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, update_should_move_ready_delayed_to_removing_when_out_of_range)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        const JobIt job = jobs.emplace(
            jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile, ChangeType::update, processTime);

        JobQueue queue;
        queue.push(job, now);

        ASSERT_EQ(queue.getStats().mDelayed, 1);

        queue.update(TilePosition(10, 10), mMaxTiles, processTime);

        EXPECT_EQ(queue.getStats().mDelayed, 0);
        EXPECT_EQ(queue.getStats().mRemoving, 1);
        EXPECT_EQ(job->mChangeType, ChangeType::remove);
    }

    TEST_F(DetourNavigatorJobQueueTest, update_should_move_updating_to_removing_when_out_of_range)
    {
        std::list<Job> jobs;

        JobQueue queue;
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, mChangedTile,
                       ChangeType::update, mProcessTime),
            mNow);
        queue.push(jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(10, 10),
                       ChangeType::update, mProcessTime),
            mNow);

        ASSERT_EQ(queue.getStats().mUpdating, 2);

        queue.update(TilePosition(10, 10), mMaxTiles, mNow);

        EXPECT_EQ(queue.getStats().mUpdating, 1);
        EXPECT_EQ(queue.getStats().mRemoving, 1);
    }

    TEST_F(DetourNavigatorJobQueueTest, clear_should_remove_all)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point processTime = now + std::chrono::seconds(1);

        std::list<Job> jobs;
        const JobIt removing = jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace,
            TilePosition(0, 0), ChangeType::remove, mProcessTime);
        const JobIt updating = jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace,
            TilePosition(1, 0), ChangeType::update, mProcessTime);
        const JobIt delayed = jobs.emplace(jobs.end(), mAgentBounds, mNavMeshCacheItem, mWorldspace, TilePosition(2, 0),
            ChangeType::update, processTime);

        JobQueue queue;
        queue.push(removing, mNow);
        queue.push(updating, mNow);
        queue.push(delayed, now);

        ASSERT_EQ(queue.getStats().mRemoving, 1);
        ASSERT_EQ(queue.getStats().mUpdating, 1);
        ASSERT_EQ(queue.getStats().mDelayed, 1);

        queue.clear();

        EXPECT_EQ(queue.getStats().mRemoving, 0);
        EXPECT_EQ(queue.getStats().mUpdating, 0);
        EXPECT_EQ(queue.getStats().mDelayed, 0);
    }
}
