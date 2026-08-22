#include <initializer_list>

#include <gtest/gtest.h>

#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/Texture2D>
#include <osgParticle/Particle>
#include <osgParticle/ParticleSystem>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>

namespace RtxBridge
{
    namespace
    {
        osg::ref_ptr<osg::Vec3Array> makePositions(std::initializer_list<osg::Vec3f> values)
        {
            osg::ref_ptr<osg::Vec3Array> positions = new osg::Vec3Array;
            for (const osg::Vec3f& value : values)
                positions->push_back(value);
            return positions;
        }

        osg::ref_ptr<osg::DrawElementsUInt> makeTriangles(std::initializer_list<unsigned int> indices)
        {
            osg::ref_ptr<osg::DrawElementsUInt> triangles = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
            for (const unsigned int index : indices)
                triangles->push_back(index);
            return triangles;
        }

        /// A unit quad in the xy plane: four vertices, two triangles.
        osg::ref_ptr<osg::Geometry> makeQuad()
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
            }));
            geometry->addPrimitiveSet(makeTriangles({ 0, 1, 2, 0, 2, 3 }));
            return geometry;
        }

        TEST(RtxSceneExtractorTest, twoDrawablesBecomeTwoMeshesAndTwoInstances)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(makeQuad());
            root->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*root, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mMeshesAdded, 2u);
            EXPECT_EQ(stats.mMeshesReused, 0u);
            EXPECT_EQ(stats.mInstances, 2u);
            EXPECT_EQ(scene.getTriangleCount(), 4u);
        }

        /// The same geometry under two parents is one mesh and two placements. Getting this wrong is
        /// not a cosmetic waste: OpenMW's resource cache hands out the same object for every
        /// reference to a model, so a cell of a hundred identical crates would build a hundred
        /// acceleration structures.
        TEST(RtxSceneExtractorTest, sharedGeometryIsOneMeshPlacedTwice)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();

            osg::ref_ptr<osg::MatrixTransform> left = new osg::MatrixTransform(osg::Matrix::translate(10.0, 0.0, 0.0));
            left->addChild(quad);
            osg::ref_ptr<osg::MatrixTransform> right = new osg::MatrixTransform(osg::Matrix::translate(0.0, 20.0, 0.0));
            right->addChild(quad);

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(left);
            root->addChild(right);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*root, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mMeshesAdded, 1u);
            EXPECT_EQ(stats.mMeshesReused, 1u);
            ASSERT_EQ(scene.getInstances().size(), 2u);
            EXPECT_EQ(scene.getInstances()[0].mMesh, scene.getInstances()[1].mMesh);

            const osg::Vec3f origin(0.0f, 0.0f, 0.0f);
            EXPECT_EQ(origin * scene.getInstances()[0].mTransform, osg::Vec3f(10.0f, 0.0f, 0.0f));
            EXPECT_EQ(origin * scene.getInstances()[1].mTransform, osg::Vec3f(0.0f, 20.0f, 0.0f));
        }

        TEST(RtxSceneExtractorTest, theRootTransformIsAppliedAfterTheGraphsOwn)
        {
            osg::ref_ptr<osg::MatrixTransform> inner = new osg::MatrixTransform(osg::Matrix::scale(2.0, 2.0, 2.0));
            inner->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*inner, osg::Matrixf::translate(0.0f, 0.0f, 5.0f), 0);

            // The quad's (1,1,0) corner doubles to (2,2,0), then rises by five.
            ASSERT_EQ(scene.getInstances().size(), 1u);
            EXPECT_EQ(osg::Vec3f(1.0f, 1.0f, 0.0f) * scene.getInstances()[0].mTransform, osg::Vec3f(2.0f, 2.0f, 5.0f));
        }

        /// The visitor accumulates the local-to-world on its way down instead of rebuilding each
        /// drawable's chain from the root, so what a chain composes to is its own property to hold.
        TEST(RtxSceneExtractorTest, nestedTransformsComposeFromTheRootDownwards)
        {
            // Outermost first: scale by two, then rotate a quarter turn about z, then move along x.
            osg::ref_ptr<osg::MatrixTransform> scale = new osg::MatrixTransform(osg::Matrix::scale(2.0, 2.0, 2.0));
            osg::ref_ptr<osg::MatrixTransform> turn
                = new osg::MatrixTransform(osg::Matrix::rotate(osg::PI_2, osg::Vec3d(0.0, 0.0, 1.0)));
            osg::ref_ptr<osg::MatrixTransform> shift = new osg::MatrixTransform(osg::Matrix::translate(3.0, 0.0, 0.0));

            scale->addChild(turn);
            turn->addChild(shift);
            shift->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*scale, osg::Matrixf::identity(), 0);

            ASSERT_EQ(scene.getInstances().size(), 1u);
            const osg::Matrixf& place = scene.getInstances()[0].mTransform;

            // (1,0,0) shifts to (4,0,0), turns to (0,4,0), and scales to (0,8,0). Order is the whole
            // of what this asserts: composed the other way round it would be (0,2,0) moved to
            // (3,2,0), which is a different point and a plausible-looking one.
            const osg::Vec3f corner = osg::Vec3f(1.0f, 0.0f, 0.0f) * place;
            EXPECT_NEAR(corner.x(), 0.0f, 1e-4f);
            EXPECT_NEAR(corner.y(), 8.0f, 1e-4f);
            EXPECT_NEAR(corner.z(), 0.0f, 1e-4f);

            // And the origin lands where only the outer two act on the shift: (3,0,0) turned is
            // (0,3,0), scaled is (0,6,0).
            const osg::Vec3f origin = osg::Vec3f(0.0f, 0.0f, 0.0f) * place;
            EXPECT_NEAR(origin.x(), 0.0f, 1e-4f);
            EXPECT_NEAR(origin.y(), 6.0f, 1e-4f);
            EXPECT_NEAR(origin.z(), 0.0f, 1e-4f);
        }

        /// An absolute reference frame replaces what is above it rather than adding to it, which is
        /// a branch inside `computeLocalToWorldMatrix` and the one thing an accumulating visitor
        /// could quietly get wrong by adding where it should overwrite.
        TEST(RtxSceneExtractorTest, anAbsoluteFrameDiscardsTheTransformsAboveIt)
        {
            osg::ref_ptr<osg::MatrixTransform> above
                = new osg::MatrixTransform(osg::Matrix::translate(100.0, 100.0, 100.0));
            osg::ref_ptr<osg::MatrixTransform> absolute
                = new osg::MatrixTransform(osg::Matrix::translate(0.0, 0.0, 7.0));
            absolute->setReferenceFrame(osg::Transform::ABSOLUTE_RF);

            above->addChild(absolute);
            absolute->addChild(makeQuad());

            // A relative sibling under the same parent, so the test also shows the frame is not
            // simply being ignored for everything.
            osg::ref_ptr<osg::MatrixTransform> relative
                = new osg::MatrixTransform(osg::Matrix::translate(0.0, 0.0, 7.0));
            relative->addChild(makeQuad());
            above->addChild(relative);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*above, osg::Matrixf::identity(), 0);

            ASSERT_EQ(scene.getInstances().size(), 2u);
            const osg::Vec3f origin(0.0f, 0.0f, 0.0f);

            // The absolute one stands at its own translation and nowhere near the hundred above it.
            EXPECT_EQ(origin * scene.getInstances()[0].mTransform, osg::Vec3f(0.0f, 0.0f, 7.0f));

            // The relative one carries it.
            EXPECT_EQ(origin * scene.getInstances()[1].mTransform, osg::Vec3f(100.0f, 100.0f, 107.0f));
        }

        /// The property the incremental mirror rests on: nothing changed, so nothing is added.
        TEST(RtxSceneExtractorTest, aSecondPassOverAnUnchangedGraphAddsNothing)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(makeQuad());
            root->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*root, osg::Matrixf::identity(), 0);

            const ExtractionStats second = extractor.extract(*root, osg::Matrixf::identity(), 0);

            EXPECT_EQ(second.mMeshesAdded, 0u);
            EXPECT_EQ(second.mMeshesReused, 2u);
            EXPECT_EQ(scene.getMeshes().size(), 2u);

            // **The property the incremental mirror rests on, in its strongest form.** The same mesh
            // at two places is still two rows of the acceleration structure — placements are not
            // deduplicated — but a second pass over an unchanged graph finds the slots those two
            // already hold rather than making two more. Nothing was added, and nothing moved.
            EXPECT_EQ(scene.getPlacedCount(), 2u);
            EXPECT_EQ(scene.getInstances().size(), 2u);

            scene.advancePlacement();
            EXPECT_EQ(extractor.extract(*root, osg::Matrixf::identity(), 0).mInstances, 2u);
            EXPECT_TRUE(scene.getMoved().empty()) << "an unchanged graph reported a placement moving";
        }

        /// **A node path does not identify a placement, and this is the case that proves it.**
        /// `SceneManager::getTemplate` hands out one node per model, so every reference to that model
        /// is walked from the same node down the same path. Without the anchor they share a slot,
        /// and a hundred crates collapse into one.
        TEST(RtxSceneExtractorTest, oneTemplateWalkedUnderTwoAnchorsIsTwoPlacements)
        {
            osg::ref_ptr<osg::Group> shared = new osg::Group;
            shared->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);

            extractor.extract(*shared, osg::Matrixf::translate(10.0f, 0.0f, 0.0f), 1);
            extractor.extract(*shared, osg::Matrixf::translate(0.0f, 20.0f, 0.0f), 2);

            ASSERT_EQ(scene.getPlacedCount(), 2u);
            EXPECT_EQ(scene.getMeshes().size(), 1u) << "one model is still one mesh";

            const osg::Vec3f origin(0.0f, 0.0f, 0.0f);
            EXPECT_EQ(origin * scene.getInstances()[0].mTransform, osg::Vec3f(10.0f, 0.0f, 0.0f));
            EXPECT_EQ(origin * scene.getInstances()[1].mTransform, osg::Vec3f(0.0f, 20.0f, 0.0f));

            // And they keep their own histories. Moving one must leave the other reporting nothing —
            // sharing a slot would have the still one inherit the mover's previous transform and
            // smear across the frame.
            scene.advancePlacement();
            extractor.extract(*shared, osg::Matrixf::translate(11.0f, 0.0f, 0.0f), 1);
            extractor.extract(*shared, osg::Matrixf::translate(0.0f, 20.0f, 0.0f), 2);

            ASSERT_EQ(scene.getMoved().size(), 1u);
            EXPECT_EQ(scene.getMoved()[0], 0u);
        }

        /// Runs a deforming drawable's own cull path, which is where its vertices are computed.
        ///
        /// An `osgUtil::CullVisitor` proper would need a render stage and a state graph behind it.
        /// What `MorphGeometry` actually reads of the visitor is its traversal number and the node
        /// path, so one that merely says it is a cull drives the real deformation — which is the
        /// point: this test asserts against vertices the production code computed, not against a
        /// stand-in for them.
        struct DeformingCull : osg::NodeVisitor
        {
            DeformingCull()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
                setVisitorType(CULL_VISITOR);
            }
        };

        /// A skinned body is mirrored, and from the pose rather than from the bind geometry.
        ///
        /// The discriminator is replacing the source's vertex array after `setSourceGeometry` has
        /// taken its deep copy: a mirror reading `getSourceGeometry` shows the replacement, and one
        /// reading the pose shows what was copied. Without that the two are the same numbers and the
        /// test could not fail.
        TEST(RtxSceneExtractorTest, aSkinnedBodyIsMirroredFromItsPoseAndNotItsBindGeometry)
        {
            osg::ref_ptr<osg::Geometry> source = makeQuad();
            osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
            rig->setSourceGeometry(source);

            source->setVertexArray(makePositions({
                osg::Vec3f(100.0f, 100.0f, 100.0f),
                osg::Vec3f(101.0f, 100.0f, 100.0f),
                osg::Vec3f(101.0f, 101.0f, 100.0f),
                osg::Vec3f(100.0f, 101.0f, 100.0f),
            }));

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(rig);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*root, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mSkippedUnknown, 0u) << "a drawable that is not an osg::Geometry is still geometry";
            EXPECT_EQ(stats.mMeshesAdded, 1u);
            EXPECT_EQ(stats.mDeformed, 1u);
            EXPECT_EQ(stats.mInstances, 1u);
            EXPECT_EQ(scene.getTriangleCount(), 2u);

            EXPECT_EQ(scene.getMeshPositions(0)[2], osg::Vec3f(1.0f, 1.0f, 0.0f)) << "the bind pose, not the source";
        }

        /// A pose is the one thing the mesh cache does not answer: met again, it is read again — and
        /// a static drawable met again is not.
        ///
        /// The two kinds stand side by side in one graph and one pass, so the same run produces both
        /// answers and the difference cannot be a difference in how they were set up.
        ///
        /// The two culls also land in different halves of the double buffer, which is the case that
        /// keying the mesh cache on the geometry pointer would break: it would put two frozen poses
        /// of the same face in the scene and alternate between them.
        TEST(RtxSceneExtractorTest, aMorphedFaceIsReadAgainEachPassAndAStaticDrawableIsNot)
        {
            osg::ref_ptr<SceneUtil::MorphGeometry> morph = new SceneUtil::MorphGeometry;
            morph->setSourceGeometry(makeQuad());

            // The base target is what the pose starts from; the second is added at its weight. One
            // unit of z per unit of weight, so the expected value is arithmetic rather than a fit.
            morph->addMorphTarget(makePositions({
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
            }));
            morph->addMorphTarget(makePositions({
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            }));

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(makeQuad());
            root->addChild(morph);

            constexpr Rtx::Index sStill = 0;
            constexpr Rtx::Index sFace = 1;

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);

            DeformingCull cull;
            cull.setTraversalNumber(1);
            root->accept(cull);

            const ExtractionStats first = extractor.extract(*root, osg::Matrixf::identity(), 0);
            EXPECT_EQ(first.mMeshesAdded, 2u);
            EXPECT_EQ(first.mDeformed, 1u);
            EXPECT_EQ(scene.getMeshPositions(sFace)[2], osg::Vec3f(1.0f, 1.0f, 1.0f)) << "base plus one of the target";

            scene.clearPlacement();
            morph->getMorphTarget(1).setWeight(3.0f);
            morph->dirty();
            cull.setTraversalNumber(2);
            root->accept(cull);

            const ExtractionStats second = extractor.extract(*root, osg::Matrixf::identity(), 0);

            EXPECT_EQ(second.mMeshesAdded, 0u);
            EXPECT_EQ(second.mMeshesReused, 2u);
            EXPECT_EQ(second.mDeformed, 1u) << "the face, and only the face";
            EXPECT_EQ(scene.getMeshes().size(), 2u) << "the other half of the double buffer is the same mesh";

            ASSERT_EQ(scene.getDeformed().size(), 1u);
            EXPECT_EQ(scene.getDeformed()[0], sFace) << "the still quad's structure is not built again";

            EXPECT_EQ(scene.getMeshPositions(sFace)[2], osg::Vec3f(1.0f, 1.0f, 3.0f)) << "base plus three";
            EXPECT_EQ(scene.getMeshPositions(sStill)[2], osg::Vec3f(1.0f, 1.0f, 0.0f)) << "and the neighbour is intact";
        }

        TEST(RtxSceneExtractorTest, degenerateTrianglesAreDropped)
        {
            // One real triangle and two zero-area ones, which is how a triangle strip restarts.
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
            }));
            geometry->addPrimitiveSet(makeTriangles({ 0, 1, 2, 0, 0, 1, 2, 2, 2 }));

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*geometry, osg::Matrixf::identity(), 0);

            EXPECT_EQ(scene.getTriangleCount(), 1u);
        }

        TEST(RtxSceneExtractorTest, geometryWithNoTrianglesIsSkippedRatherThanAdded)
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({ osg::Vec3f(0.0f, 0.0f, 0.0f) }));

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*geometry, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mSkippedEmpty, 1u);
            EXPECT_EQ(stats.mInstances, 0u);
            EXPECT_TRUE(scene.getMeshes().empty());
        }

        /// What the walk stopped finding leaves the scene, and what stayed keeps working.
        ///
        /// **The whole reason this exists is not memory but identity.** The mesh cache is keyed on
        /// the `osg::Drawable*`, which is what makes a crate met in a second cell resolve to the
        /// crate already uploaded — and an address the engine freed when a cell unloaded can be
        /// handed straight back for something else. Sweeping is what stops the next thing allocated
        /// there inheriting a mesh it has nothing to do with.
        TEST(RtxSceneExtractorTest, aSweepDropsWhatTheWalkNoLongerFindsAndCarriesTheRest)
        {
            osg::ref_ptr<osg::Geometry> stays = makeQuad();
            osg::ref_ptr<osg::Geometry> goes = makeQuad();
            osg::ref_ptr<osg::Geometry> alsoStays = makeQuad();

            // Told apart by their vertices, so the survivors can be checked by what came out of them
            // rather than only by how many there are.
            static_cast<osg::Vec3Array*>(alsoStays->getVertexArray())->at(0).z() = 7.0f;

            osg::ref_ptr<osg::Group> whole = new osg::Group;
            whole->addChild(stays);
            whole->addChild(goes);
            whole->addChild(alsoStays);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*whole, osg::Matrixf::identity(), 0);
            ASSERT_EQ(scene.getMeshes().size(), 3u);

            // Nothing has gone yet, so the sweep is a no-op — and the epoch it opens is what the
            // next walk is measured against.
            EXPECT_TRUE(extractor.retire().empty());
            EXPECT_EQ(scene.getMeshes().size(), 3u);

            osg::ref_ptr<osg::Group> less = new osg::Group;
            less->addChild(stays);
            less->addChild(alsoStays);

            scene.clearPlacement();
            extractor.extract(*less, osg::Matrixf::identity(), 0);

            const Retirement went = extractor.retire();
            EXPECT_EQ(went.mMeshes, 1u);
            EXPECT_EQ(went.mMaterials, 0u) << "an untextured quad has no state set and so no material";
            ASSERT_EQ(scene.getMeshes().size(), 2u);

            // The middle one went, so the third has moved down into its slot — and the identity map
            // has to have moved with it, or the next walk resolves the survivor to a mesh that is
            // now somebody else's.
            scene.clearPlacement();
            const ExtractionStats after = extractor.extract(*less, osg::Matrixf::identity(), 0);

            EXPECT_EQ(after.mMeshesAdded, 0u) << "a survivor was re-added rather than recognised";
            EXPECT_EQ(after.mMeshesReused, 2u);

            // **Five slots and two standing in them.** The three walked under `whole` are gone —
            // that graph is not walked any more, so the sweep took their placements — and the two
            // walked under `less` are different placements of the same geometry, so they took slots
            // of their own. A dropped placement leaves its slot behind rather than closing the gap.
            ASSERT_EQ(scene.getPlacedCount(), 2u);
            ASSERT_EQ(scene.getInstances().size(), 5u);
            for (std::size_t gap = 0; gap < 3; ++gap)
                EXPECT_FALSE(scene.getInstances()[gap].isPlaced()) << "slot " << gap << " should be a gap";

            // And `carryPlacement` renumbered what the survivors name: the third quad's mesh moved
            // down into index one when the middle one was compacted out, and the placement standing
            // on it has to have followed.
            ASSERT_TRUE(scene.getInstances()[3].isPlaced());
            ASSERT_TRUE(scene.getInstances()[4].isPlaced());
            EXPECT_EQ(scene.getInstances()[3].mMesh, 0u);
            EXPECT_EQ(scene.getInstances()[4].mMesh, 1u);
            EXPECT_EQ(scene.getMeshPositions(1)[0].z(), 7.0f) << "the survivor kept somebody else's vertices";
        }

        /// A material and the texture behind it go when the last thing wearing them does.
        TEST(RtxSceneExtractorTest, aSweepTakesTheMaterialsAndTexturesNothingWearsAnyMore)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_stone_01.dds");

            osg::ref_ptr<osg::Geometry> stone = makeQuad();
            stone->getOrCreateStateSet()->setTextureAttributeAndModes(
                0, new osg::Texture2D(image), osg::StateAttribute::ON);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*stone, osg::Matrixf::identity(), 0);

            ASSERT_EQ(scene.getMaterials().size(), 1u);
            ASSERT_EQ(scene.getTextures().size(), 1u);
            ASSERT_TRUE(extractor.retire().empty()) << "the walk that found it is the epoch it survives";

            // A walk that finds nothing at all is still a walk, and it is what an emptied cell is.
            osg::ref_ptr<osg::Group> nothing = new osg::Group;
            scene.clearPlacement();
            extractor.extract(*nothing, osg::Matrixf::identity(), 0);

            const Retirement went = extractor.retire();
            EXPECT_EQ(went.mMeshes, 1u);
            EXPECT_EQ(went.mMaterials, 1u);
            EXPECT_EQ(went.mTextures, 1u);
            EXPECT_TRUE(scene.getMeshes().empty());
            EXPECT_TRUE(scene.getMaterials().empty());
            EXPECT_TRUE(scene.getTextures().empty());
            EXPECT_TRUE(scene.getPositions().empty());
        }

        /// A particle system under a transform that carries its texture and its blend, the way
        /// `NifOsg` builds one.
        ///
        /// The emitter's own state set sets neither, which is what makes this a test of the walk up
        /// the path rather than of the drawable: a `ParticleSystem` really does carry an empty state
        /// set of its own in the shipped content, and asking it for the blend answers "covers" for
        /// every flame in the game.
        struct Plume
        {
            osg::ref_ptr<osg::MatrixTransform> mRoot;
            osg::ref_ptr<osgParticle::ParticleSystem> mParticles;
        };

        Plume makePlume(const osg::Matrix& place, bool additive)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_fire_00.dds");

            Plume plume;
            plume.mRoot = new osg::MatrixTransform(place);
            plume.mRoot->getOrCreateStateSet()->setTextureAttributeAndModes(
                0, new osg::Texture2D(image), osg::StateAttribute::ON);
            plume.mRoot->getOrCreateStateSet()->setAttributeAndModes(
                new osg::BlendFunc(
                    osg::BlendFunc::SRC_ALPHA, additive ? osg::BlendFunc::ONE : osg::BlendFunc::ONE_MINUS_SRC_ALPHA),
                osg::StateAttribute::ON);

            plume.mParticles = new osgParticle::ParticleSystem;
            plume.mParticles->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            plume.mRoot->addChild(plume.mParticles);

            return plume;
        }

        /// Adds one particle and brings its interpolated size, colour and alpha up to date.
        ///
        /// `getCurrentSize` and the two beside it are only meaningful after `Particle::update`, so
        /// the zero-length step is not a formality: without it every sprite this test reads back
        /// carries whatever the default template was constructed with.
        osgParticle::Particle* emit(
            osgParticle::ParticleSystem& particles, const osg::Vec3f& at, float size, const osg::Vec4f& colour)
        {
            osgParticle::Particle seed;
            osgParticle::Particle* particle = particles.createParticle(&seed);
            particle->setLifeTime(10.0f);
            particle->setPosition(at);
            particle->setVelocity(osg::Vec3f());
            particle->setSizeRange(osgParticle::rangef(size, size));
            particle->setColorRange(osgParticle::rangev4(colour, colour));
            particle->setAlphaRange(osgParticle::rangef(colour.a(), colour.a()));
            particle->update(0.0, false);
            return particle;
        }

        /// A particle system is not geometry, and what comes out of it is a run of discs.
        ///
        /// Every number here is the file's own carried through one transform: the placement moves
        /// each sprite and its uniform scale widens it, because `NifOsg` asks for particle sizes in
        /// the emitter's own coordinates and the modelview is what the rasterizer would have scaled
        /// them by.
        TEST(RtxSceneExtractorTest, aParticleSystemPlacesSpritesAndNoMesh)
        {
            // Scaled by two and moved a hundred along x, so the radius and the position each prove a
            // different half of the transform.
            const Plume plume = makePlume(osg::Matrix::scale(2.0, 2.0, 2.0) * osg::Matrix::translate(100.0, 0.0, 0.0),
                /*additive=*/true);

            emit(*plume.mParticles, osg::Vec3f(0.0f, 0.0f, 5.0f), 3.0f, osg::Vec4f(1.0f, 0.5f, 0.25f, 0.5f));
            emit(*plume.mParticles, osg::Vec3f(0.0f, 0.0f, 9.0f), 1.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f));

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*plume.mRoot, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mEmitters, 1u);
            EXPECT_EQ(stats.mSprites, 2u);
            EXPECT_EQ(stats.mSkippedUnknown, 0u) << "a particle system is read, not passed over";
            EXPECT_EQ(stats.mInstances, 0u) << "sprites are the drawing, so there is nothing to build over";
            EXPECT_EQ(stats.mMeshesAdded, 0u);

            ASSERT_EQ(scene.getSprites().size(), 2u);

            // (0, 0, 5) scaled by two is (0, 0, 10), then moved to x = 100. The radius is the file's
            // three by the same two.
            const Rtx::Sprite& low = scene.getSprites()[0];
            EXPECT_EQ(low.mPosition, osg::Vec3f(100.0f, 0.0f, 10.0f));
            EXPECT_FLOAT_EQ(low.mRadius, 6.0f);
            EXPECT_EQ(low.mColour, osg::Vec3f(1.0f, 0.5f, 0.25f));

            // The colour ramp's alpha and the alpha ramp are separate and the rasterizer multiplies
            // them; here both are a half, so a quarter is what proves the product rather than one of
            // the two being read and the other dropped.
            EXPECT_FLOAT_EQ(low.mAlpha, 0.25f);

            EXPECT_EQ(scene.getSprites()[1].mPosition, osg::Vec3f(100.0f, 0.0f, 18.0f));
            EXPECT_FLOAT_EQ(scene.getSprites()[1].mRadius, 2.0f);

            // Two sprites four apart before the scale and eight after, each one wider than the
            // other: the box runs z = 4 to 20, so the centre is 12 and the reach 8.
            ASSERT_EQ(scene.getEmitters().size(), 1u);
            EXPECT_EQ(scene.getEmitters().front().mCentre, osg::Vec3f(100.0f, 0.0f, 12.0f));
            EXPECT_FLOAT_EQ(scene.getEmitters().front().mReach, 8.0f);
            EXPECT_EQ(scene.getTextures().size(), 1u);
            EXPECT_EQ(scene.getTextures()[0], VFS::Path::NormalizedView("textures/tx_fire_00.dds"));
        }

        /// `SRC_ALPHA, ONE` is a flame and anything else covers, and the difference is what decides
        /// whether the sprite is light or an albedo to be lit.
        ///
        /// The blend sits on the transform above the emitter, where `NifOsg` puts it, and the
        /// emitter carries a state set of its own that says nothing about blending — so an answer
        /// read off the drawable is "covers" both times.
        TEST(RtxSceneExtractorTest, theBlendTellsAFlameFromSmoke)
        {
            const auto extractOne = [](bool additive) {
                const Plume plume = makePlume(osg::Matrix::identity(), additive);
                emit(*plume.mParticles, osg::Vec3f(), 1.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f));

                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene);
                extractor.extract(*plume.mRoot, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.getEmitters().size(), 1u);
                return scene.getEmitters().front().mAdditive;
            };

            EXPECT_TRUE(extractOne(true));
            EXPECT_FALSE(extractOne(false));
        }

        /// A dead slot keeps the position its last particle expired at, and an emitter with nothing
        /// alive places nothing at all — not a sphere with an empty run behind it, which every ray
        /// crossing that part of the cell would then be rejected by one test later than it needs.
        TEST(RtxSceneExtractorTest, deadParticlesAndUntexturedEmittersPlaceNothing)
        {
            const Plume spent = makePlume(osg::Matrix::identity(), true);
            osgParticle::Particle* particle
                = emit(*spent.mParticles, osg::Vec3f(0.0f, 0.0f, 5.0f), 3.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f));
            particle->kill();
            particle->update(0.0, false);
            ASSERT_FALSE(particle->isAlive());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*spent.mRoot, osg::Matrixf::identity(), 0);

            EXPECT_EQ(stats.mEmitters, 0u);
            EXPECT_EQ(stats.mSprites, 0u);
            EXPECT_TRUE(scene.getEmitters().empty());

            // The texture is registered the moment the emitter is met, alive or not: it is what the
            // array is built from, and one that turns up two hundred frames later has nowhere to go.
            EXPECT_EQ(scene.getTextures().size(), 1u);

            // A particle's whole silhouette is that texture's alpha, so an emitter with none draws
            // nothing rather than a white disc.
            osg::ref_ptr<osg::Group> bare = new osg::Group;
            osg::ref_ptr<osgParticle::ParticleSystem> particles = new osgParticle::ParticleSystem;
            bare->addChild(particles);
            emit(*particles, osg::Vec3f(), 1.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f));

            Rtx::SceneDesc bareScene;
            SceneExtractor bareExtractor(bareScene);
            EXPECT_EQ(bareExtractor.extract(*bare, osg::Matrixf::identity(), 0).mEmitters, 0u);
            EXPECT_TRUE(bareScene.getTextures().empty());
        }

        /// An emitter's sprite is on no material, so the sweep has to speak for it itself.
        ///
        /// The emitter outlives a textured quad here, and the texture the quad wore is what proves
        /// the sweep is doing anything at all: a pass that kept every texture would keep both.
        TEST(RtxSceneExtractorTest, aSweepKeepsTheTextureAnEmitterIsStillDrawingWith)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_stone_01.dds");

            osg::ref_ptr<osg::Geometry> stone = makeQuad();
            stone->getOrCreateStateSet()->setTextureAttributeAndModes(
                0, new osg::Texture2D(image), osg::StateAttribute::ON);

            const Plume plume = makePlume(osg::Matrix::identity(), /*additive=*/true);
            emit(*plume.mParticles, osg::Vec3f(), 1.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f));

            osg::ref_ptr<osg::Group> both = new osg::Group;
            both->addChild(stone);
            both->addChild(plume.mRoot);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*both, osg::Matrixf::identity(), 0);
            ASSERT_EQ(scene.getTextures().size(), 2u);
            ASSERT_TRUE(extractor.retire().empty());

            scene.clearPlacement();
            extractor.extract(*plume.mRoot, osg::Matrixf::identity(), 0);

            const Retirement went = extractor.retire();
            EXPECT_EQ(went.mTextures, 1u);
            ASSERT_EQ(scene.getTextures().size(), 1u);
            EXPECT_EQ(scene.getTextures()[0], VFS::Path::NormalizedView("textures/tx_fire_00.dds"));

            // And the emitter still draws with it after the compaction moved it down a slot.
            scene.clearPlacement();
            extractor.extract(*plume.mRoot, osg::Matrixf::identity(), 0);

            ASSERT_EQ(scene.getEmitters().size(), 1u);
            EXPECT_EQ(scene.getEmitters().front().mTexture, 0u);
            EXPECT_EQ(scene.getTextures().size(), 1u) << "the sprite's path was added a second time";
        }

        /// Textures come from wherever they are bound; everything else comes from the drawable.
        ///
        /// `NifOsg` puts a model's textures on its geometry, but a drawable can carry a state set of
        /// its own that sets only culling. Taking the whole material from whichever state set held
        /// the textures hands that drawable a parent's two-sidedness.
        TEST(RtxSceneExtractorTest, aDrawableKeepsItsOwnStateWhileInheritingATexture)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_stone_01.dds");

            osg::ref_ptr<osg::Group> parent = new osg::Group;
            parent->getOrCreateStateSet()->setTextureAttributeAndModes(
                0, new osg::Texture2D(image), osg::StateAttribute::ON);

            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            quad->getOrCreateStateSet()->setAttributeAndModes(
                new osg::CullFace(osg::CullFace::BACK), osg::StateAttribute::ON);
            parent->addChild(quad);

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*parent, osg::Matrixf::identity(), 0);

            ASSERT_EQ(scene.getMaterials().size(), 1u);
            ASSERT_EQ(scene.getTextures().size(), 1u);
            EXPECT_EQ(scene.getTextures()[0], VFS::Path::NormalizedView("textures/tx_stone_01.dds"));
            EXPECT_EQ(scene.getMaterials()[0].mDiffuse, 0u);
            EXPECT_FALSE(scene.getMaterials()[0].mTwoSided);
        }

        /// A blend is what marks a cutout in this data, and it has to survive into the material.
        ///
        /// Morrowind's foliage, grates and banners are drawn with `NiAlphaProperty` over a texture
        /// whose alpha is all but binary; hardly anything in the game sets an alpha test. Losing
        /// the blend here loses every mask with it.
        TEST(RtxSceneExtractorTest, aBlendedSurfaceIsTracedAsACutoutAndAPlainOneIsNot)
        {
            const auto extractOne = [](bool blend) {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->setFileName("textures/tx_leaves.dds");

                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                state.setTextureAttributeAndModes(0, new osg::Texture2D(image), osg::StateAttribute::ON);
                if (blend)
                    state.setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);

                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.getMaterials().size(), 1u);
                return scene.getMaterials().front();
            };

            const Rtx::Material blended = extractOne(true);
            EXPECT_EQ(blended.mAlphaMode, Rtx::AlphaMode::Blend);
            EXPECT_TRUE(blended.isCutout());

            const Rtx::Material plain = extractOne(false);
            EXPECT_EQ(plain.mAlphaMode, Rtx::AlphaMode::Opaque);
            EXPECT_FALSE(plain.isCutout());
        }

        /// The emissive multiplier is folded into the colour, because their product is all the
        /// game's own shader ever uses.
        TEST(RtxSceneExtractorTest, anEmissiveMultiplierIsFoldedIntoTheColourItScales)
        {
            const auto extractOne = [](float multiplier, bool attach) {
                osg::ref_ptr<osg::Material> colours = new osg::Material;
                colours->setEmission(osg::Material::FRONT, osg::Vec4f(0.5f, 0.25f, 0.0f, 1.0f));

                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                state.setAttributeAndModes(colours, osg::StateAttribute::ON);
                if (attach)
                    state.addUniform(new osg::Uniform("emissiveMult", multiplier));

                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.getMaterials().size(), 1u);
                return scene.getMaterials().front().mEmissiveColour;
            };

            EXPECT_EQ(extractOne(2.0f, true), osg::Vec3f(1.0f, 0.5f, 0.0f));
            EXPECT_EQ(extractOne(0.5f, true), osg::Vec3f(0.25f, 0.125f, 0.0f));

            // `NifOsg` only attaches the uniform where a model asked for something other than one,
            // so its absence is the default rather than a value nobody wrote.
            EXPECT_EQ(extractOne(0.0f, false), osg::Vec3f(0.5f, 0.25f, 0.0f));
        }

        /// OpenGL culls nothing unless told to, and `NifOsg` only adds a `CullFace` where the model
        /// asked for one — so an absent attribute means two-sided. Reading it the other way lights
        /// every sheet of vanilla geometry from one face only.
        TEST(RtxSceneExtractorTest, cullFaceDecidesWhetherASurfaceIsTwoSided)
        {
            const auto extractOne = [](bool cull) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet* stateSet = quad->getOrCreateStateSet();
                if (cull)
                {
                    stateSet->setAttributeAndModes(new osg::CullFace(osg::CullFace::BACK), osg::StateAttribute::ON);
                }
                else
                {
                    // Something in the state set, so it is found at all, but no CullFace.
                    stateSet->setMode(GL_BLEND, osg::StateAttribute::OFF);
                }

                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);
                return scene.getMaterials().empty() ? true : scene.getMaterials()[0].mTwoSided;
            };

            EXPECT_FALSE(extractOne(true));
            EXPECT_TRUE(extractOne(false));
        }
    }
}
