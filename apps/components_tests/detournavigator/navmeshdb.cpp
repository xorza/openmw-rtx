#include "generate.hpp"

#include <components/detournavigator/navmeshdb.hpp>

#include <DetourAlloc.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sqlite3.h>

#include <filesystem>
#include <limits>
#include <random>
#include <string>

namespace
{
    using namespace testing;
    using namespace DetourNavigator;
    using namespace DetourNavigator::Tests;

    struct Tile
    {
        ESM::RefId mWorldspace;
        TilePosition mTilePosition;
        std::vector<std::byte> mInput;
        std::vector<std::byte> mData;
    };

    struct DetourNavigatorNavMeshDbTest : Test
    {
        NavMeshDb mDb{ ":memory:", std::numeric_limits<std::uint64_t>::max() };
        std::minstd_rand mRandom;

        std::vector<std::byte> generateData()
        {
            std::vector<std::byte> data(32);
            generateRange(data.begin(), data.end(), mRandom);
            return data;
        }

        Tile insertTile(TileId tileId, TileVersion version)
        {
            const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
            const TilePosition tilePosition{ 3, 4 };
            std::vector<std::byte> input = generateData();
            std::vector<std::byte> data = generateData();
            EXPECT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
            return { worldspace, tilePosition, std::move(input), std::move(data) };
        }
    };

