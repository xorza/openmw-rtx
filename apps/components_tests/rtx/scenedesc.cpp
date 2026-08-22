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

        /// The cutoff a material is traced against, and which materials get traced against one.
        ///
        /// The blended case is the load-bearing one: Morrowind's foliage is drawn with
        /// `NiAlphaProperty` and no alpha test, so a renderer that only honoured the tested mode
        /// would find nothing to cut out. A blend that *did* name a threshold keeps its own.
        TEST(RtxSceneDescTest, onlyAMaterialWithAMaskToReadIsTracedAsACutout)
        {
            constexpr Index texture = 3;

            const Material opaque{ .mDiffuse = texture };
            EXPECT_EQ(opaque.getAlphaCutoff(), 0.0f);
            EXPECT_FALSE(opaque.isCutout());

            const Material tested{ .mDiffuse = texture, .mAlphaRef = 0.3f, .mAlphaMode = AlphaMode::Cutout };
            EXPECT_EQ(tested.getAlphaCutoff(), 0.3f);
            EXPECT_TRUE(tested.isCutout());

            const Material blended{ .mDiffuse = texture, .mAlphaMode = AlphaMode::Blend };
            EXPECT_EQ(blended.getAlphaCutoff(), 0.5f);
            EXPECT_TRUE(blended.isCutout());

            const Material blendedWithRef{ .mDiffuse = texture, .mAlphaRef = 0.8f, .mAlphaMode = AlphaMode::Blend };
            EXPECT_EQ(blendedWithRef.getAlphaCutoff(), 0.8f);

            // The mask lives in the diffuse map's alpha, so a cutoff with no map to read it from is
            // not a cutout — and marking it one would cost traversal a candidate loop that could
            // only ever say yes.
            const Material untextured{ .mAlphaMode = AlphaMode::Blend };
            EXPECT_EQ(untextured.getAlphaCutoff(), 0.5f);
            EXPECT_FALSE(untextured.isCutout());
        }

        /// A deformed mesh keeps its slot and its topology, and says so.
        ///
        /// The second mesh is what makes the test worth running: an update that wrote at the wrong
        /// offset would land in a neighbour, and with one mesh in the scene there is no neighbour to
        /// land in.
        TEST(RtxSceneDescTest, aDeformedMeshKeepsItsSlotAndNamesItselfOnce)
        {
            const std::array sNormals{
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            };

            SceneDesc scene;
            const Index still = scene.addMesh(sQuadPositions, sNormals, {}, sQuadIndices);
            const Index moving = scene.addMesh(sQuadPositions, sNormals, {}, sQuadIndices);

            EXPECT_TRUE(scene.getDeformed().empty()) << "nothing has deformed yet";

            // The same quad a unit further along z, facing the other way.
            std::array<osg::Vec3f, 4> posed = sQuadPositions;
            for (osg::Vec3f& vertex : posed)
                vertex.z() += 1.0f;

            const std::array sPosedNormals{
                osg::Vec3f(0.0f, 0.0f, -1.0f),
                osg::Vec3f(0.0f, 0.0f, -1.0f),
                osg::Vec3f(0.0f, 0.0f, -1.0f),
                osg::Vec3f(0.0f, 0.0f, -1.0f),
            };

            scene.updateMesh(moving, posed, sPosedNormals);
            scene.updateMesh(moving, posed, sPosedNormals);

            ASSERT_EQ(scene.getDeformed().size(), 1u) << "twice in a frame is one structure to build";
            EXPECT_EQ(scene.getDeformed()[0], moving);

            // Eight vertices still, at the same offsets: an update is not an append.
            EXPECT_EQ(scene.getPositions().size(), 8u);
            EXPECT_EQ(scene.getIndices().size(), 12u);
            EXPECT_EQ(scene.getMeshes()[moving].mVertexOffset, 4u);

            EXPECT_EQ(scene.getMeshPositions(moving)[2], osg::Vec3f(1.0f, 1.0f, 1.0f));
            EXPECT_EQ(scene.getNormals()[scene.getMeshes()[moving].mVertexOffset], osg::Vec3f(0.0f, 0.0f, -1.0f));
            EXPECT_EQ(scene.getMeshIndices(moving)[5], 3u) << "topology is what does not change";

            // And the mesh beside it is untouched, which is the offset arithmetic being right rather
            // than merely being applied.
            EXPECT_EQ(scene.getMeshPositions(still)[2], osg::Vec3f(1.0f, 1.0f, 0.0f));
            EXPECT_EQ(scene.getNormals()[scene.getMeshes()[still].mVertexOffset], osg::Vec3f(0.0f, 0.0f, 1.0f));

            // The list is a frame's worth, so it goes when the frame's placements do.
            scene.clearPlacement();
            EXPECT_TRUE(scene.getDeformed().empty());
            EXPECT_EQ(scene.getMeshes().size(), 2u) << "clearing where things are keeps what they are";
        }

        /// An emitter's sphere is derived from the sprites rather than passed in, so the rejection
        /// test a ray makes and the sprites it would then walk cannot disagree about where they are.
        ///
        /// **Off the box and not off the mean**, which the lopsided arrangement here is chosen to
        /// prove: two sprites sit at the origin and one at four along x, so the mean is at 4/3 and
        /// the box's centre at 2. From the box the reach is 2 + 1 = 3 either way; from the mean it
        /// would have to be 8/3 + 1 = 3.67 to hold the far one, a sphere 22% wider for the same
        /// three particles.
        TEST(RtxSceneDescTest, anEmitterCarriesItsSpritesAndTheSphereThatHoldsThem)
        {
            SceneDesc scene;
            const Index texture = scene.addTexture(VFS::Path::NormalizedView("textures/tx_fire_00.dds"));

            const std::array sPlume{
                Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f), .mRadius = 1.0f },
                Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f), .mRadius = 1.0f },
                Sprite{ .mPosition = osg::Vec3f(4.0f, 0.0f, 0.0f), .mRadius = 1.0f },
            };
            scene.addEmitter(sPlume, texture, true);

            ASSERT_EQ(scene.getEmitters().size(), 1u);
            const SpriteEmitter& plume = scene.getEmitters().front();
            EXPECT_EQ(plume.mCentre, osg::Vec3f(2.0f, 0.0f, 0.0f));
            EXPECT_FLOAT_EQ(plume.mReach, 3.0f);
            EXPECT_EQ(plume.mFirst, 0u);
            EXPECT_EQ(plume.mCount, 3u);
            EXPECT_EQ(plume.mTexture, texture);
            EXPECT_TRUE(plume.mAdditive);

            // An emitter with nothing alive in it is not an emitter, and the next one that has
            // something starts where the first left off rather than where a placeholder would have.
            scene.addEmitter({}, texture, false);
            EXPECT_EQ(scene.getEmitters().size(), 1u);

            const std::array sSmoke{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 10.0f), .mRadius = 2.0f } };
            scene.addEmitter(sSmoke, texture, false);

            ASSERT_EQ(scene.getEmitters().size(), 2u);
            EXPECT_EQ(scene.getEmitters()[1].mFirst, 3u);
            EXPECT_EQ(scene.getEmitters()[1].mCount, 1u);
            EXPECT_FALSE(scene.getEmitters()[1].mAdditive)
                << "the blend the file asked for is what tells the two apart";
            EXPECT_EQ(scene.getSprites().size(), 4u);
            EXPECT_EQ(scene.getSprites()[3].mPosition, osg::Vec3f(0.0f, 0.0f, 10.0f));

            // A frame's worth, so they go when the frame's placements do — and the texture they name
            // stays, because the array it indexes was uploaded when the scene was built.
            scene.clearPlacement();
            EXPECT_TRUE(scene.getEmitters().empty());
            EXPECT_TRUE(scene.getSprites().empty());
            EXPECT_EQ(scene.getTextures().size(), 1u);
        }

        TEST(RtxSceneDescTest, clearingEmptiesEveryTable)
        {
            SceneDesc scene;
            const Index mesh = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index material = scene.addMaterial(Material{});
            scene.addTexture(VFS::Path::NormalizedView("textures/tx_stone_01.dds"));
            scene.addInstance(
                MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh, .mMaterial = material });
            scene.updateMesh(mesh, sQuadPositions, {});
            scene.addEmitter(std::array{ Sprite{ .mRadius = 1.0f } }, 0, true);

            scene.clear();

            EXPECT_TRUE(scene.getMeshes().empty());
            EXPECT_TRUE(scene.getInstances().empty());
            EXPECT_TRUE(scene.getMaterials().empty());
            EXPECT_TRUE(scene.getTextures().empty());
            EXPECT_TRUE(scene.getPositions().empty());
            EXPECT_TRUE(scene.getDeformed().empty());
            EXPECT_TRUE(scene.getSprites().empty());
            EXPECT_TRUE(scene.getEmitters().empty());
            EXPECT_EQ(scene.getTriangleCount(), 0u);
        }
    }
}
