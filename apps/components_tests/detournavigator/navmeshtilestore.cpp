#include "settings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <BulletCollision/CollisionShapes/btBoxShape.h>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/heightfieldshape.hpp>
#include <components/detournavigator/makenavmesh.hpp>
#include <components/detournavigator/navmeshtilestore.hpp>
#include <components/detournavigator/objectid.hpp>
#include <components/detournavigator/objecttransform.hpp>
#include <components/detournavigator/preparednavmeshdata.hpp>
#include <components/detournavigator/recastmesh.hpp>
#include <components/detournavigator/serialization.hpp>
#include <components/detournavigator/tilecachedrecastmeshmanager.hpp>
#include <components/esm/refid.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/testing/util.hpp>

namespace DetourNavigator
{
    namespace
    {
        /// What a tile is made of, each part one the key must follow.
        struct TileInput
        {
            Settings mSettings = Tests::makeSettings();
            AgentBounds mAgentBounds{ CollisionShapeType::Aabb, { 29, 29, 66 } };
            ESM::RefId mWorldspace = ESM::RefId::stringRefId("sys::default");
            TilePosition mTilePosition{ 0, 0 };
            std::array<float, 4> mHeights{ 0, 10, 20, 30 };
            float mWaterLevel = -25;
            std::string mPath = "meshes/box.nif";
            std::string mFileHash = "box hash";
            AreaType mAreaType = AreaType_ground;
            float mScale = 1;
        };

        /// The recast mesh `input` makes, as the game's manager makes it: a heightfield surface, a cell's water and a
        /// box, all in cell (0, 0).
        std::shared_ptr<RecastMesh> meshOf(const TileInput& input, const btBoxShape& box)
        {
            TileCachedRecastMeshManager manager(input.mSettings.mRecast);
            manager.setWorldspace(input.mWorldspace, nullptr);
            const int cellSize = 8192;
            manager.addHeightfield(osg::Vec2i(0, 0), cellSize,
                HeightfieldSurface{ .mHeights = input.mHeights.data(), .mSize = 2, .mMinHeight = 0, .mMaxHeight = 30 },
                nullptr);
            manager.addWater(osg::Vec2i(0, 0), cellSize, input.mWaterLevel, nullptr);

            auto shape = std::make_shared<Resource::BulletShape>();
            shape->mFileName = VFS::Path::Normalized(input.mPath);
            shape->mFileHash = input.mFileHash;
            ObjectTransform transform{};
            transform.mScale = input.mScale;
            const CollisionShape collision(std::make_shared<Resource::BulletShapeInstance>(shape), box, transform);
            manager.addObject(ObjectId(&box), collision, btTransform::getIdentity(), input.mAreaType, nullptr);
            return manager.getMesh(input.mWorldspace, input.mTilePosition);
        }

        std::optional<ContentStore::Key> keyOf(const TileInput& input)
        {
            const btBoxShape box(btVector3(100, 100, 20));
            const std::shared_ptr<RecastMesh> mesh = meshOf(input, box);
            if (mesh == nullptr)
                return std::nullopt;
            return NavMeshTileStore::keyOf(
                input.mSettings.mRecast, input.mAgentBounds, input.mWorldspace, input.mTilePosition, *mesh);
        }

