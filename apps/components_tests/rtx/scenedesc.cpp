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
        /// A freed mesh keeps its index and its room, and the next one that fits moves in.
        ///
        /// Hand-counted throughout. Three meshes of 4, 3 and 4 vertices sit at vertex offsets 0, 4
        /// and 7 and index offsets 0, 6 and 9. Freeing the middle one moves nothing: the third is
        /// still index 2 at vertex 7, and the hole at vertex 4 is three vertices and six indices
        /// wide — which is exactly a triangle, and exactly what the next triangle takes.
        ///
        /// **Not compacting is the whole point.** Closing the gap renames every mesh above it, and a
        /// mesh index is what every bottom-level acceleration structure in the world is named by, so
        /// a cell boundary cost a full rebuild (`docs/rtx/plan.md` §10).
        TEST(RtxSceneDescTest, aFreedMeshKeepsItsSlotAndTheNextThatFitsTakesIt)
        {
            SceneDesc scene;
            const std::array quads{ quadAt(0.0f), quadAt(2.0f) };
            const Index first = scene.addMesh(quads[0], {}, {}, sQuadIndices);
            const Index middle = scene.addMesh(sTrianglePositions, {}, {}, sTriangleIndices);
            const Index last = scene.addMesh(quads[1], {}, {}, sQuadIndices);

            ASSERT_EQ(scene.getPositions().size(), 11u);
            ASSERT_EQ(scene.getIndices().size(), 15u);
            ASSERT_EQ(scene.getMeshes()[last].mVertexOffset, 7u);

            const std::uint64_t was = scene.getStructureRevision();
            const std::array keep{ first, last };
            const std::array<Index, 0> noMaterials{};
            ASSERT_TRUE(scene.release(keep, noMaterials, {}));

            // Nothing moved, nothing shrank, and every index still means what it meant.
            EXPECT_EQ(scene.getMeshes().size(), 3u);
            EXPECT_EQ(scene.getPositions().size(), 11u);
            EXPECT_EQ(scene.getIndices().size(), 15u);
            EXPECT_EQ(scene.getMeshes()[last].mVertexOffset, 7u);
            EXPECT_EQ(scene.getMeshPositions(first)[0].z(), 0.0f);
            EXPECT_EQ(scene.getMeshPositions(last)[0].z(), 2.0f);

            // The freed one describes nothing until something takes it, so a backend that walks the
            // table builds a structure over no triangles rather than over somebody else's.
            EXPECT_EQ(scene.getMeshes()[middle].mVertexCount, 0u);
            EXPECT_EQ(scene.getMeshes()[middle].mIndexCount, 0u);
            EXPECT_EQ(scene.getMeshes()[middle].mVertexCapacity, 3u) << "the room went with the contents";

            EXPECT_EQ(scene.getStructureRevision(), was)
                << "nothing arrived, so nothing built from these indices is out of date";

            // A triangle fits the hole exactly and takes it back, at the index and the offset the
            // old one had.
            const Index moved = scene.addMesh(sTrianglePositions, {}, {}, sTriangleIndices);
            EXPECT_EQ(moved, middle);
            EXPECT_EQ(scene.getMeshes()[moved].mVertexOffset, 4u);
            EXPECT_EQ(scene.getMeshes()[moved].mVertexCount, 3u);
            EXPECT_EQ(scene.getPositions().size(), 11u) << "a reused slot appended";
            EXPECT_GT(scene.getStructureRevision(), was) << "a slot taken over holds different geometry";

            // And the last mesh is still where it was, which a compaction is what would break.
            EXPECT_EQ(scene.getMeshPositions(last)[0].z(), 2.0f);
        }

        /// A mesh arriving is told from a texture arriving, and a reused slot counts as an arrival.
        ///
        /// **The guard a backend builds on, and getting it wrong crashes.** `VulkanRenderer` rebuilds
        /// its acceleration structures when a mesh arrives and not when a texture does, and it used
        /// to ask the table's *size* — which cannot see a freed slot taken over by something else.
        /// A skinned body landing in one was then refitted into a bottom-level structure that had
        /// never been made for it, which is a build into a null handle.
        TEST(RtxSceneDescTest, aMeshArrivingIsToldFromATextureArrivingAndAReusedSlotIsAnArrival)
        {
            SceneDesc scene;
            const Index slot = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);

            const std::uint64_t meshes = scene.getMeshRevision();
            const std::uint64_t structure = scene.getStructureRevision();

            // A texture is an upload, not a structure to build.
            scene.addTexture(VFS::Path::NormalizedView("textures/tx_stone.dds"));
            EXPECT_EQ(scene.getMeshRevision(), meshes) << "a texture asked for the structures to be built again";
            EXPECT_GT(scene.getStructureRevision(), structure);

            // The slot comes back and is taken over. The table is the same size it was, and what is
            // in it is not.
            ASSERT_TRUE(scene.release({}, {}, {}));
            EXPECT_EQ(scene.getMeshRevision(), meshes) << "a cell leaving asked for the structures to be built again";

            EXPECT_EQ(scene.addMesh(sTrianglePositions, {}, {}, sTriangleIndices), slot);
            EXPECT_EQ(scene.getMeshes().size(), 1u) << "the table grew, so a size test would have caught this anyway";
            EXPECT_GT(scene.getMeshRevision(), meshes) << "a slot taken over went unnoticed";
        }

        /// Best fit, and a mesh too big for every hole appends rather than being refused.
        ///
        /// **First fit would spend a cathedral's hole on a crate.** The free list is what one
        /// departing ring left, so it is short enough to search and long-lived enough that the
        /// choice matters: a quad dropped into the four-vertex hole leaves the eight-vertex one for
        /// the next thing that needs eight.
        TEST(RtxSceneDescTest, aFreedSlotGoesToTheSmallestMeshThatFitsAndTheRestAppend)
        {
            SceneDesc scene;

            // Eight vertices and twelve indices, which is two quads' worth in one mesh.
            std::vector<osg::Vec3f> big;
            std::vector<std::uint32_t> bigIndices;
            for (int copy = 0; copy < 2; ++copy)
            {
                for (const osg::Vec3f& vertex : quadAt(static_cast<float>(copy)))
                    big.push_back(vertex);

                for (const std::uint32_t index : sQuadIndices)
                    bigIndices.push_back(index + static_cast<std::uint32_t>(copy) * 4u);
            }

            const Index roomy = scene.addMesh(big, {}, {}, bigIndices);
            const Index snug = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index kept = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);

            ASSERT_EQ(scene.getMeshes()[roomy].mVertexCapacity, 8u);
            ASSERT_EQ(scene.getMeshes()[snug].mVertexCapacity, 4u);

            const std::array keep{ kept };
            ASSERT_TRUE(scene.release(keep, {}, {}));

            // A quad fits both holes and takes the tighter one.
            EXPECT_EQ(scene.addMesh(sQuadPositions, {}, {}, sQuadIndices), snug)
                << "the roomy hole was spent on something that fitted the snug one";

            // The big one is still free, and something that size takes it rather than appending.
            const std::size_t vertices = scene.getPositions().size();
            EXPECT_EQ(scene.addMesh(big, {}, {}, bigIndices), roomy);
            EXPECT_EQ(scene.getPositions().size(), vertices) << "a mesh that fitted a hole appended anyway";

            // Nothing fits now, so this one goes on the end.
            EXPECT_EQ(scene.addMesh(big, {}, {}, bigIndices), 3u);
            EXPECT_GT(scene.getPositions().size(), vertices);
        }

        /// A slot taken over holds its own attributes and none of its predecessor's.
        ///
        /// **The one way a reused slot can be quietly wrong.** A mesh that brings no normals is
        /// given zeroes on a fresh slot because the buffer was grown for it; on a reused one the
        /// room already holds whatever the last tenant put there, and a surface lit by somebody
        /// else's normals looks lit rather than looking broken.
        TEST(RtxSceneDescTest, aReusedSlotDoesNotInheritTheAttributesOfWhatStoodInIt)
        {
            SceneDesc scene;

            const std::array<osg::Vec3f, 4> normals{ osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f) };
            const std::array<osg::Vec2f, 4> uvs{ osg::Vec2f(0.5f, 0.5f), osg::Vec2f(0.5f, 0.5f), osg::Vec2f(0.5f, 0.5f),
                osg::Vec2f(0.5f, 0.5f) };

            const Index slot = scene.addMesh(sQuadPositions, normals, uvs, sQuadIndices);
            ASSERT_EQ(scene.getNormals()[scene.getMeshes()[slot].mVertexOffset], osg::Vec3f(1.0f, 0.0f, 0.0f));

            ASSERT_TRUE(scene.release({}, {}, {}));
            EXPECT_EQ(scene.addMesh(sQuadPositions, {}, {}, sQuadIndices), slot);

            EXPECT_EQ(scene.getNormals()[scene.getMeshes()[slot].mVertexOffset], osg::Vec3f())
                << "the slot kept the last tenant's normals";
            EXPECT_EQ(scene.getTexCoords()[0], osg::Vec2f());
        }

        /// A material frees its slot too, and its layers and masks stay behind.
        ///
        /// Hand-counted: three materials, of which the first and last are terrain with one and two
        /// layers. The layers sit at 0, 1 and 2 and their masks at 0 and 4, nine weights of the
        /// second sitting behind four of the first. Freeing the first material leaves all of that
        /// exactly where it is — **a known leak**, because a layer run is variable length and
        /// reclaiming one needs the suballocator the meshes have and the layers do not. Travel is
        /// what takes it back.
        TEST(RtxSceneDescTest, releasingAMaterialFreesItsSlotAndLeavesItsLayersBehind)
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

            const std::array<Index, 0> noMeshes{};
            const std::array materials{ plain, kept };
            ASSERT_TRUE(scene.release(noMeshes, materials, {}));

            // Every survivor is at the index it was given, which is what nothing moving means.
            EXPECT_EQ(scene.getMaterials().size(), 3u);
            EXPECT_EQ(scene.getMaterials()[plain].mDiffuse, stone);
            EXPECT_EQ(scene.getMaterials()[kept].mLayerOffset, 1u);
            EXPECT_EQ(scene.getMaterials()[kept].mLayerCount, 2u);

            EXPECT_EQ(scene.getLayers().size(), 3u) << "a layer run was reclaimed, which nothing can do yet";
            EXPECT_EQ(scene.getMasks().size(), 13u);
            EXPECT_EQ(scene.getLayers()[1].mDiffuse, sand);
            EXPECT_EQ(scene.getLayers()[2].mDiffuse, moss);

            // The freed slot goes to the next material asked for, whatever size it is: a material is
            // one size, so there is no fit to find.
            const Index reused = scene.addMaterial(Material{ .mDiffuse = moss });
            EXPECT_EQ(reused, dropped);
            EXPECT_EQ(scene.getMaterials().size(), 3u);

            // **`tx_ground` goes with the layer that named it, and its slot comes back.** Only the
            // dead material's run wore it, and an orphaned run is deliberately not allowed to speak
            // for a texture — or the image would leak alongside the layers.
            ASSERT_EQ(scene.getTextures().size(), 4u) << "the table shrank, so something was renumbered";
            EXPECT_TRUE(scene.getTextures()[ground].value().empty()) << "a texture nothing wears was kept";

            // The three the survivors wear are untouched, at the indices they were given.
            EXPECT_EQ(scene.getTextures()[stone], VFS::Path::NormalizedView("textures/tx_stone.dds"));
            EXPECT_EQ(scene.getTextures()[sand], VFS::Path::NormalizedView("textures/tx_sand.dds"));
            EXPECT_EQ(scene.getTextures()[moss], VFS::Path::NormalizedView("textures/tx_moss.dds"));

            // The freed slot is what the next texture takes, and the path lookup went with it: asking
            // for `tx_ground` again is a new arrival rather than a hit on a slot nothing stands in.
            EXPECT_EQ(scene.addTexture(VFS::Path::NormalizedView("textures/tx_ground.dds")), ground);
            EXPECT_EQ(scene.getTextures().size(), 4u) << "the table grew past a free slot";
            EXPECT_EQ(scene.getArrivedTextures().back(), ground) << "a slot taken over was not reported as arriving";
        }

        /// **The split that keeps an animated state set from rebuilding the world.**
        ///
        /// A material appearing, and a sweep that takes one away again, is a few kilobytes of table.
        /// A mesh or a texture *appearing* is every acceleration structure in the scene. The mirror
        /// reports them apart so a reader can answer them apart — OpenMW's water cycles thirty-two
        /// materials a second, and reading that as a world arriving cost the game every frame it
        /// had.
        TEST(RtxSceneDescTest, aMaterialChangingIsNotAStructureChanging)
        {
            SceneDesc scene;
            const Index mesh = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            scene.addMaterial(Material{});

            const std::uint64_t structure = scene.getStructureRevision();
            const std::uint64_t shading = scene.getShadingRevision();

            // A second material, which is what a state set with a new address comes to.
            Material other;
            other.mTwoSided = true;
            const Index kept = scene.addMaterial(other);

            EXPECT_EQ(scene.getStructureRevision(), structure) << "a material asked for a rebuild";
            EXPECT_GT(scene.getShadingRevision(), shading);

            // And taking one away again is the same kind of change, not a different one.
            const std::uint64_t settled = scene.getShadingRevision();
            const std::array meshes{ mesh };
            const std::array materials{ kept };

            ASSERT_TRUE(scene.release(meshes, materials, {}));
            EXPECT_EQ(scene.getStructureRevision(), structure) << "a sweep of one material asked for a rebuild";
            EXPECT_GT(scene.getShadingRevision(), settled);

            // **And a mesh going is no longer the other answer either.** It was, while a sweep
            // compacted: the table moved and everything built from it had to be built again. A slot
            // that is freed in place invalidates nothing, so the frame after a cell leaves costs the
            // top level and nothing else.
            const std::uint64_t before = scene.getStructureRevision();
            ASSERT_TRUE(scene.release({}, materials, {}));
            EXPECT_EQ(scene.getStructureRevision(), before) << "a cell leaving asked for a rebuild";
        }

        /// A scene that lost nothing is left entirely alone, and a sprite's texture is the caller's
        /// to speak for.
        TEST(RtxSceneDescTest, releasingDoesNothingWhenNothingWent)
        {
            SceneDesc scene;
            const Index mesh = scene.addMesh(sQuadPositions, {}, {}, sQuadIndices);
            const Index material = scene.addMaterial(Material{});
            scene.addTexture(VFS::Path::NormalizedView("textures/tx_fire_00.dds"));

            const std::array meshes{ mesh };
            const std::array materials{ material };
            const std::uint64_t was = scene.getStructureRevision();
            const std::uint64_t shading = scene.getShadingRevision();

            EXPECT_FALSE(scene.release(meshes, materials, {}));
            EXPECT_EQ(scene.getStructureRevision(), was);
            EXPECT_EQ(scene.getShadingRevision(), shading);

            // Asked again with everything already free, which is the frame after a cell left: the
            // live count is what the keep set is compared against, not the table's size.
            const std::array<Index, 0> none{};
            ASSERT_TRUE(scene.release(none, none, {}));
            EXPECT_FALSE(scene.release(none, none, {})) << "a table with nothing left in it went again";

            EXPECT_EQ(scene.getTextures().size(), 1u) << "a sprite's texture is on no material and must not go";
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
