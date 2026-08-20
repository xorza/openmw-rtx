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

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

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
            const ExtractionStats stats = extractor.extract(*root, osg::Matrixf::identity());

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
            const ExtractionStats stats = extractor.extract(*root, osg::Matrixf::identity());

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
            extractor.extract(*inner, osg::Matrixf::translate(0.0f, 0.0f, 5.0f));

            // The quad's (1,1,0) corner doubles to (2,2,0), then rises by five.
            ASSERT_EQ(scene.getInstances().size(), 1u);
            EXPECT_EQ(osg::Vec3f(1.0f, 1.0f, 0.0f) * scene.getInstances()[0].mTransform, osg::Vec3f(2.0f, 2.0f, 5.0f));
        }

        /// The property the incremental mirror rests on: nothing changed, so nothing is added.
        TEST(RtxSceneExtractorTest, aSecondPassOverAnUnchangedGraphAddsNothing)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(makeQuad());
            root->addChild(makeQuad());

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            extractor.extract(*root, osg::Matrixf::identity());

            const ExtractionStats second = extractor.extract(*root, osg::Matrixf::identity());

            EXPECT_EQ(second.mMeshesAdded, 0u);
            EXPECT_EQ(second.mMeshesReused, 2u);
            EXPECT_EQ(scene.getMeshes().size(), 2u);

            // Instances are not deduplicated, and must not be: the same mesh at two places is two
            // rows of the acceleration structure. A second pass over the same graph places it again.
            EXPECT_EQ(scene.getInstances().size(), 4u);
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
            extractor.extract(*geometry, osg::Matrixf::identity());

            EXPECT_EQ(scene.getTriangleCount(), 1u);
        }

        TEST(RtxSceneExtractorTest, geometryWithNoTrianglesIsSkippedRatherThanAdded)
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({ osg::Vec3f(0.0f, 0.0f, 0.0f) }));

            Rtx::SceneDesc scene;
            SceneExtractor extractor(scene);
            const ExtractionStats stats = extractor.extract(*geometry, osg::Matrixf::identity());

            EXPECT_EQ(stats.mSkippedEmpty, 1u);
            EXPECT_EQ(stats.mInstances, 0u);
            EXPECT_TRUE(scene.getMeshes().empty());
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
            extractor.extract(*parent, osg::Matrixf::identity());

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
                extractor.extract(*quad, osg::Matrixf::identity());

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
                extractor.extract(*quad, osg::Matrixf::identity());

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
                extractor.extract(*quad, osg::Matrixf::identity());
                return scene.getMaterials().empty() ? true : scene.getMaterials()[0].mTwoSided;
            };

            EXPECT_FALSE(extractOne(true));
            EXPECT_TRUE(extractOne(false));
        }
    }
}