        /// **One input is one key, and every part of a tile's input moves it**: a mod that replaces a mesh moves
        /// its file hash, one that adds, moves or scales an object moves the sources, and one that edits the land
        /// or the water moves the heights or the level. A key a part did not move would hand a changed tile the
        /// tile its content had before.
        TEST(DetourNavigatorTileStoreTest, eachPartOfATilesInputMovesItsKey)
        {
            const TileInput base;
            const std::optional<ContentStore::Key> key = keyOf(base);
            ASSERT_TRUE(key.has_value());
            EXPECT_EQ(keyOf(base), key) << "the same input keys the same";

            const std::vector<std::pair<const char*, std::function<void(TileInput&)>>> changes{
                { "a recast setting", [](TileInput& v) { v.mSettings.mRecast.mMaxClimb += 1; } },
                { "the agent's shape", [](TileInput& v) { v.mAgentBounds.mShapeType = CollisionShapeType::Cylinder; } },
                { "the agent's extents", [](TileInput& v) { v.mAgentBounds.mHalfExtents.z() += 1; } },
                { "the worldspace", [](TileInput& v) { v.mWorldspace = ESM::RefId::stringRefId("other"); } },
                { "the tile", [](TileInput& v) { v.mTilePosition = TilePosition(1, 0); } },
                { "a height", [](TileInput& v) { v.mHeights[2] += 1; } },
                { "the water", [](TileInput& v) { v.mWaterLevel += 1; } },
                { "the model's path", [](TileInput& v) { v.mPath = "meshes/crate.nif"; } },
                { "the model's file", [](TileInput& v) { v.mFileHash = "replaced"; } },
                { "the area", [](TileInput& v) { v.mAreaType = AreaType_null; } },
                { "the scale", [](TileInput& v) { v.mScale = 2; } },
            };
            for (const auto& [name, change] : changes)
            {
                TileInput changed = base;
                change(changed);
                const std::optional<ContentStore::Key> moved = keyOf(changed);
                ASSERT_TRUE(moved.has_value()) << name;
                EXPECT_NE(moved, key) << name;
            }
        }

        /// **The objects' order is no part of the key**: the recast mesh lists a tile's sources as its index
        /// finds them, which follows the order the objects were added in, and two starts that load one content
        /// add them in different orders. Two crates added in either order key one tile.
        TEST(DetourNavigatorTileStoreTest, theOrderObjectsWereAddedInIsNoPartOfTheKey)
        {
            const TileInput input;
            const btBoxShape first(btVector3(100, 100, 20));
            const btBoxShape second(btVector3(40, 40, 60));
            const auto keyAdding
                = [&](const btBoxShape& a, const std::string& aHash, const btBoxShape& b, const std::string& bHash) {
                      TileCachedRecastMeshManager manager(input.mSettings.mRecast);
                      manager.setWorldspace(input.mWorldspace, nullptr);
                      const auto add = [&](const btBoxShape& box, const std::string& hash, float x) {
                          auto shape = std::make_shared<Resource::BulletShape>();
                          shape->mFileName = VFS::Path::Normalized(input.mPath);
                          shape->mFileHash = hash;
                          ObjectTransform transform{};
                          transform.mPosition.pos[0] = x;
                          transform.mScale = 1;
                          const CollisionShape collision(
                              std::make_shared<Resource::BulletShapeInstance>(shape), box, transform);
                          manager.addObject(ObjectId(&box), collision,
                              btTransform(btMatrix3x3::getIdentity(), btVector3(x, 0, 0)), AreaType_ground, nullptr);
                      };
                      add(a, aHash, &a == &first ? 0.f : 300.f);
                      add(b, bHash, &b == &first ? 0.f : 300.f);
                      const std::shared_ptr<RecastMesh> mesh = manager.getMesh(input.mWorldspace, input.mTilePosition);
                      return NavMeshTileStore::keyOf(
                          input.mSettings.mRecast, input.mAgentBounds, input.mWorldspace, input.mTilePosition, *mesh);
                  };
            const std::optional<ContentStore::Key> forward = keyAdding(first, "first", second, "second");
            const std::optional<ContentStore::Key> backward = keyAdding(second, "second", first, "first");
            ASSERT_TRUE(forward.has_value());
            EXPECT_EQ(forward, backward);
        }

        /// **A source with no file hash keys nothing**: its path alone would keep the tile of a file a mod
        /// replaced under the same name.
        TEST(DetourNavigatorTileStoreTest, aSourceWithNoFileHashKeysNothing)
        {
            TileInput input;
            input.mFileHash.clear();
            EXPECT_EQ(keyOf(input), std::nullopt);
        }

