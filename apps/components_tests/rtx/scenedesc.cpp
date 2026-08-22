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

        /// A triangle, so that a mesh beside the quads has a length of its own to be packed against.
        const std::array sTrianglePositions{
            osg::Vec3f(0.0f, 0.0f, 5.0f),
            osg::Vec3f(1.0f, 0.0f, 5.0f),
            osg::Vec3f(0.0f, 1.0f, 5.0f),
        };

        constexpr std::array<std::uint32_t, 3> sTriangleIndices{ 0, 1, 2 };

        /// The same quad lifted to `z`, so a mesh can be told apart by what came back out of it.
        std::array<osg::Vec3f, 4> quadAt(float z)
        {
            std::array<osg::Vec3f, 4> lifted = sQuadPositions;
            for (osg::Vec3f& vertex : lifted)
                vertex.z() = z;

            return lifted;
        }

        /// Compaction closes the gaps in every table and says where everything went.
        ///
        /// Hand-counted throughout. Three meshes of 4, 3 and 4 vertices sit at vertex offsets 0, 4
        /// and 7 and index offsets 0, 6 and 9; dropping the middle one leaves the first where it is
        /// and brings the third to 4 and 6, for eight vertices and twelve indices in all.
        TEST(RtxSceneDescTest, retainingClosesTheGapsAndSaysWhereEverythingWent)
        {
            SceneDesc scene;
            const std::array quads{ quadAt(0.0f), quadAt(2.0f) };
            const Index first = scene.addMesh(quads[0], {}, {}, sQuadIndices);
            const Index middle = scene.addMesh(sTrianglePositions, {}, {}, sTriangleIndices);
            const Index last = scene.addMesh(quads[1], {}, {}, sQuadIndices);

            ASSERT_EQ(scene.getPositions().size(), 11u);
            ASSERT_EQ(scene.getIndices().size(), 15u);
            EXPECT_EQ(scene.getMeshes()[last].mVertexOffset, 7u);

            const std::uint64_t was = scene.getRevision();

            Remap remap;
            const std::array keep{ first, last };
            const std::array<Index, 0> noMaterials{};
            ASSERT_TRUE(scene.retain(keep, noMaterials, {}, remap));

            EXPECT_EQ(remap.mMeshes[first], 0u);
            EXPECT_EQ(remap.mMeshes[middle], sNoIndex);
            EXPECT_EQ(remap.mMeshes[last], 1u);

            ASSERT_EQ(scene.getMeshes().size(), 2u);
            EXPECT_EQ(scene.getPositions().size(), 8u);
            EXPECT_EQ(scene.getIndices().size(), 12u);
            EXPECT_EQ(scene.getMeshes()[1].mVertexOffset, 4u);
            EXPECT_EQ(scene.getMeshes()[1].mIndexOffset, 6u);

            // The contents and not only the offsets: a copy that moved the wrong range would leave
            // the arithmetic looking right and the triangle in the quad's place.
            EXPECT_EQ(scene.getMeshPositions(0)[0].z(), 0.0f);
            EXPECT_EQ(scene.getMeshPositions(1)[0].z(), 2.0f);
            EXPECT_EQ(scene.getMeshIndices(1)[5], 3u);

            EXPECT_GT(scene.getRevision(), was) << "the structures built from this describe a scene that has gone";
        }

        /// Layers and masks belong to the material that owns them and go where it goes; a texture
        /// lives while anything still names it.
        ///
        /// Hand-counted: three materials, of which the first and last are terrain with one and two
        /// layers. The layers sit at 0, 1 and 2 and their masks at 0 and 4, nine weights of the
        /// second sitting behind four of the first. Dropping the first material takes its layer and
        /// its four weights with it, so the two that survive come to layer 0 and the nine weights to
        /// mask 0 — and the texture only that layer named goes with them.
        TEST(RtxSceneDescTest, retainingCarriesALayersMasksAndDropsTheTexturesNothingNames)
        {
            SceneDesc scene;
            const Index ground = scene.addTexture(VFS::Path::NormalizedView("textures/tx_ground.dds"));
            const Index stone = scene.addTexture(VFS::Path::NormalizedView("textures/tx_stone.dds"));
            const Index sand = scene.addTexture(VFS::Path::NormalizedView("textures/tx_sand.dds"));
            const Index moss = scene.addTexture(VFS::Path::NormalizedView("textures/tx_moss.dds"));
            ASSERT_EQ(moss, 3u);

            const std::array sGroundWeights{ 0.25f, 0.25f, 0.25f, 0.25f };
            const std::array sSandWeights{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

            scene.addLayer(MaterialLayer{
                .mDiffuse = ground, .mMaskOffset = scene.addMask(sGroundWeights), .mMaskWidth = 2, .mMaskHeight = 2 });
            const Index dropped
                = scene.addMaterial(Material{ .mKind = MaterialKind::Terrain, .mLayerOffset = 0, .mLayerCount = 1 });

            const Index plain = scene.addMaterial(Material{ .mDiffuse = stone });

            scene.addLayer(MaterialLayer{
                .mDiffuse = sand, .mMaskOffset = scene.addMask(sSandWeights), .mMaskWidth = 3, .mMaskHeight = 3 });
            scene.addLayer(MaterialLayer{ .mDiffuse = moss });
            const Index kept
                = scene.addMaterial(Material{ .mKind = MaterialKind::Terrain, .mLayerOffset = 1, .mLayerCount = 2 });

            ASSERT_EQ(scene.getLayers().size(), 3u);
            ASSERT_EQ(scene.getMasks().size(), 13u);

            Remap remap;
            const std::array<Index, 0> noMeshes{};
            const std::array materials{ plain, kept };
            ASSERT_TRUE(scene.retain(noMeshes, materials, {}, remap));

            EXPECT_EQ(remap.mMaterials[dropped], sNoIndex);
            EXPECT_EQ(remap.mMaterials[plain], 0u);
            EXPECT_EQ(remap.mMaterials[kept], 1u);

            ASSERT_EQ(scene.getLayers().size(), 2u);
            EXPECT_EQ(scene.getMaterials()[1].mLayerOffset, 0u);
            EXPECT_EQ(scene.getMaterials()[1].mLayerCount, 2u);

            ASSERT_EQ(scene.getMasks().size(), 9u);
            EXPECT_EQ(scene.getLayers()[0].mMaskOffset, 0u);
            EXPECT_EQ(scene.getMasks()[0], 0.5f) << "the weights that moved are the ones that survived";
            EXPECT_EQ(scene.getLayers()[1].mMaskWidth, 0u) << "a layer with no mask keeps none";

            // `tx_ground` was named by the layer that went and by nothing else.
            ASSERT_EQ(scene.getTextures().size(), 3u);
            EXPECT_EQ(remap.mTextures[ground], sNoIndex);
            EXPECT_EQ(scene.getTextures()[0], VFS::Path::NormalizedView("textures/tx_stone.dds"));
            EXPECT_EQ(scene.getMaterials()[0].mDiffuse, 0u);
            EXPECT_EQ(scene.getLayers()[0].mDiffuse, 1u);
            EXPECT_EQ(scene.getLayers()[1].mDiffuse, 2u);

            // The lookup that dedups a path has to have moved with the table, or the next reference
            // to a texture already here appends a second copy of it.
            EXPECT_EQ(scene.addTexture(VFS::Path::NormalizedView("textures/tx_stone.dds")), 0u);
            EXPECT_EQ(scene.getTextures().size(), 3u);
        }

        /// A texture nothing else speaks for survives if the caller names it, and a scene that lost
        /// nothing is left entirely alone.
        TEST(RtxSceneDescTest, retainingKeepsANamedTextureAndDoesNothingWhenNothingWent)
        {
            SceneDesc scene;
            const Index mesh = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index material = scene.addMaterial(Material{});
            const Index sprite = scene.addTexture(VFS::Path::NormalizedView("textures/tx_fire_00.dds"));

            // Nothing went: every mesh and every material is named, so the tables are untouched and
            // the caller's remap is not even written.
            Remap remap;
            const std::array meshes{ mesh };
            const std::array materials{ material };
            const std::uint64_t was = scene.getRevision();

            EXPECT_FALSE(scene.retain(meshes, materials, {}, remap));
            EXPECT_TRUE(remap.mMeshes.empty());
            EXPECT_EQ(scene.getRevision(), was);
            EXPECT_EQ(scene.getTextures().size(), 1u) << "a sprite's texture is on no material and must not go";

            // And with the material gone the sprite's texture is still the caller's to keep — which
            // is the whole reason the keep set has a third span.
            const std::array<Index, 0> none{};
            const std::array sprites{ sprite };
            ASSERT_TRUE(scene.retain(meshes, none, sprites, remap));

            EXPECT_TRUE(scene.getMaterials().empty());
            ASSERT_EQ(scene.getTextures().size(), 1u);
            EXPECT_EQ(remap.mTextures[sprite], 0u);
            EXPECT_EQ(scene.getTextures()[0], VFS::Path::NormalizedView("textures/tx_fire_00.dds"));
        }

        /// A texture that survives without moving keeps its path.
        ///
        /// **The case the other compaction tests cannot reach.** They all drop something early, so
        /// every survivor slides down at least one slot and the copy is between two different
        /// entries; the survivors *before* the first casualty are written to the slot they are
        /// already in. A path long enough to be on the heap self-assigned that way is emptied rather
        /// than left alone, and an emptied path is a texture nothing can load — which reaches the
        /// screen as a world with no textures on it at all.
        TEST(RtxSceneDescTest, aTextureThatSurvivesWhereItStoodKeepsItsPath)
        {
            SceneDesc scene;

            // Long enough to be on the heap rather than inside the string, which is the whole
            // condition: a short name survives a self-assignment that a real path does not.
            const VFS::Path::NormalizedView first("textures/tx_ai_wickerbasket01.dds");
            const VFS::Path::NormalizedView second("textures/tx_ai_ceramicbowl01.dds");
            const VFS::Path::NormalizedView doomed("textures/tx_ai_lanternhandle.dds");
            ASSERT_GT(first.value().size(), sizeof(std::string))
                << "a path this short may live inside the string and never be moved";

            const Index kept = scene.addMaterial(Material{ .mDiffuse = scene.addTexture(first) });
            const Index also = scene.addMaterial(Material{ .mDiffuse = scene.addTexture(second) });
            scene.addMaterial(Material{ .mDiffuse = scene.addTexture(doomed) });

            Remap remap;
            const std::array<Index, 0> noMeshes{};
            const std::array materials{ kept, also };
            ASSERT_TRUE(scene.retain(noMeshes, materials, {}, remap));

            // Both survivors are where they already were, so neither index moves and both paths have
            // to be exactly what they were.
            ASSERT_EQ(scene.getTextures().size(), 2u);
            EXPECT_EQ(remap.mTextures[0], 0u);
            EXPECT_EQ(remap.mTextures[1], 1u);
            EXPECT_EQ(scene.getTextures()[0], first);
            EXPECT_EQ(scene.getTextures()[1], second);

            // And the two materials still name them, which is what an emptied path takes away: the
            // lookup would find nothing to load and the surface would come out untextured.
            EXPECT_EQ(scene.getMaterials()[0].mDiffuse, 0u);
            EXPECT_EQ(scene.getMaterials()[1].mDiffuse, 1u);
            EXPECT_EQ(scene.addTexture(first), 0u) << "the path lookup lost track of a texture still here";
            EXPECT_EQ(scene.getTextures().size(), 2u);
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
