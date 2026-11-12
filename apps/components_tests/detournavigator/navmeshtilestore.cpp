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
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/generatenavmeshtile.hpp>
#include <components/detournavigator/heightfieldshape.hpp>
#include <components/detournavigator/makenavmesh.hpp>
#include <components/detournavigator/navmeshtilestore.hpp>
#include <components/detournavigator/objectid.hpp>
#include <components/detournavigator/objecttransform.hpp>
#include <components/detournavigator/preparednavmeshdata.hpp>
#include <components/detournavigator/recastmesh.hpp>
#include <components/detournavigator/recastmeshprovider.hpp>
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
            btVector3 mBoxHalfExtents{ 100, 100, 20 };
            float mBoxX = 0;
            AreaType mAreaType = AreaType_ground;
            float mScale = 1;
        };

        /// A model's collision as the game hands the navigator one: the shape, and the file it was read from.
        CollisionShape collisionOf(
            const btCollisionShape& shape, std::string_view path, std::string_view hash, float scale = 1, float x = 0)
        {
            auto source = std::make_shared<Resource::BulletShape>();
            source->mFileName = VFS::Path::Normalized(path);
            source->mFileHash = hash;
            ObjectTransform transform{};
            transform.mPosition.pos[0] = x;
            transform.mScale = scale;
            return CollisionShape(std::make_shared<Resource::BulletShapeInstance>(source), shape, transform);
        }

        /// A manager over `input`'s land and water in cell (0, 0), as the game's holds them.
        void addLand(TileCachedRecastMeshManager& manager, const TileInput& input)
        {
            manager.setWorldspace(input.mWorldspace, nullptr);
            const int cellSize = 8192;
            manager.addHeightfield(osg::Vec2i(0, 0), cellSize,
                HeightfieldSurface{ .mHeights = input.mHeights.data(), .mSize = 2, .mMinHeight = 0, .mMaxHeight = 30 },
                nullptr);
            manager.addWater(osg::Vec2i(0, 0), cellSize, input.mWaterLevel, nullptr);
        }

        ContentStore::Key keyOfMesh(const TileInput& input, const RecastMesh& mesh)
        {
            return NavMeshTileStore::keyOf(
                input.mSettings.mRecast, input.mAgentBounds, input.mWorldspace, input.mTilePosition, mesh);
        }

        /// The recast mesh `input` makes, as the game's manager makes it: a heightfield surface, a cell's water and a
        /// box.
        std::shared_ptr<RecastMesh> meshOf(const TileInput& input)
        {
            TileCachedRecastMeshManager manager(input.mSettings.mRecast);
            addLand(manager, input);
            // Scaled as the game scales an object's collision, by the shape's own scaling.
            btBoxShape box(input.mBoxHalfExtents);
            box.setLocalScaling(btVector3(input.mScale, input.mScale, input.mScale));
            const CollisionShape collision = collisionOf(box, "meshes/box.nif", "box hash", input.mScale, input.mBoxX);
            manager.addObject(ObjectId(&box), collision,
                btTransform(btMatrix3x3::getIdentity(), btVector3(input.mBoxX, 0, 0)), input.mAreaType, nullptr);
            return manager.getMesh(input.mWorldspace, input.mTilePosition);
        }

        ContentStore::Key keyOf(const TileInput& input)
        {
            const std::shared_ptr<RecastMesh> mesh = meshOf(input);
            EXPECT_NE(mesh, nullptr);
            return keyOfMesh(input, *mesh);
        }

        /// **One input is one key, and every part of a tile's input moves it**: what Recast is told — the
        /// settings, the agent, the worldspace and the tile — and every triangle it builds from — the land, the
        /// water, and an object's size, place, scale and area. A key a part did not move would hand a changed tile
        /// the tile its content had before.
        TEST(DetourNavigatorTileStoreTest, eachPartOfATilesInputMovesItsKey)
        {
            const TileInput base;
            const ContentStore::Key key = keyOf(base);
            EXPECT_EQ(keyOf(base), key) << "the same input keys the same";

            const std::vector<std::pair<const char*, std::function<void(TileInput&)>>> changes{
                { "a recast setting", [](TileInput& v) { v.mSettings.mRecast.mMaxClimb += 1; } },
                { "the agent's shape", [](TileInput& v) { v.mAgentBounds.mShapeType = CollisionShapeType::Cylinder; } },
                { "the agent's extents", [](TileInput& v) { v.mAgentBounds.mHalfExtents.z() += 1; } },
                { "the worldspace", [](TileInput& v) { v.mWorldspace = ESM::RefId::stringRefId("other"); } },
                { "the tile", [](TileInput& v) { v.mTilePosition = TilePosition(1, 0); } },
                { "a height", [](TileInput& v) { v.mHeights[2] += 1; } },
                { "the water", [](TileInput& v) { v.mWaterLevel += 1; } },
                { "the object's size", [](TileInput& v) { v.mBoxHalfExtents.setZ(21); } },
                { "where the object stands", [](TileInput& v) { v.mBoxX = 50; } },
                { "the area", [](TileInput& v) { v.mAreaType = AreaType_null; } },
                { "the scale", [](TileInput& v) { v.mScale = 2; } },
            };
            for (const auto& [name, change] : changes)
            {
                TileInput changed = base;
                change(changed);
                EXPECT_NE(keyOf(changed), key) << name;
            }
        }

        /// **A model's file is no part of the key, and its triangles are**: two models of one shape key one tile,
        /// whatever their paths and file hashes, and one with no file hash is keyed as well, where a key of names
        /// handed a replaced file the tile of the one before it and stored nothing for a model with no hash.
        TEST(DetourNavigatorTileStoreTest, aModelsFileIsNoPartOfTheKeyAndItsTrianglesAre)
        {
            const TileInput input;
            const btBoxShape box(input.mBoxHalfExtents);
            const auto keyFrom = [&](std::string_view path, std::string_view hash) {
                TileCachedRecastMeshManager manager(input.mSettings.mRecast);
                addLand(manager, input);
                manager.addObject(
                    ObjectId(&box), collisionOf(box, path, hash), btTransform::getIdentity(), input.mAreaType, nullptr);
                return keyOfMesh(input, *manager.getNewMesh(input.mWorldspace, input.mTilePosition));
            };
            const ContentStore::Key key = keyFrom("meshes/box.nif", "box hash");
            EXPECT_EQ(keyFrom("meshes/crate.nif", "crate hash"), key);
            EXPECT_EQ(keyFrom("meshes/box.nif", ""), key);
        }

        /// **The objects' order is no part of the key**: two starts that load one content add a tile's objects in
        /// different orders, and the builder sorts the triangles they make. Two crates added in either order key
        /// one tile.
        TEST(DetourNavigatorTileStoreTest, theOrderObjectsWereAddedInIsNoPartOfTheKey)
        {
            const TileInput input;
            const btBoxShape first(btVector3(100, 100, 20));
            const btBoxShape second(btVector3(40, 40, 60));
            const auto keyAdding = [&](const btBoxShape& a, const btBoxShape& b) {
                TileCachedRecastMeshManager manager(input.mSettings.mRecast);
                manager.setWorldspace(input.mWorldspace, nullptr);
                const auto add = [&](const btBoxShape& box) {
                    const float x = &box == &first ? 0.f : 300.f;
                    manager.addObject(ObjectId(&box), collisionOf(box, "meshes/box.nif", "box hash", 1, x),
                        btTransform(btMatrix3x3::getIdentity(), btVector3(x, 0, 0)), AreaType_ground, nullptr);
                };
                add(a);
                add(b);
                return keyOfMesh(input, *manager.getNewMesh(input.mWorldspace, input.mTilePosition));
            };
            EXPECT_EQ(keyAdding(first, second), keyAdding(second, first));
        }

        /// **An animated part's pose moves the key, and the pose it had keys the tile it had**: a model whose
        /// collision has a moving part, a door's leaf or a lift, stands at one transform through every pose, and
        /// the key of its file and transform read a tile built in one pose back for every other. The part is a box
        /// inside a compound, moved 200 units up and back as an animation moves it.
        TEST(DetourNavigatorTileStoreTest, anAnimatedPartsPoseMovesTheKeyAndItsFormerPoseKeysTheFormerTile)
        {
            const TileInput input;
            const btBoxShape part(btVector3(100, 100, 20));
            btCompoundShape compound;
            compound.addChildShape(btTransform::getIdentity(), const_cast<btBoxShape*>(&part));
            TileCachedRecastMeshManager manager(input.mSettings.mRecast);
            addLand(manager, input);
            const ObjectId id(&compound);
            manager.addObject(id, collisionOf(compound, "meshes/lift.nif", "lift hash"), btTransform::getIdentity(),
                AreaType_ground, nullptr);
            const auto key
                = [&] { return keyOfMesh(input, *manager.getNewMesh(input.mWorldspace, input.mTilePosition)); };
            const ContentStore::Key down = key();

            compound.updateChildTransform(0, btTransform(btMatrix3x3::getIdentity(), btVector3(0, 0, 200)));
            ASSERT_TRUE(manager.updateObject(id, btTransform::getIdentity(), AreaType_ground, nullptr));
            const ContentStore::Key up = key();
            EXPECT_NE(up, down);

            compound.updateChildTransform(0, btTransform::getIdentity());
            ASSERT_TRUE(manager.updateObject(id, btTransform::getIdentity(), AreaType_ground, nullptr));
            EXPECT_EQ(key(), down);
        }

        /// **An object a script brings moves the key, and taking it away again keys the tile without it**, as a
        /// house the player has built, or a bridge a quest raises, comes and goes: the tile with it is another
        /// tile, and the tile without it the one stored before it came.
        TEST(DetourNavigatorTileStoreTest, anObjectAScriptAddsMovesTheKeyAndItsRemovalRestoresIt)
        {
            const TileInput input;
            const btBoxShape box(input.mBoxHalfExtents);
            const btBoxShape house(btVector3(300, 300, 200));
            TileCachedRecastMeshManager manager(input.mSettings.mRecast);
            addLand(manager, input);
            manager.addObject(ObjectId(&box), collisionOf(box, "meshes/box.nif", "box hash"),
                btTransform::getIdentity(), input.mAreaType, nullptr);
            const auto key
                = [&] { return keyOfMesh(input, *manager.getNewMesh(input.mWorldspace, input.mTilePosition)); };
            const ContentStore::Key without = key();

            ASSERT_TRUE(manager.addObject(ObjectId(&house), collisionOf(house, "meshes/house.nif", "house hash"),
                btTransform(btMatrix3x3::getIdentity(), btVector3(1000, 1000, 0)), AreaType_ground, nullptr));
            EXPECT_NE(key(), without);

            manager.removeObject(ObjectId(&house), nullptr);
            EXPECT_EQ(key(), without);
        }

        /// **A tile written is read back whole by another store on the same folder**, marked as the store's, and a
        /// payload that does not read is discarded, its file gone, so the tile made again is written in its place.
        TEST(DetourNavigatorTileStoreTest, aWrittenTileReadsBackAndAPayloadThatDoesNotReadIsDiscarded)
        {
            const TileInput input;
            const std::shared_ptr<RecastMesh> mesh = meshOf(input);
            ASSERT_NE(mesh, nullptr);
            const std::unique_ptr<PreparedNavMeshData> made = prepareNavMeshTileData(
                *mesh, input.mWorldspace, input.mTilePosition, input.mAgentBounds, input.mSettings.mRecast);
            ASSERT_NE(made, nullptr);
            const ContentStore::Key key = keyOfMesh(input, *mesh);

            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "navmesh";
            {
                NavMeshTileStore store(folder);
                EXPECT_FALSE(store.holds(key));
                store.write(key, *made);
            }

            NavMeshTileStore store(folder);
            ASSERT_TRUE(store.holds(key));
            const std::unique_ptr<PreparedNavMeshData> read = store.read(key);
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
                raw.write(key, 0, NavMeshTileStore::sTileVersion, Misc::ByteBuffer(64, std::byte{ 7 }),
                    ContentStore::Reach::Disk);
            }
            NavMeshTileStore refusing(spoilt);
            ASSERT_TRUE(refusing.holds(key));
            EXPECT_EQ(refusing.read(key), nullptr);
            refusing.flush();
            EXPECT_FALSE(refusing.holds(key)) << "the file stands";
        }

        /// What `openmw-navmeshtool` hands its generated tiles to, kept as a store keeps them.
        struct HeldTiles final : NavMeshTileConsumer
        {
            std::unordered_map<ContentStore::Key, std::unique_ptr<PreparedNavMeshData>, ContentStore::KeyHash> mTiles;
            std::size_t mWrites = 0;
            std::size_t mProvided = 0;

            bool holds(const ContentStore::Key& key) override { return mTiles.contains(key); }

            std::unique_ptr<PreparedNavMeshData> read(const ContentStore::Key& key) override
            {
                const auto it = mTiles.find(key);
                return it == mTiles.end() ? nullptr : std::make_unique<PreparedNavMeshData>(*it->second);
            }

            void write(const ContentStore::Key& key, const PreparedNavMeshData& data) override
            {
                ++mWrites;
                mTiles.insert_or_assign(key, std::make_unique<PreparedNavMeshData>(data));
            }

            void provide(ESM::RefId) override { ++mProvided; }
            void cancel(std::string_view reason) override { ADD_FAILURE() << "cancelled: " << reason; }
            void updateStats(const NavMeshTileConsumerStats&) override {}
        };

        /// A recast mesh provider over a manager the test holds.
        struct ManagerMeshes final : RecastMeshProvider
        {
            TileCachedRecastMeshManager& mManager;

            explicit ManagerMeshes(TileCachedRecastMeshManager& manager)
                : mManager(manager)
            {
            }

            std::shared_ptr<RecastMesh> getMesh(ESM::RefId worldspace, const TilePosition& tilePosition) const override
            {
                return mManager.getNewMesh(worldspace, tilePosition);
            }
        };

        /// **`openmw-navmeshtool` writes a tile under the key of its mesh, once**: a second run over the same content
        /// finds the tile held and writes nothing, a tile of a model with no file hash is written as well, and an
        /// animated part moved since is a tile of its own, written beside the first.
        TEST(DetourNavigatorGenerateNavMeshTileTest, aTileIsWrittenUnderItsMeshesKeyOnceAndAPoseIsATileOfItsOwn)
        {
            const TileInput input;
            const btBoxShape part(input.mBoxHalfExtents);
            btCompoundShape compound;
            compound.addChildShape(btTransform::getIdentity(), const_cast<btBoxShape*>(&part));
            TileCachedRecastMeshManager manager(input.mSettings.mRecast);
            addLand(manager, input);
            const ObjectId id(&compound);
            manager.addObject(
                id, collisionOf(compound, "meshes/lift.nif", ""), btTransform::getIdentity(), AreaType_ground, nullptr);
            const auto meshes = std::make_shared<ManagerMeshes>(manager);
            const auto tiles = std::make_shared<HeldTiles>();
            const auto generate = [&] {
                GenerateNavMeshTile(
                    input.mWorldspace, input.mTilePosition, meshes, input.mAgentBounds, input.mSettings, false, tiles)
                    .doWork();
            };

            generate();
            const ContentStore::Key down
                = keyOfMesh(input, *manager.getNewMesh(input.mWorldspace, input.mTilePosition));
            EXPECT_EQ(tiles->mWrites, 1u);
            EXPECT_TRUE(tiles->holds(down)) << "under the key of the mesh it was made from";

            generate();
            EXPECT_EQ(tiles->mWrites, 1u) << "a tile held is not made again";

            compound.updateChildTransform(0, btTransform(btMatrix3x3::getIdentity(), btVector3(0, 0, 200)));
            ASSERT_TRUE(manager.updateObject(id, btTransform::getIdentity(), AreaType_ground, nullptr));
            generate();
            EXPECT_EQ(tiles->mWrites, 2u);
            EXPECT_EQ(tiles->mTiles.size(), 2u) << "the first pose's tile beside the second's";
            EXPECT_EQ(tiles->mProvided, 3u) << "every run provides its tile once";
        }

        /// **The version holds what makes a tile and its key**: a tile made from a fixed input, and the key of that
        /// input, digested beside `NavMeshTileStore::sTileVersion`. A change to the key's layout, the recast mesh
        /// builder, the collision or Recast that moves either fails here until the version moves, and the stored
        /// tiles of the version before are then never read.
        TEST(DetourNavigatorTileVersionTest, theTileVersionRecordsWhatMakesATileAndItsKey)
        {
            const TileInput input;
            const std::shared_ptr<RecastMesh> mesh = meshOf(input);
            ASSERT_NE(mesh, nullptr);
            const std::unique_ptr<PreparedNavMeshData> made = prepareNavMeshTileData(
                *mesh, input.mWorldspace, input.mTilePosition, input.mAgentBounds, input.mSettings.mRecast);
            ASSERT_NE(made, nullptr);
            const ContentStore::Key key = keyOfMesh(input, *mesh);

            const std::vector<std::byte> tile = serialize(*made);
            ContentStore::Digest digest("guard", 0);
            digest.add(std::span<const std::byte>(tile));

            constexpr std::uint32_t recordedVersion = 3;
            constexpr std::uint64_t recordedTile = 0x1227044d8b7b87b1;
            constexpr std::uint64_t recordedKey = 0x42dbd04d759805f5;
            EXPECT_EQ(NavMeshTileStore::sTileVersion, recordedVersion)
                << "a version raised without its digests recorded again";
            EXPECT_EQ(digest.getKey().mHash[0], recordedTile)
                << "the tile moved under version " << NavMeshTileStore::sTileVersion << ", 0x" << std::hex
                << digest.getKey().mHash[0];
            EXPECT_EQ(key.mHash[0], recordedKey) << "the key moved under version " << NavMeshTileStore::sTileVersion
                                                 << ", 0x" << std::hex << key.mHash[0];
        }
    }
}