        /// **A tile written is read back whole by another store on the same folder**, marked as the store's, and a
        /// payload that does not read is discarded, its file gone, so the tile made again is written in its place.
        TEST(DetourNavigatorTileStoreTest, aWrittenTileReadsBackAndAPayloadThatDoesNotReadIsDiscarded)
        {
            const TileInput input;
            const btBoxShape box(btVector3(100, 100, 20));
            const std::shared_ptr<RecastMesh> mesh = meshOf(input, box);
            ASSERT_NE(mesh, nullptr);
            const std::unique_ptr<PreparedNavMeshData> made = prepareNavMeshTileData(
                *mesh, input.mWorldspace, input.mTilePosition, input.mAgentBounds, input.mSettings.mRecast);
            ASSERT_NE(made, nullptr);
            const std::optional<ContentStore::Key> key = keyOf(input);
            ASSERT_TRUE(key.has_value());

            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "navmesh";
            {
                NavMeshTileStore store(folder);
                EXPECT_FALSE(store.holds(*key));
                store.write(*key, *made);
            }

            NavMeshTileStore store(folder);
            ASSERT_TRUE(store.holds(*key));
            const std::unique_ptr<PreparedNavMeshData> read = store.read(*key);
            ASSERT_NE(read, nullptr);
            EXPECT_EQ(read->mUserId, 1u);
            PreparedNavMeshData expected(*made);
            expected.mUserId = 1;
            EXPECT_EQ(serialize(*read), serialize(expected));

            // A payload of the right length and digest that the tile's codec refuses, as a fault in this code would
            // write one: the store's own file, under the key and the version.
            const std::filesystem::path spoilt = TestingOpenMW::currentTestDirPath() / "spoilt";
            {
                ContentStore::Store raw(spoilt, ContentStore::Policy{ .mKindCount = 1, .mHolding = std::nullopt });
                raw.write(*key, 0, NavMeshTileStore::sTileVersion, Misc::ByteBuffer(64, std::byte{ 7 }),
                    ContentStore::Reach::Disk);
            }
            NavMeshTileStore refusing(spoilt);
            ASSERT_TRUE(refusing.holds(*key));
            EXPECT_EQ(refusing.read(*key), nullptr);
            refusing.flush();
            EXPECT_FALSE(refusing.holds(*key)) << "the file stands";
        }

        /// **The version holds what makes a tile and its key**: a tile made from a fixed input, and the key of that
        /// input, digested beside `NavMeshTileStore::sTileVersion`. A change to the key's layout, the recast mesh
        /// builder, the collision or Recast that moves either fails here until the version moves, and the stored
        /// tiles of the version before are then never read.
        TEST(DetourNavigatorTileVersionTest, theTileVersionRecordsWhatMakesATileAndItsKey)
        {
            const TileInput input;
            const btBoxShape box(btVector3(100, 100, 20));
            const std::shared_ptr<RecastMesh> mesh = meshOf(input, box);
            ASSERT_NE(mesh, nullptr);
            const std::unique_ptr<PreparedNavMeshData> made = prepareNavMeshTileData(
                *mesh, input.mWorldspace, input.mTilePosition, input.mAgentBounds, input.mSettings.mRecast);
            ASSERT_NE(made, nullptr);
            const std::optional<ContentStore::Key> key = keyOf(input);
            ASSERT_TRUE(key.has_value());

            const std::vector<std::byte> tile = serialize(*made);
            ContentStore::Digest digest("guard", 0);
            digest.add(std::span<const std::byte>(tile));

            constexpr std::uint32_t recordedVersion = 2;
            constexpr std::uint64_t recordedTile = 0x1227044d8b7b87b1;
            constexpr std::uint64_t recordedKey = 0x7aac888bc481659b;
            EXPECT_EQ(NavMeshTileStore::sTileVersion, recordedVersion)
                << "a version raised without its digests recorded again";
            EXPECT_EQ(digest.getKey().mHash[0], recordedTile)
                << "the tile moved under version " << NavMeshTileStore::sTileVersion << ", 0x" << std::hex
                << digest.getKey().mHash[0];
            EXPECT_EQ(key->mHash[0], recordedKey) << "the key moved under version " << NavMeshTileStore::sTileVersion
                                                  << ", 0x" << std::hex << key->mHash[0];
        }
    }
}
