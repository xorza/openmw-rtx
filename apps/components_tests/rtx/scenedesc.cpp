#include <array>

#include <gtest/gtest.h>

#include <components/rtx/scenedesc.hpp>

namespace Rtx
{
    namespace
    {
        const std::array sQuadPositions{
            osg::Vec3f(0.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 1.0f, 0.0f),
        };

        constexpr std::array<std::uint32_t, 6> sQuadIndices{ 0, 1, 2, 0, 2, 3 };

        TEST(RtxSceneDescTest, aMeshRemembersWhereItsVerticesWent)
        {
            SceneDesc scene;

            const Index first = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index second = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);

            EXPECT_EQ(first, 0u);
            EXPECT_EQ(second, 1u);

            // Two quads: 8 vertices and 12 indices in the shared buffers, the second mesh starting
            // where the first left off.
            EXPECT_EQ(scene.getPositions().size(), 8u);
            EXPECT_EQ(scene.getIndices().size(), 12u);
            EXPECT_EQ(scene.getMeshes()[1].mVertexOffset, 4u);
            EXPECT_EQ(scene.getMeshes()[1].mIndexOffset, 6u);

            EXPECT_EQ(scene.getMeshPositions(second)[2], osg::Vec3f(1.0f, 1.0f, 0.0f));
            EXPECT_EQ(scene.getMeshIndices(second)[5], 3u);
        }

        /// A mesh without normals or texture coordinates must still leave the attribute buffers as
        /// long as the position buffer, or every vertex after it reads someone else's normal.
        TEST(RtxSceneDescTest, theAttributeBuffersStayParallelWhenAMeshBringsNoAttributes)
        {
            SceneDesc scene;

            const std::array sNormals{
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            };

            scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index withNormals = scene.addMesh(sQuadPositions, sNormals, {}, sQuadIndices);

            ASSERT_EQ(scene.getNormals().size(), scene.getPositions().size());
            ASSERT_EQ(scene.getTexCoords().size(), scene.getPositions().size());

            const MeshRange& range = scene.getMeshes()[withNormals];
            EXPECT_EQ(scene.getNormals()[range.mVertexOffset], osg::Vec3f(0.0f, 0.0f, 1.0f));
            EXPECT_EQ(scene.getNormals()[0], osg::Vec3f(0.0f, 0.0f, 0.0f));
        }

        TEST(RtxSceneDescTest, aTextureIsAddedOnceHoweverOftenItIsAskedFor)
        {
            SceneDesc scene;

            constexpr VFS::Path::NormalizedView stone("textures/tx_stone_01.dds");
            constexpr VFS::Path::NormalizedView wood("textures/tx_wood_01.dds");

            EXPECT_EQ(scene.addTexture(stone), 0u);
            EXPECT_EQ(scene.addTexture(wood), 1u);
            EXPECT_EQ(scene.addTexture(stone), 0u);
            EXPECT_EQ(scene.getTextures().size(), 2u);
        }

        TEST(RtxSceneDescTest, theCountsAreWhatTheBuffersHold)
        {
            SceneDesc scene;
            scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);

            EXPECT_EQ(scene.getTriangleCount(), 4u);
            EXPECT_EQ(scene.getMeshes()[0].getTriangleCount(), 2u);

            // 8 positions and 8 normals at 12 bytes, 8 texture coordinates at 8, 12 indices at 4.
            EXPECT_EQ(scene.getGeometryBytes(), 8u * 12u + 8u * 12u + 8u * 8u + 12u * 4u);
        }

        TEST(RtxSceneDescTest, clearingEmptiesEveryTable)
        {
            SceneDesc scene;
            const Index mesh = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index material = scene.addMaterial(Material{});
            scene.addTexture(VFS::Path::NormalizedView("textures/tx_stone_01.dds"));
            scene.addInstance(MeshInstance{ .mMesh = mesh, .mMaterial = material });

            scene.clear();

            EXPECT_TRUE(scene.getMeshes().empty());
            EXPECT_TRUE(scene.getInstances().empty());
            EXPECT_TRUE(scene.getMaterials().empty());
            EXPECT_TRUE(scene.getTextures().empty());
            EXPECT_TRUE(scene.getPositions().empty());
            EXPECT_EQ(scene.getTriangleCount(), 0u);
        }
    }
}