    TEST_F(DetourNavigatorNavMeshDbTest, get_max_tile_id_for_empty_db_should_return_zero)
    {
        EXPECT_EQ(mDb.getMaxTileId(), TileId{ 0 });
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_tile_should_be_found_by_key)
    {
        const TileId tileId{ 146 };
        const TileVersion version{ 1 };
        const auto [worldspace, tilePosition, input, data] = insertTile(tileId, version);
        const auto result = mDb.findTile(worldspace, tilePosition, input);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->mTileId, tileId);
        EXPECT_EQ(result->mVersion, version);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_tile_should_change_max_tile_id)
    {
        insertTile(TileId{ 53 }, TileVersion{ 1 });
        EXPECT_EQ(mDb.getMaxTileId(), TileId{ 53 });
    }

    TEST_F(DetourNavigatorNavMeshDbTest, updated_tile_should_change_data)
    {
        const TileId tileId{ 13 };
        const TileVersion version{ 1 };
        auto [worldspace, tilePosition, input, data] = insertTile(tileId, version);
        generateRange(data.begin(), data.end(), mRandom);
        ASSERT_EQ(mDb.updateTile(tileId, version, data), 1);
        const auto row = mDb.getTileData(worldspace, tilePosition, input);
        ASSERT_TRUE(row.has_value());
        EXPECT_EQ(row->mTileId, tileId);
        EXPECT_EQ(row->mVersion, version);
        ASSERT_FALSE(row->mData.empty());
        EXPECT_EQ(row->mData, data);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, on_inserted_duplicate_should_throw_exception)
    {
        const TileId tileId{ 53 };
        const TileVersion version{ 1 };
        const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
        const TilePosition tilePosition{ 3, 4 };
        const std::vector<std::byte> input = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
        EXPECT_THROW(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), std::runtime_error);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_duplicate_leaves_db_in_correct_state)
    {
        const TileId tileId{ 53 };
        const TileVersion version{ 1 };
        const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
        const TilePosition tilePosition{ 3, 4 };
        const std::vector<std::byte> input = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
        EXPECT_THROW(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), std::runtime_error);
        EXPECT_NO_THROW(insertTile(TileId{ 54 }, version));
    }

    TEST_F(DetourNavigatorNavMeshDbTest, delete_tiles_at_should_remove_all_tiles_with_given_worldspace_and_position)
    {
        const TileVersion version{ 1 };
        const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
        const TilePosition tilePosition{ 3, 4 };
        const std::vector<std::byte> input1 = generateData();
        const std::vector<std::byte> input2 = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(TileId{ 53 }, worldspace, tilePosition, version, input1, data), 1);
        ASSERT_EQ(mDb.insertTile(TileId{ 54 }, worldspace, tilePosition, version, input2, data), 1);
        ASSERT_EQ(mDb.deleteTilesAt(worldspace, tilePosition), 2);
        EXPECT_FALSE(mDb.findTile(worldspace, tilePosition, input1).has_value());
        EXPECT_FALSE(mDb.findTile(worldspace, tilePosition, input2).has_value());
    }

    TEST_F(DetourNavigatorNavMeshDbTest, delete_tiles_at_except_should_leave_tile_with_given_id)
    {
        const TileId leftTileId{ 53 };
        const TileId removedTileId{ 54 };
        const TileVersion version{ 1 };
        const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
        const TilePosition tilePosition{ 3, 4 };
        const std::vector<std::byte> leftInput = generateData();
        const std::vector<std::byte> removedInput = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(leftTileId, worldspace, tilePosition, version, leftInput, data), 1);
        ASSERT_EQ(mDb.insertTile(removedTileId, worldspace, tilePosition, version, removedInput, data), 1);
        ASSERT_EQ(mDb.deleteTilesAtExcept(worldspace, tilePosition, leftTileId), 1);
        const auto left = mDb.findTile(worldspace, tilePosition, leftInput);
        ASSERT_TRUE(left.has_value());
        EXPECT_EQ(left->mTileId, leftTileId);
        EXPECT_FALSE(mDb.findTile(worldspace, tilePosition, removedInput).has_value());
    }

    TEST_F(DetourNavigatorNavMeshDbTest, delete_tiles_outside_range_should_leave_tiles_inside_given_rectangle)
    {
        TileId tileId{ 1 };
        const TileVersion version{ 1 };
        const ESM::RefId worldspace = ESM::RefId::stringRefId("sys::default");
        const std::vector<std::byte> input = generateData();
        const std::vector<std::byte> data = generateData();
        for (int x = -2; x <= 2; ++x)
        {
            for (int y = -2; y <= 2; ++y)
            {
                ASSERT_EQ(mDb.insertTile(tileId, worldspace, TilePosition{ x, y }, version, input, data), 1);
                ++tileId;
            }
        }
        const TilesPositionsRange range{ TilePosition{ -1, -1 }, TilePosition{ 2, 2 } };
        ASSERT_EQ(mDb.deleteTilesOutsideRange(worldspace, range), 16);
        for (int x = -2; x <= 2; ++x)
            for (int y = -2; y <= 2; ++y)
                ASSERT_EQ(mDb.findTile(worldspace, TilePosition{ x, y }, input).has_value(),
                    -1 <= x && x <= 1 && -1 <= y && y <= 1)
                    << "x=" << x << " y=" << y;
    }

    TEST_F(DetourNavigatorNavMeshDbTest, should_support_file_size_limit)
    {
        mDb = NavMeshDb(":memory:", 4096);
        const auto f = [&] {
            for (std::int64_t i = 1; i <= 100; ++i)
                insertTile(TileId{ i }, TileVersion{ 1 });
        };
        EXPECT_THROW(f(), std::runtime_error);
    }

    // A file's journal mode is the file's own, so a second connection reads what the cache set: a commit appends to
    // the write-ahead log rather than creating, syncing and deleting a rollback journal.
    TEST(DetourNavigatorNavMeshDbFileTest, a_file_database_should_journal_into_a_write_ahead_log)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-navmeshdb-test-" + std::to_string(std::random_device{}()) + ".db");
        {
            NavMeshDb db(path.string(), std::numeric_limits<std::uint64_t>::max());
            EXPECT_EQ(db.getMaxTileId(), TileId{ 0 });
        }

        sqlite3* handle = nullptr;
        ASSERT_EQ(sqlite3_open_v2(path.string().c_str(), &handle, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
        std::string mode;
        const auto read = [](void* into, int, char** values, char**) {
            *static_cast<std::string*>(into) = values[0];
            return 0;
        };
        EXPECT_EQ(sqlite3_exec(handle, "pragma journal_mode;", read, &mode, nullptr), SQLITE_OK);
        sqlite3_close(handle);
        EXPECT_EQ(mode, "wal");

        for (const char* suffix : { "", "-wal", "-shm" })
            std::filesystem::remove(path.string() + suffix);
    }

    // A cache another process holds open keeps the journal it has and stays a cache: switching to WAL takes a moment's
    // exclusive lock, which a reader inside a transaction refuses. The file is made a cache, put back in a rollback
    // journal, and opened again while a second connection reads it.
    TEST(DetourNavigatorNavMeshDbFileTest, a_cache_another_process_holds_should_open_in_the_journal_it_has)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-navmeshdb-test-" + std::to_string(std::random_device{}()) + ".db");
        {
            NavMeshDb db(path.string(), std::numeric_limits<std::uint64_t>::max());
        }

        sqlite3* reader = nullptr;
        ASSERT_EQ(sqlite3_open_v2(path.string().c_str(), &reader, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(reader, "pragma journal_mode = DELETE;", nullptr, nullptr, nullptr), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(reader, "begin; select count(*) from tiles;", nullptr, nullptr, nullptr), SQLITE_OK);

        EXPECT_NO_THROW({
            NavMeshDb db(path.string(), std::numeric_limits<std::uint64_t>::max());
            EXPECT_EQ(db.getMaxTileId(), TileId{ 0 });
        });

        std::string mode;
        const auto read = [](void* into, int, char** values, char**) {
            *static_cast<std::string*>(into) = values[0];
            return 0;
        };
        EXPECT_EQ(sqlite3_exec(reader, "pragma journal_mode;", read, &mode, nullptr), SQLITE_OK);
        sqlite3_exec(reader, "rollback;", nullptr, nullptr, nullptr);
        sqlite3_close(reader);
        EXPECT_EQ(mode, "delete");

        for (const char* suffix : { "", "-wal", "-shm", "-journal" })
            std::filesystem::remove(path.string() + suffix);
    }
}
