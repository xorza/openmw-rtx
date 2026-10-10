#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/AlphaFunc>
#include <osg/BlendFunc>
#include <osg/FrameStamp>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Texture>
#include <osg/Uniform>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>
#include <osgUtil/UpdateVisitor>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/graph.hpp>
#include <apps/components_tests/rtx/support/graphlight.hpp>
#include <apps/components_tests/rtx/support/sceneholds.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/rtx/mirror/cells/templatewalk.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/lightbuilder.hpp>
#include <components/rtx/mirror/sceneextractor.hpp>
#include <components/rtx/mirror/statereading.hpp>
#include <components/rtx/mirror/surfacedescription.hpp>
#include <components/rtx/scene/instancerecord.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/sceneutil/texmat.hpp>
#include <components/sceneutil/texturetype.hpp>
#include <components/sceneutil/util.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// The same controller as an update callback, which is how `NifOsg` hangs everything the
        /// content did not mark auto-play.
        ///
        /// `StateSetUpdater::applyUpdate` alternates the node's own state set between two copies of
        /// itself, one per traversal parity, so a mirror keying a material on that address adds one
        /// and sweeps one every frame for a surface that has not moved — and every placement
        /// standing on it has to be repointed each time.
        /// Morrowind scrolls lava, waterfalls and banners by moving a texture matrix rather than
        /// geometry, and the description records the two numbers that matrix was built from. The
        /// sampler takes `uv * xy + zw`, so the scale-about-the-middle has to be resolved on the way
        /// through — a surface scaled by two with no offset samples `uv * 2 - 0.5`, which is the
        /// middle of the texture staying put while its edges move outward.
        TEST_F(RtxSceneExtractorTest, aScrollingSurfaceCarriesItsUvTransformResolvedForTheSampler)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            paint(*quad->getOrCreateStateSet(), "lava.dds");

            // The matrix `NifOsg::UVController` builds for a scale of two by four and an offset of a
            // quarter by minus a half.
            const osg::Vec3f origin(0.5f, 0.5f, 0.0f);
            osg::Matrixf transform = osg::Matrixf::translate(origin);
            transform.preMultScale(osg::Vec3f(2.0f, 4.0f, 1.0f));
            transform.preMultTranslate(-origin);
            transform.setTrans(transform.getTrans() + osg::Vec3f(0.25f, -0.5f, 0.0f));
            SceneUtil::setupTexMatForStateSet(*quad->getOrCreateStateSet(), 0, transform);

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(quad);

            walk(*root);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);

            // 0.5 * (1 - 2) + 0.25 = -0.25, and 0.5 * (1 - 4) - 0.5 = -2.
            EXPECT_EQ(mScene.materials().getRows()[0].mTextureTransform, osg::Vec4f(2.0f, 4.0f, -0.25f, -2.0f));
        }

        /// The identity, and not by accident: every surface that does not scroll shares one sampler
        /// path with the ones that do, so the transform has to be a no-op rather than a branch.
        TEST_F(RtxSceneExtractorTest, aSurfaceThatDoesNotScrollCarriesTheIdentityTransform)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            paint(*quad->getOrCreateStateSet(), "stone.dds");

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(quad);

            walk(*root);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            EXPECT_EQ(mScene.materials().getRows()[0].mTextureTransform, osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f));
        }

        TEST_F(RtxSceneExtractorTest, aMaterialKeepsItsSlotWhileTheNodesOwnStateSetAlternates)
        {
            osg::ref_ptr<ColourController> controller = new ColourController;

            osg::ref_ptr<osg::Group> node = new osg::Group;
            node->addChild(makeQuad());
            node->addUpdateCallback(controller);

            osgUtil::UpdateVisitor update;

            for (unsigned int pass = 1; pass <= 4; ++pass)
            {
                update.setTraversalNumber(pass);
                node->accept(update);

                mScene.clearPlacement();
                const ExtractionStats found = walk(*node, 0, pass);
                const Retirement went = mExtractor.retire();

                // The first pass is where everything arrives; what is asserted is that no later one
                // is, and the parity has turned over twice by the last.
                if (pass == 1)
                    continue;

                EXPECT_EQ(found.mMaterialsAdded, 0u) << "pass " << pass;
                EXPECT_EQ(found.mMaterialsReused, 1u) << "pass " << pass;
                EXPECT_EQ(went.mMaterials, 0u) << "pass " << pass << ": swept is added again next frame";
            }

            EXPECT_EQ(mScene.materials().getRows().size(), 1u);
        }

        TEST_F(RtxSceneExtractorTest, degenerateTrianglesAreDropped)
        {
            // One real triangle and two zero-area ones, which is how a triangle strip restarts.
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
            }));
            geometry->addPrimitiveSet(makeTriangles({ 0, 1, 2, 0, 0, 1, 2, 2, 2 }));

            walk(*geometry);

            EXPECT_EQ(mScene.meshes().getTriangleCount(), 1u);
        }

        TEST_F(RtxSceneExtractorTest, geometryWithNoTrianglesIsSkippedRatherThanAdded)
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(makePositions({ osg::Vec3f(0.0f, 0.0f, 0.0f) }));

            const ExtractionStats stats = walk(*geometry);

            EXPECT_EQ(stats.mSkippedEmpty, 1u);
            EXPECT_EQ(stats.mInstances, 0u);
            EXPECT_TRUE(mScene.meshes().getRows().empty());

            EXPECT_EQ(mScene.refusals().count(Refused::Mesh), 0u) << "a point with no triangle draws nothing anywhere";

            // Filed as it was found, so the next walk neither reads it nor adds it.
            mScene.clearPlacement();
            const ExtractionStats again = walk(*geometry, 0, 1);
            EXPECT_EQ(again.mSkippedEmpty, 0u) << "an empty drawable read again";
            EXPECT_EQ(again.mInstances, 0u);
            EXPECT_TRUE(mScene.meshes().getRows().empty());
        }

        /// **Lines are refused and not filed as empty**: the rasterizer draws an `NiLines`, and a ray
        /// has no width of theirs to meet, so the log says so once for the drawable.
        TEST_F(RtxSceneExtractorTest, linesAreRefusedWhereTheRasterizerDrawsThem)
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setName("a rope");
            geometry->setVertexArray(makePositions({ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f) }));
            geometry->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, 2));

            const ExtractionStats stats = walk(*geometry);
            EXPECT_EQ(stats.mInstances, 0u);
            EXPECT_EQ(mScene.refusals().count(Refused::Mesh), 1u);

            mScene.clearPlacement();
            walk(*geometry, 0, 1);
            EXPECT_EQ(mScene.refusals().count(Refused::Mesh), 1u) << "said once";
        }

        /// **What a surface states and the trace does not read is a refusal of its state**, named by
        /// its diffuse texture and said once whatever stands on it; the surface is drawn.
        TEST_F(RtxSceneExtractorTest, aSurfacesUnreadStateIsRefusedOnceAndTheSurfaceDrawn)
        {
            const auto multiplied = [] {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                paint(state, "textures/tx_a_steel.dds");
                state.setAttributeAndModes(new osg::BlendFunc(GL_DST_COLOR, GL_ZERO));
                return quad;
            };

            const ExtractionStats first = walk(*multiplied());
            EXPECT_EQ(first.mInstances, 1u) << "the surface is drawn";
            EXPECT_EQ(mScene.refusals().count(Refused::Surface), 1u);

            mScene.clearPlacement();
            walk(*multiplied(), 0, 1);
            EXPECT_EQ(mScene.refusals().count(Refused::Surface), 1u) << "said once for the texture";
        }

        /// A drawable that describes nothing inherits what the state sets above it say.
        ///
        /// **Folded down the chain, which is how OpenGL resolves it.** A NIF property on a node
        /// applies to every shape below it until another replaces it, and a drawable carrying a
        /// state set for some unrelated reason — a uniform and nothing else, which is common — does
        /// not lose the surface it inherits by having one.
        TEST_F(RtxSceneExtractorTest, aDrawableWithNoDescriptionInheritsTheNearestOneAbove)
        {
            osg::ref_ptr<osg::Group> parent = new osg::Group;
            paint(*parent->getOrCreateStateSet(), "textures/tx_stone_01.dds");

            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            quad->getOrCreateStateSet()->addUniform(new osg::Uniform("useFalloff", false));
            parent->addChild(quad);

            walk(*parent);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            ASSERT_EQ(mScene.textures().getRows().size(), 1u);
            EXPECT_EQ(mScene.textures().getRows()[0].mPath, VFS::Path::NormalizedView("textures/tx_stone_01.dds"));
            EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, 0u);

            // Nothing on the chain turned culling off, so the surface shows one face: the scene
            // root culls, and only a record that says otherwise makes a surface two-sided.
            EXPECT_FALSE(mScene.materials().getRows()[0].mTwoSided);
        }

        /// A blend is what marks a cutout in this data, and it has to survive into the material.
        ///
        /// Morrowind's foliage, grates and banners are drawn with `NiAlphaProperty` over a texture
        /// whose alpha is binary; hardly anything in the game sets an alpha test. Losing the blend
        /// here loses every mask with it.
        ///
        /// **And the texture's alpha decides between a cut and a pane.** A mask is solid wherever
        /// its paint is, and stays a cut however soft its fringe. A texture that never reaches
        /// solid is no mask, so the Imperial lantern's glass, whose alpha peaks at 119, is a pane
        /// and not a hole. A test cuts at its reference whatever the texture holds. A file nothing
        /// here reads answers what traces it as before: a cut.
        TEST_F(RtxSceneExtractorTest, aBlendIsACutWhereItsTextureIsAMaskAndAPaneWhereItNeverCloses)
        {
            const auto imageOf = [](std::array<std::uint8_t, 4> alphas) {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->setFileName("textures/tx_window_pane.dds");
                image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                for (std::size_t texel = 0; texel < alphas.size(); ++texel)
                    image->data()[texel * 4 + 3] = alphas[texel];
                return image;
            };

            const auto extractOne = [](bool blend, osg::Image* image, float test = 0.0f) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                if (image != nullptr)
                    paint(state, *image);
                else
                    paint(state, "textures/tx_leaves.dds");
                if (blend)
                    state.setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);
                if (test > 0.0f)
                    state.setAttributeAndModes(new osg::AlphaFunc(osg::AlphaFunc::GREATER, test));

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                return scene.materials().getRows().front();
            };

            const Rtx::Material unread = extractOne(true, nullptr);
            EXPECT_EQ(unread.mAlphaMode, AlphaMode::Blend);
            EXPECT_TRUE(unread.isCutout());
            EXPECT_FALSE(unread.isTranslucent()) << "a file nothing reads keeps the cut";

            const Rtx::Material plain = extractOne(false, nullptr);
            EXPECT_EQ(plain.mAlphaMode, AlphaMode::Opaque);
            EXPECT_FALSE(plain.isCutout());

            osg::ref_ptr<osg::Image> leaves = imageOf({ 0, 255, 119, 0 });
            const Rtx::Material leaf = extractOne(true, leaves);
            EXPECT_FALSE(leaf.isTranslucent()) << "a mask with a soft fringe is cut";
            EXPECT_TRUE(leaf.getTraversed().mCutout);
            EXPECT_EQ(leaf.getAlphaTest().mReference, Material::sBlendCutoff);

            osg::ref_ptr<osg::Image> glass = imageOf({ 119, 102, 119, 0 });
            const Rtx::Material pane = extractOne(true, glass);
            EXPECT_TRUE(pane.isTranslucent()) << "a texture that never closes is a pane";
            EXPECT_FALSE(pane.isMedium()) << "all there, so a pane and no cloud";
            EXPECT_EQ(pane.getAlphaTest().mReference, Shaders::ALPHA_PANE_CUTOFF);
            EXPECT_TRUE(pane.getTraversed().placedAt(1.0f).mTranslucent);
            EXPECT_FALSE(pane.getTraversed().placedAt(1.0f).mCutout);

            const Rtx::Material tested = extractOne(true, glass, 0.5f);
            EXPECT_FALSE(tested.isTranslucent()) << "a test cuts whatever the texture holds";
            EXPECT_EQ(tested.getAlphaTest().mReference, 0.5f);

            // The ring's reader hands over the same facts with its reading.
            PreparedModel model;
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            paint(*quad->getOrCreateStateSet(), *glass);
            quad->getOrCreateStateSet()->setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);
            TemplateWalk walk(nullptr);
            walk.read(*quad, ~0u, model);
            ASSERT_EQ(model.mParts.size(), 1u);
            ASSERT_TRUE(model.mParts[0].mMaterial.mDiffuseFacts.has_value());
            EXPECT_EQ(model.mParts[0].mMaterial.mDiffuseFacts->mReachesSolid, std::optional<bool>(false));
            EXPECT_FALSE(model.mParts[0].mMaterial.mDiffuseFacts->mMean.has_value()) << "a pane asks no mean";

            // **And lays what the hand-over would widen**, beside the file: the RGBA8 pane uploads as
            // it is, and an RGB8 file is laid on the reader's thread under its own name.
            const SurfaceDescription& read = *model.mParts[0].mMaterial.mDescribed;
            EXPECT_EQ(read.getTextureUse(SurfaceMap::Diffuse).mLaid, nullptr) << "a file uploaded as it is was laid";

            osg::ref_ptr<osg::Image> loose = new osg::Image;
            loose->setFileName("textures/tx_loose.tga");
            loose->allocateImage(2, 2, 1, GL_RGB, GL_UNSIGNED_BYTE);
            PreparedModel looseModel;
            osg::ref_ptr<osg::Geometry> looseQuad = makeQuad();
            paint(*looseQuad->getOrCreateStateSet(), *loose);
            walk.read(*looseQuad, ~0u, looseModel);
            ASSERT_EQ(looseModel.mParts.size(), 1u);
            const TextureUse& laid = looseModel.mParts[0].mMaterial.mDescribed->getTextureUse(SurfaceMap::Diffuse);
            EXPECT_EQ(laid.mImage, loose) << "the file was replaced rather than laid beside";
            ASSERT_NE(laid.mLaid, nullptr) << "a widened file was left to the hand-over";
            EXPECT_EQ(laid.mLaid->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
            EXPECT_EQ(laid.mLaid->getFileName(), loose->getFileName());
        }

        /// A surface that adds — `SRC_ALPHA, ONE` — is no cutout, no pane and no medium: it is
        /// placed under `MASK_ADDITIVE` alone, built non-opaque so the one query that gathers it
        /// walks every crossing, and met by nothing that shades. A `SRC_ALPHA, DST_ALPHA` reads
        /// the same, because the frame's alpha is one; `ONE, ONE` adds whole.
        TEST_F(RtxSceneExtractorTest, anAdditiveSurfaceIsPlacedOnItsOwnMaskAndMetByNothingThatShades)
        {
            const auto extractOne = [](GLenum source, GLenum destination, float opacity) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                paint(state, "textures/vfx_alt_glow02.dds");
                state.setAttributeAndModes(new osg::BlendFunc(source, destination), osg::StateAttribute::ON);

                osg::ref_ptr<SceneUtil::Material> colours = new SceneUtil::Material;
                colours->setDiffuse(osg::Vec4f(1.0f, 1.0f, 1.0f, opacity));
                state.setAttribute(colours);

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                const Rtx::Material material = scene.materials().getRows().front();

                std::vector<InstanceRecord> records;
                makeInstanceRecords(scene, records);
                EXPECT_EQ(records.size(), 1u);

                return std::pair(material, records.front());
            };

            const auto [adds, addsRecord] = extractOne(GL_SRC_ALPHA, GL_ONE, 0.5f);
            EXPECT_EQ(adds.mBlend, BlendKind::Add);
            EXPECT_TRUE(adds.isAdditive());
            EXPECT_FALSE(adds.isCutout());
            EXPECT_FALSE(adds.isTranslucent()) << "an alpha of a half on an additive surface covers nothing";
            EXPECT_FALSE(adds.isMedium());
            EXPECT_EQ(addsRecord.mMask, Shaders::MASK_ADDITIVE);
            EXPECT_TRUE(addsRecord.mAdditive);
            EXPECT_FALSE(addsRecord.mCutout);
            EXPECT_FALSE(addsRecord.mTranslucent);

            const auto [frameAlpha, frameRecord] = extractOne(GL_SRC_ALPHA, GL_DST_ALPHA, 1.0f);
            EXPECT_EQ(frameAlpha.mBlend, BlendKind::Add);
            EXPECT_EQ(frameRecord.mMask, Shaders::MASK_ADDITIVE);

            const auto [whole, wholeRecord] = extractOne(GL_ONE, GL_ONE, 1.0f);
            EXPECT_EQ(whole.mBlend, BlendKind::AddWhole);
            EXPECT_EQ(wholeRecord.mMask, Shaders::MASK_ADDITIVE);

            // And the pane it is not: the same alpha over `ONE_MINUS_SRC_ALPHA` is glass.
            const auto [over, overRecord] = extractOne(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 0.5f);
            EXPECT_EQ(over.mBlend, BlendKind::Over);
            EXPECT_FALSE(over.isAdditive());
            EXPECT_TRUE(over.isTranslucent());
            EXPECT_EQ(overRecord.mMask, Shaders::MASK_STATIC);
            EXPECT_TRUE(overRecord.mTranslucent);
        }

        /// The white ambient the game gives a magic effect joins the material's glow, times the
        /// material's own ambient colour, which is where `objects.frag` puts `ambientColor *
        /// ambientLight`. An ambient of (0.5, 1, 0.25) under a white override adds its decode to the
        /// glow; with no override nothing is added.
        ///
        /// The numbers: `decodeColour` undoes the sRGB curve, so 0.5 is 0.2140411, 1 is 1 and
        /// 0.25 is 0.0508761; an emission of 0.25 with a multiplier of two is 0.1017522.
        ///
        /// **And the row carries the ambient, decoded, either way**: what the fill is reflected by,
        /// `Material::mAmbientColour`.
        TEST_F(RtxSceneExtractorTest, theAmbientTheGameOverridesJoinsTheGlowByTheMaterialsAmbient)
        {
            const auto extractOne = [](bool overridden) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();

                osg::ref_ptr<SceneUtil::Material> colours = new SceneUtil::Material;
                colours->setAmbient(osg::Vec4f(0.5f, 1.0f, 0.25f, 1.0f));
                colours->setEmission(osg::Vec4f(0.25f, 0.25f, 0.25f, 1.0f));
                colours->setEmissiveMultiplier(2.0f);
                state.setAttribute(colours);
                if (overridden)
                    state.addUniform(new osg::Uniform("sun.ambient", osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f)));

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);
                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                return scene.materials().getRows().front();
            };

            for (const bool overridden : { true, false })
            {
                const std::optional<osg::Vec3f> ambient = extractOne(overridden).mAmbientColour;
                ASSERT_TRUE(ambient.has_value()) << "the content stated one";
                EXPECT_NEAR(ambient->x(), 0.2140411f, 1.0e-6f);
                EXPECT_FLOAT_EQ(ambient->y(), 1.0f);
                EXPECT_NEAR(ambient->z(), 0.0508761f, 1.0e-6f);
            }

            const osg::Vec3f lit = extractOne(true).mEmissiveColour;
            EXPECT_NEAR(lit.x(), 0.1017522f + 0.2140411f, 1.0e-6f);
            EXPECT_NEAR(lit.y(), 0.1017522f + 1.0f, 1.0e-6f);
            EXPECT_NEAR(lit.z(), 0.1017522f + 0.0508761f, 1.0e-6f);

            const osg::Vec3f unlit = extractOne(false).mEmissiveColour;
            EXPECT_NEAR(unlit.x(), 0.1017522f, 1.0e-6f);
            EXPECT_NEAR(unlit.y(), 0.1017522f, 1.0e-6f);
            EXPECT_NEAR(unlit.z(), 0.1017522f, 1.0e-6f);
        }

        /// The environment map, its tint and the dark map with its unit all reach the material,
        /// each in its own slot, and a file bound clamped and bound repeating is two slots.
        TEST_F(RtxSceneExtractorTest, theEnvironmentAndDarkMapsReachTheMaterialAndAWrapIsASlotOfItsOwn)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            osg::StateSet& state = *quad->getOrCreateStateSet();
            paint(state, "textures/a_glass.dds");
            paint(state, "textures/tx_6th_dark.dds", TextureRole::Dark);
            paint(state, "textures/vfx_alt_envir.dds", TextureRole::Environment);
            state.addUniform(new osg::Uniform("envMapColor", osg::Vec4f(1.0f, 0.5f, 0.0f, 1.0f)));

            // The dark map at unit one, and the same file as the diffuse bound again clamped at
            // unit three — the loader binds one unit per texture and a wrap per texture.
            osg::ref_ptr<osg::Image> sameFile = new osg::Image;
            sameFile->setFileName("textures/a_glass.dds");
            osg::ref_ptr<osg::Texture2D> clamped = new osg::Texture2D(sameFile);
            clamped->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            clamped->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            state.setTextureAttributeAndModes(3, clamped, osg::StateAttribute::ON);
            state.setTextureAttribute(3, new SceneUtil::TextureType("emissiveMap"), osg::StateAttribute::ON);

            SurfaceDescription described;
            describeStateSet(state, described);
            EXPECT_EQ(described.getTextureUse(SurfaceMap::Emissive).mWrap, TextureWrap::Clamp);
            EXPECT_EQ(described.getTexture(SurfaceMap::Emissive), sameFile.get());
            EXPECT_EQ(described.mEmissiveUnit, 3);

            walk(*quad);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            const Rtx::Material& material = mScene.materials().getRows().front();
            EXPECT_NE(material.mDark, sNoIndex);
            EXPECT_EQ(material.mDarkUnit, 1);
            EXPECT_EQ(material.mEmissiveUnit, 3);
            EXPECT_NE(material.mEnvironment, sNoIndex);
            EXPECT_NEAR(material.mEnvironmentColour.x(), 1.0f, 1.0e-6f);
            EXPECT_NEAR(material.mEnvironmentColour.y(), 0.2140411f, 1.0e-6f);
            EXPECT_NEAR(material.mEnvironmentColour.z(), 0.0f, 1.0e-6f);

            EXPECT_NE(material.mDiffuse, material.mEmissive) << "one file under two wraps is two slots";
            EXPECT_EQ(mScene.textures().getRows()[material.mDiffuse].mPath,
                mScene.textures().getRows()[material.mEmissive].mPath);
            EXPECT_EQ(mScene.textures().getRows()[material.mDiffuse].mWrap, TextureWrap::Repeat);
            EXPECT_EQ(mScene.textures().getRows()[material.mEmissive].mWrap, TextureWrap::Clamp);
            EXPECT_EQ(mScene.textures().getRows().size(), 4u);
        }

        /// **A model's own image is traced under its stamp, and an image with no name is refused.**
        /// The NIF loader stamps an image a model carries inside it with the model and the record
        /// (`SceneUtil::EmbeddedImage`), so two images of one record share one slot, as two of one
        /// file do. An image with neither a file nor a stamp has nothing a table finds it by: its
        /// surface is traced untextured, and the log says so once.
        TEST_F(RtxSceneExtractorTest, aModelsOwnImageIsTracedUnderItsStampAndANamelessOneRefused)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            std::array<osg::ref_ptr<osg::Image>, 3> images;
            for (osg::ref_ptr<osg::Image>& image : images)
            {
                image = new osg::Image;
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                paint(*quad->getOrCreateStateSet(), *image);
                root->addChild(quad);
            }
            SceneUtil::EmbeddedImage::stamp(*images[0], "meshes/i/tx_crystal_02.nif", 7);
            SceneUtil::EmbeddedImage::stamp(*images[1], "meshes/i/tx_crystal_02.nif", 7);

            walk(*root);

            std::vector<Index> diffuse;
            for (const Rtx::Material& material : mScene.materials().getRows())
                diffuse.push_back(material.mDiffuse);
            std::sort(diffuse.begin(), diffuse.end());
            ASSERT_EQ(diffuse.size(), 3u);
            EXPECT_NE(diffuse[0], sNoIndex);
            EXPECT_EQ(diffuse[0], diffuse[1]) << "two images of one record took two slots";
            EXPECT_EQ(diffuse[2], sNoIndex) << "the nameless image took a slot";
            EXPECT_EQ(mScene.textures().getRows()[diffuse[0]].mPath.value(), "meshes/i/tx_crystal_02.nif#7");
            EXPECT_EQ(mScene.refusals().count(Refused::Texture), 1u);
        }

        /// **The companion maps reach the material, the specular map in the layout that names what
        /// its channels mean.** A normal map with height and one without are one map. The metal
        /// layout reads the specular map as data, and the classic one as a colour, its highlight
        /// being one the artist saw. **A normal map bound with its height is parallax**, and not on a
        /// cutout, whose hole the traversal finds with no eye to shift by.
        TEST_F(RtxSceneExtractorTest, theCompanionMapsReachTheMaterialAsDataAndTheSpecularMapOnlyInItsLayout)
        {
            const auto extractOne = [](SpecularLayout layout, TextureRole normalRole, bool cutout = false) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();
                paint(state, "textures/tx_a_steel.dds");
                paint(state, "textures/tx_a_steel_nh.dds", normalRole);
                paint(state, "textures/tx_a_steel_spec.dds", TextureRole::Specular);
                if (cutout)
                    state.setAttributeAndModes(new osg::AlphaFunc(osg::AlphaFunc::GEQUAL, 0.5f));

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = layout };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                return std::pair{ scene.materials().getRows().front(),
                    std::vector<TextureRow>(scene.textures().getRows().begin(), scene.textures().getRows().end()) };
            };

            const auto [classic, classicRows] = extractOne(SpecularLayout::Classic, TextureRole::NormalHeight);
            ASSERT_NE(classic.mNormal, sNoIndex);
            EXPECT_EQ(classicRows[classic.mNormal].mPath, VFS::Path::NormalizedView("textures/tx_a_steel_nh.dds"));
            EXPECT_EQ(classicRows[classic.mNormal].mEncoding, TextureEncoding::Normal);
            EXPECT_EQ(classicRows[classic.mDiffuse].mEncoding, TextureEncoding::Colour);
            ASSERT_NE(classic.mSpecular, sNoIndex);
            EXPECT_EQ(classicRows[classic.mSpecular].mPath, VFS::Path::NormalizedView("textures/tx_a_steel_spec.dds"));
            EXPECT_EQ(classicRows[classic.mSpecular].mEncoding, TextureEncoding::Colour);
            EXPECT_EQ(classicRows.size(), 3u);
            EXPECT_TRUE(classic.mSpecularClassic);
            EXPECT_TRUE(classic.mParallax) << "a normal map with its height";

            const auto [read, readRows] = extractOne(SpecularLayout::MetalRoughness, TextureRole::Normal);
            ASSERT_NE(read.mNormal, sNoIndex);
            ASSERT_NE(read.mSpecular, sNoIndex);
            EXPECT_EQ(readRows[read.mSpecular].mPath, VFS::Path::NormalizedView("textures/tx_a_steel_spec.dds"));
            EXPECT_EQ(readRows[read.mSpecular].mEncoding, TextureEncoding::Data);
            EXPECT_EQ(readRows.size(), 3u);
            EXPECT_FALSE(read.mParallax) << "a normal map without one";
            EXPECT_FALSE(read.mSpecularClassic);

            const auto [cut, cutRows] = extractOne(SpecularLayout::Classic, TextureRole::NormalHeight, true);
            EXPECT_TRUE(cut.isCutout());
            EXPECT_FALSE(cut.mParallax) << "a cutout, shifted where it shades and not where it is cut";
        }

        /// A controller that shows a different sheet every frame, out of `mSheets`, on the unit
        /// the enchantment's glow uses: `SceneUtil::GlowUpdater`, with its clock made explicit.
        class FlipController : public SceneUtil::StateSetUpdater
        {
        public:
            std::vector<osg::ref_ptr<osg::Image>> mSheets;
            std::size_t mShown = 0;

            /// Unit one by number and not by `paint`, which appends: the walk hands the controller
            /// a copy of the node's own state set, which already carries the diffuse at nought.
            void setDefaults(osg::StateSet* stateset) override
            {
                stateset->setTextureAttributeAndModes(1, new osg::Texture2D(mSheets.front()), osg::StateAttribute::ON);
                stateset->setTextureAttribute(1,
                    new SceneUtil::TextureType(std::string(sTextureRoleNames.name(TextureRole::Environment))),
                    osg::StateAttribute::ON);
            }

            void apply(osg::StateSet* stateset, osg::NodeVisitor*) override
            {
                stateset->setTextureAttribute(
                    1, new osg::Texture2D(mSheets[mShown]), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
            }
        };

        osg::ref_ptr<FlipController> makeFlip()
        {
            osg::ref_ptr<FlipController> controller = new FlipController;
            for (std::size_t sheet = 0; sheet < 32; ++sheet)
            {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->setFileName("textures/magicitem/caust" + std::to_string(sheet) + ".dds");
                controller->mSheets.push_back(image);
            }
            return controller;
        }

        /// A shape wearing `state`: on the node the way `NifOsg` builds one, or on the drawable
        /// under it, which is the other place a walk can meet a state set.
        osg::ref_ptr<osg::Group> makeShape(osg::StateSet* state, const bool onDrawable = false)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            osg::ref_ptr<osg::Group> shape = new osg::Group;
            if (onDrawable)
                quad->setStateSet(state);
            else
                shape->setStateSet(state);
            shape->addChild(quad);
            return shape;
        }

        osg::ref_ptr<osg::StateSet> shapeState()
        {
            osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
            paint(*state, "textures/w_sword.dds");
            state->setAttribute(new SceneUtil::Material, osg::StateAttribute::ON);
            return state;
        }

        /// An animated material keeps every texture it has worn, so a controller that cycles
        /// thirty-two caustic sheets at sixteen a second — `SceneUtil::GlowUpdater` — takes no slot
        /// and gives none back on any frame after the first it showed each on. The material's death
        /// lets them all go.
        ///
        /// **The sheet cycles beside a diffuse that stays**, which is the shape the game's glow has:
        /// a ring sized for the cycle alone holds the diffuse in it too, and from the second cycle
        /// on every sheet change drops the sheet due next and takes it up again.
        TEST_F(RtxSceneExtractorTest, anAnimatedMaterialKeepsEveryTextureItHasWorn)
        {
            osg::ref_ptr<FlipController> controller = makeFlip();
            osg::ref_ptr<osg::Group> node = makeShape(shapeState());
            node->addUpdateCallback(controller);

            osgUtil::UpdateVisitor update;
            for (unsigned int frame = 1; frame <= 72; ++frame)
            {
                controller->mShown = (frame - 1) % 32;
                update.setTraversalNumber(frame);
                node->accept(update);

                mScene.clearPlacement();
                walk(*node, 0, frame);

                ASSERT_EQ(mScene.materials().getRows().size(), 1u) << "on frame " << frame;
                EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, 0u) << "on frame " << frame;
                EXPECT_EQ(mScene.materials().getRows()[0].mEnvironment, controller->mShown + 1)
                    << "the sheet shown on frame " << frame;

                // The diffuse and a slot per distinct sheet as each is first shown, and none given
                // back: on the second time round every slot is already there.
                const std::size_t worn = std::min<std::size_t>(frame, 32) + 1;
                EXPECT_EQ(mScene.textures().getRows().size(), worn) << "on frame " << frame;
                EXPECT_EQ(mScene.textures().getLiveCount(), worn) << "on frame " << frame;
                if (frame > 32)
                {
                    EXPECT_TRUE(mScene.textures().getArrived().empty()) << "a sheet arrived again on frame " << frame;
                    EXPECT_TRUE(mScene.textures().getFreed().empty()) << "a sheet was given back on frame " << frame;
                }

                mExtractor.retire();
                mScene.clearArrivals();
            }

            // The material goes, and every sheet with it.
            node->removeChild(0, 1);
            mScene.clearPlacement();
            walk(*node, 0, 73);
            mExtractor.retire();

            for (std::size_t slot = 0; slot < 33; ++slot)
                EXPECT_FALSE(mScene.textures().isLive(slot)) << "slot " << slot << " outlived the material";
        }

        /// What `MWRender::TransparencyUpdater` does: sets its blend and its two uniforms up, and
        /// writes the fade every frame with no test that the uniform is there.
        class FadeController : public SceneUtil::StateSetUpdater
        {
        public:
            float mFade = 1.0f;

            void setDefaults(osg::StateSet* stateset) override
            {
                stateset->setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);
                stateset->addUniform(new osg::Uniform("alpha", 1.0f));
                stateset->addUniform(new osg::Uniform("actorFade", 1.0f));
            }

            void apply(osg::StateSet* stateset, osg::NodeVisitor*) override
            {
                stateset->getUniform("actorFade")->set(mFade);
            }
        };

        /// A fade that arrives after a glow finds its own defaults, both apply, and nothing the fade
        /// set stays once it goes.
        ///
        /// **What an actor casting and then going invisible does**: the glow rides the root's update
        /// chain and the fade its cull chain. A state set set up by the glow alone had no
        /// `actorFade`, and the fade dereferenced it on the next walk.
        TEST_F(RtxSceneExtractorTest, aFadeAfterAGlowFindsItsOwnDefaultsAndLeavesNothingWhenItGoes)
        {
            osg::ref_ptr<FlipController> glow = makeFlip();
            osg::ref_ptr<FadeController> fade = new FadeController;
            fade->mFade = 0.25f;

            osg::ref_ptr<osg::Group> root = makeShape(shapeState());
            root->addUpdateCallback(glow);

            osgUtil::UpdateVisitor update;
            const auto frame = [&](unsigned int number) {
                glow->mShown = number - 1;
                update.setTraversalNumber(number);
                root->accept(update);

                mScene.clearPlacement();
                walk(*root, 0, number);
                mExtractor.retire();
                mScene.clearArrivals();

                EXPECT_EQ(mScene.materials().getRows().front().mEnvironment, glow->mShown + 1)
                    << "the glow on frame " << number;
                return mScene.placements().getRows().front().mInstance.mOpacity;
            };

            EXPECT_EQ(frame(1), 1.0f);

            root->addCullCallback(fade);
            EXPECT_EQ(frame(2), 0.25f) << "the fade that arrived after the glow";
            EXPECT_EQ(frame(3), 0.25f);

            root->removeCullCallback(fade);
            EXPECT_EQ(frame(4), 1.0f) << "a fade that went, still applied";
        }

        /// **A spell's glow ends when the game says, and a glow given a new colour wears it.** Open
        /// and Lock put `SceneUtil::addEnchantedGlow` on a door for a second; an enchantment's glow
        /// is recoloured by the next spell. Both change the glow's defaults by
        /// `StateSetUpdater::reset`, which the node's own update consumes before the walk applies
        /// the same updater to its own copy, so the copy hears it by the updater's generation. A
        /// copy that never heard it kept the last sheet for as long as the door stood.
        TEST_F(RtxSceneExtractorTest, aSpellCastGlowEndsWhenTheGameSaysAndARecolouredGlowWearsItsColour)
        {
            const VFS::Manager vfs;
            Resource::ResourceSystem resources(&vfs, 1.0, nullptr);

            // The ray tracer compiles none of the rasterizer's shaders, and neither does this.
            resources.getSceneManager()->setShadersEnabled(false);

            const auto frame = [&](osg::Group& root, unsigned int number, double seconds) {
                osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
                stamp->setFrameNumber(number);
                stamp->setSimulationTime(seconds);
                osgUtil::UpdateVisitor update;
                update.setTraversalNumber(number);
                update.setFrameStamp(stamp);
                root.accept(update);

                runWorld(seconds - mWorldSeconds);
                mScene.clearPlacement();
                walk(root, 0, number);
                mExtractor.retire();
                mScene.clearArrivals();
                return mScene.materials().getRows()[mScene.placements().getRows().front().mInstance.mMaterial];
            };

            // A glow of a second, from ten seconds in: the node's update starts its clock at ten
            // and ends it on the first frame past eleven.
            osg::ref_ptr<osg::Group> door = makeShape(shapeState());
            SceneUtil::addEnchantedGlow(door, &resources, osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f), 1.0f);

            // The archives here are empty, so every sheet is the image manager's one stand-in,
            // which has no name the texture table would take. Named, it stands in for all of them.
            const auto* sheet = static_cast<const osg::Texture2D*>(
                door->getStateSet()->getTextureAttribute(1, osg::StateAttribute::TEXTURE));
            ASSERT_NE(sheet, nullptr) << "the glow's unit is the one after the diffuse";
            const_cast<osg::Image*>(sheet->getImage())->setFileName("textures/magicitem/caust00.dds");
            EXPECT_NE(frame(*door, 1, 10.0).mEnvironment, sNoIndex) << "the glow the spell put on";
            EXPECT_NE(frame(*door, 2, 10.5).mEnvironment, sNoIndex) << "half way through";
            EXPECT_EQ(frame(*door, 3, 11.5).mEnvironment, sNoIndex) << "ended on the frame the game ended it";
            EXPECT_EQ(frame(*door, 4, 12.0).mEnvironment, sNoIndex) << "and it stays ended";

            // A permanent glow recoloured: one and nought decode to themselves, so the colours are
            // exact.
            osg::ref_ptr<osg::Group> sword = makeShape(shapeState());
            osg::ref_ptr<SceneUtil::GlowUpdater> glow
                = SceneUtil::addEnchantedGlow(sword, &resources, osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f), -1.0f);
            EXPECT_EQ(frame(*sword, 5, 13.0).mEnvironmentColour, osg::Vec3f(1.0f, 0.0f, 0.0f));
            glow->setColor(osg::Vec4f(0.0f, 0.0f, 1.0f, 1.0f));
            EXPECT_EQ(frame(*sword, 6, 13.5).mEnvironmentColour, osg::Vec3f(0.0f, 0.0f, 1.0f))
                << "the new colour on the frame it was given";
        }

        /// A material read once keeps every map it names for as long as it stands. The walk's own
        /// hold on an image goes on the frame after the material arrived, so the row's is the one
        /// that lasts — and a map the row does not hold is freed under it, its slot handed to the
        /// next texture that arrives.
        TEST_F(RtxSceneExtractorTest, aMaterialReadOnceKeepsItsEnvironmentAndDarkMaps)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            osg::StateSet& state = *quad->getOrCreateStateSet();
            paint(state, "textures/a_glass.dds");
            paint(state, "textures/tx_6th_dark.dds", TextureRole::Dark);
            paint(state, "textures/vfx_alt_envir.dds", TextureRole::Environment);

            for (unsigned int frame = 1; frame <= 6; ++frame)
            {
                mScene.clearPlacement();
                walk(*quad, 0, frame);

                ASSERT_EQ(mScene.materials().getRows().size(), 1u) << "on frame " << frame;
                const Rtx::Material& material = mScene.materials().getRows()[0];
                EXPECT_EQ(material.mDiffuse, 0u);
                EXPECT_EQ(material.mEnvironment, 1u);
                EXPECT_EQ(material.mDark, 2u);
                EXPECT_EQ(mScene.textures().getLiveCount(), 3u) << "on frame " << frame;
                EXPECT_TRUE(mScene.textures().getFreed().empty()) << "a map was given back on frame " << frame;

                mExtractor.retire();
                mScene.clearArrivals();
            }
        }

        /// A glow the game puts on an instance's root is read into the shape under it, and the
        /// shape's state set is the one every instance of the model shares. So a shape under an
        /// animated state set is animated too and keyed on its own node: the enchanted sword's
        /// sheet cycles, and the plain sword of the same model beside it wears none.
        ///
        /// **Both places a walk meets a state set**, because the chain is what carries the answer
        /// and a drawable's own link is the last one on it.
        TEST_F(RtxSceneExtractorTest, aGlowAboveAShapeAnimatesItAndLeavesTheSameShapeElsewhereAlone)
        {
            for (const bool onDrawable : { false, true })
            {
                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene, mContext);

                osg::ref_ptr<osg::StateSet> shared = shapeState();

                osg::ref_ptr<FlipController> controller = makeFlip();
                osg::ref_ptr<osg::Group> enchanted = new osg::Group;
                enchanted->addChild(makeShape(shared, onDrawable));
                enchanted->addUpdateCallback(controller);

                osg::ref_ptr<osg::Group> plain = new osg::Group;
                plain->addChild(makeShape(shared, onDrawable));

                osg::ref_ptr<osg::Group> both = new osg::Group;
                both->addChild(plain);
                both->addChild(enchanted);

                const char* const where = onDrawable ? " with the state set on the drawable" : "";

                osgUtil::UpdateVisitor update;
                for (unsigned int frame = 1; frame <= 40; ++frame)
                {
                    controller->mShown = (frame - 1) % 32;
                    update.setTraversalNumber(frame);
                    both->accept(update);

                    scene.clearPlacement();
                    extractor.extract(*both, osg::Matrixf::identity(), 0, frame);

                    ASSERT_EQ(scene.materials().getRows().size(), 2u) << "on frame " << frame << where;
                    ASSERT_EQ(scene.placements().getRows().size(), 2u) << "on frame " << frame << where;

                    const Rtx::Index plainMaterial = scene.placements().getRows()[0].mInstance.mMaterial;
                    const Rtx::Index glowingMaterial = scene.placements().getRows()[1].mInstance.mMaterial;
                    ASSERT_NE(plainMaterial, glowingMaterial) << "on frame " << frame << where;

                    const Rtx::Material& glowing = scene.materials().getRows()[glowingMaterial];
                    EXPECT_TRUE(glowing.mAnimated) << "on frame " << frame << where;
                    EXPECT_EQ(glowing.mEnvironment, controller->mShown + 1)
                        << "the sheet shown on frame " << frame << where;

                    const Rtx::Material& bare = scene.materials().getRows()[plainMaterial];
                    EXPECT_FALSE(bare.mAnimated) << "on frame " << frame << where;
                    EXPECT_EQ(bare.mEnvironment, Rtx::sNoIndex) << "on frame " << frame << where;
                    EXPECT_EQ(bare.mDiffuse, glowing.mDiffuse) << "on frame " << frame << where;

                    extractor.retire();
                    scene.clearArrivals();
                }
            }
        }

        /// **Two glows over one shared shape each show their own sheet.** `SceneUtil::CopyOp` shares
        /// a drawable between the clones of a model, and a template node stands under every
        /// reference walked from it: here one shape stands under two enchanted roots whose glows
        /// show sheets sixteen apart. The copy a controller writes is the placement's, so each
        /// placement wears its own root's sheet on every frame; one copy for the shape showed the
        /// sheet of whichever root the walk applied last, at both.
        TEST_F(RtxSceneExtractorTest, twoGlowsOverOneSharedShapeEachShowTheirOwnSheet)
        {
            for (const bool onDrawable : { false, true })
            {
                Rtx::SceneDesc scene;
                SceneExtractor extractor(scene, mContext);

                const osg::ref_ptr<osg::Group> shape = makeShape(shapeState(), onDrawable);
                const std::array<osg::ref_ptr<FlipController>, 2> glows{ makeFlip(), new FlipController };
                glows[1]->mSheets = glows[0]->mSheets;

                osg::ref_ptr<osg::Group> both = new osg::Group;
                for (const osg::ref_ptr<FlipController>& glow : glows)
                {
                    osg::ref_ptr<osg::Group> enchanted = new osg::Group;
                    enchanted->addChild(shape);
                    enchanted->addUpdateCallback(glow);
                    both->addChild(enchanted);
                }

                const char* const where = onDrawable ? " with the state set on the drawable" : "";

                osgUtil::UpdateVisitor update;
                for (unsigned int frame = 1; frame <= 4; ++frame)
                {
                    glows[0]->mShown = frame % 32;
                    glows[1]->mShown = (frame + 16) % 32;
                    update.setTraversalNumber(frame);
                    both->accept(update);

                    scene.clearPlacement();
                    extractor.extract(*both, osg::Matrixf::identity(), 0, frame);

                    const auto placements = scene.placements().getRows();
                    ASSERT_EQ(placements.size(), 2u) << "on frame " << frame << where;
                    for (std::size_t at = 0; at < 2; ++at)
                    {
                        const Rtx::Material& worn = scene.materials().getRows()[placements[at].mInstance.mMaterial];
                        ASSERT_NE(worn.mEnvironment, Rtx::sNoIndex) << "on frame " << frame << where;
                        EXPECT_EQ(scene.textures().getRows()[worn.mEnvironment].mPath,
                            VFS::Path::Normalized(glows[at]->mSheets[glows[at]->mShown]->getFileName()))
                            << "placement " << at << " on frame " << frame << where;
                    }

                    extractor.retire();
                    scene.clearArrivals();
                }
            }
        }

        /// An actor's fade rides its placement, and a model's own alpha does not ride it twice.
        ///
        /// **`alpha` has two writers and they mean different things.** `MWRender::TransparencyUpdater`
        /// writes it beside `actorFade` on a state set above the whole actor, which is where the
        /// distance fade, Invisibility and Chameleon all arrive. `NifOsg::AlphaController` writes it
        /// alone, and that one is the surface's own opacity — read into the material, and not into
        /// the placement as well, so a walk that took every `alpha` it met would not fade an
        /// animated surface twice. The pair is what tells the two apart.
        ///
        /// **On the placement and never on the material**, which is the half that cannot be got
        /// wrong: OpenMW's clone keeps state sets by reference, so every actor built from one body
        /// part reads one material, and a fade written there would fade all of them.
        TEST_F(RtxSceneExtractorTest, anActorsFadeRidesItsPlacementAndAModelsOwnAlphaDoesNot)
        {
            const auto extractOne = [](float alpha, std::optional<float> actorFade, float opacity) {
                osg::ref_ptr<osg::Group> parent = new osg::Group;
                osg::StateSet& above = *parent->getOrCreateStateSet();
                above.addUniform(new osg::Uniform("alpha", alpha));
                if (actorFade.has_value())
                    above.addUniform(new osg::Uniform("actorFade", *actorFade));

                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& own = *quad->getOrCreateStateSet();
                paint(own, "textures/tx_a_imperial_helmet.dds");
                own.setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);
                parent->addChild(quad);

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*parent, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.placements().getRows().size(), 1u);
                EXPECT_EQ(scene.materials().getRows().size(), 1u);

                // The material is asked as well, because the fade landing there instead would pass
                // every other assertion in this test.
                EXPECT_EQ(scene.materials().getRows().front().mOpacity, opacity)
                    << "a shared material took one actor's fade";

                std::vector<Rtx::InstanceRecord> records;
                Rtx::makeInstanceRecords(scene, records);
                EXPECT_EQ(records.size(), 1u);
                // A fade is not a hole: traversal still stops for the placement, as a cutout where
                // it is whole and as a translucent surface where it is not, and `candidateStops`
                // reads the mask's holes off the material either way. One of the two and not both,
                // because a translucent row is never counted as a cutout (`PlacedTraversal`).
                EXPECT_TRUE(records.front().mCutout || records.front().mTranslucent)
                    << "a fade let traversal commit the mask's holes";
                EXPECT_NE(records.front().mCutout, records.front().mTranslucent);

                return scene.placements().getRows().front().mInstance.mOpacity;
            };

            // Halves and quarters, so the product is exact in binary and the assertion is the
            // arithmetic rather than a tolerance: this is `objects.frag`'s own `alpha * actorFade`.
            EXPECT_EQ(extractOne(0.5f, 0.25f, 1.0f), 0.125f);
            EXPECT_EQ(extractOne(1.0f, 1.0f, 1.0f), 1.0f) << "an actor nothing is hiding";
            EXPECT_EQ(extractOne(0.5f, std::nullopt, 0.5f), 1.0f)
                << "a model animating its own alpha, counted once and in the material";
        }

        /// A placement keeps its slot while its fade changes, which is the only way a fade arrives.
        ///
        /// An actor fades over the last tenth of `actors processing range`, which is tens of frames
        /// of one placement standing in one slot — and a slot is what a hit reads back, so it cannot
        /// be replaced to carry a new number. A record built from the faded slot is translucent, and
        /// that is what forces the candidate loop traversal would otherwise skip.
        TEST_F(RtxSceneExtractorTest, aPlacementKeepsItsSlotWhileItsFadeChanges)
        {
            osg::ref_ptr<osg::Group> parent = new osg::Group;
            osg::StateSet& above = *parent->getOrCreateStateSet();
            osg::ref_ptr<osg::Uniform> fade = new osg::Uniform("actorFade", 1.0f);
            above.addUniform(fade);
            above.addUniform(new osg::Uniform("alpha", 1.0f));
            parent->addChild(makeQuad());

            walk(*parent);

            std::vector<Rtx::InstanceRecord> records;
            ASSERT_EQ(mScene.placements().getRows().size(), 1u);
            EXPECT_EQ(mScene.placements().getRows().front().mInstance.mOpacity, 1.0f);
            Rtx::makeInstanceRecords(mScene, records);
            EXPECT_FALSE(records.front().mTranslucent) << "an actor at full brightness stops every ray";

            mScene.placements().advance();
            fade->set(0.25f);
            walk(*parent);

            EXPECT_EQ(mScene.placements().getRows().size(), 1u) << "a second placement rather than the one that faded";
            EXPECT_EQ(mScene.placements().getRows().front().mInstance.mOpacity, 0.25f);

            // A fade is a row to rewrite — what traversal is told changed — and not a move: the
            // record carries no motion, or the actor would smear across the frame it faded on.
            ASSERT_EQ(mScene.placements().getMoved().size(), 1u)
                << "a placement that faded on the spot reported no row to write";
            EXPECT_EQ(mScene.placements().getMoved().front(), 0u);

            Rtx::makeInstanceRecords(mScene, records);
            EXPECT_TRUE(records.front().mTranslucent);
            EXPECT_EQ(records.front().mMotion, Rtx::toTransform3x4(osg::Matrixf::identity()))
                << "a fade on the spot carried a motion";
        }

        /// The emissive multiplier is folded into the colour, because their product is all the
        /// game's own shader ever uses — and it is folded in *past* the decode, because a
        /// multiplier is a gain on the light and not a colour of its own.
        ///
        /// The content states `(0.5, 0.25, 0)` in the space it was authored in, and the sRGB curve
        /// takes that to `(0.2140411, 0.0508761, 0)`. Within a millionth, because the curve is a
        /// `pow` and the numbers here are decimals.
        TEST_F(RtxSceneExtractorTest, anEmissiveMultiplierIsFoldedIntoTheColourItScales)
        {
            const auto extractOne = [](float multiplier) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                osg::StateSet& state = *quad->getOrCreateStateSet();

                SceneUtil::Material& surface = colours(state);
                surface.setEmission(osg::Vec4f(0.5f, 0.25f, 0.0f, 1.0f));
                surface.setEmissiveMultiplier(multiplier);

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                return scene.materials().getRows().front().mEmissiveColour;
            };

            const auto expectScaled = [&](float multiplier) {
                const osg::Vec3f got = extractOne(multiplier);
                EXPECT_NEAR(got.x(), 0.2140411f * multiplier, 1e-6f) << "at " << multiplier;
                EXPECT_NEAR(got.y(), 0.0508761f * multiplier, 1e-6f) << "at " << multiplier;
                EXPECT_EQ(got.z(), 0.0f) << "at " << multiplier;
            };

            expectScaled(2.0f);
            expectScaled(0.5f);

            // The default is one, so a model that asked for nothing keeps the colour it authored.
            expectScaled(1.0f);
        }

        /// A glowing surface earns no lamp, and a `LightSource` beside it is what does the lighting.
        ///
        /// **Morrowind lights what it means to light with a `LIGH` record**, and a glow is a texture.
        /// `EMISSIVE_INTENSITY` says what a glow is worth and why it is worth no lamp.
        ///
        /// The record's own lamp is still there and still derives its two sizes from the radius the
        /// record states: a flame a sixteenth of it, and the fitting around that flame a quarter.
        TEST_F(RtxSceneExtractorTest, aGlowingSurfaceEarnsNoLampAndARecordDoesTheLighting)
        {
            const auto lampsOf = [](bool torch) {
                osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform(
                    osg::Matrixf::scale(2.0f, 2.0f, 2.0f) * osg::Matrixf::translate(0.0f, 0.0f, 5.0f));

                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                colours(*quad->getOrCreateStateSet()).setEmission(osg::Vec4f(0.5f, 0.25f, 0.0f, 1.0f));
                root->addChild(quad);
                if (torch)
                    root->addChild(makeLightSource(100.0f, osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f)));

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*root, osg::Matrixf::identity(), 0);

                const std::span<const Light> lights = scene.lights();
                return std::vector<Light>(lights.begin(), lights.end());
            };

            EXPECT_TRUE(lampsOf(false).empty()) << "a glow on its own lights nothing";

            const std::vector<Light> lit = lampsOf(true);
            ASSERT_EQ(lit.size(), 1u) << "the record's own lamp and nothing beside it";
            EXPECT_EQ(lit.front().mSourceRadius, 100.0f / 16.0f) << "a record's flame is a sixteenth of its radius";
            EXPECT_EQ(lit.front().mClearance, 25.0f) << "and the fitting around it a quarter";
        }

        /// Two-sidedness is the `GL_CULL_FACE` mode the content turned off, and one face otherwise.
        ///
        /// **Off and never on.** OpenGL culls nothing unless told to and the scene root turns
        /// culling on globally, so a state set that says nothing shows one face — which is right
        /// for everything under that root — and only a `NiStencilProperty` drawing both faces, or a
        /// material file's flag, turns it off again.
        TEST_F(RtxSceneExtractorTest, aSurfaceIsTwoSidedWhenTheContentSaidSo)
        {
            const auto extractOne = [](bool twoSided) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                colours(*quad->getOrCreateStateSet());
                if (twoSided)
                    quad->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);

                Rtx::SceneDesc scene;
                WalkContext context{ .mSpecular = SpecularLayout::Classic };
                SceneExtractor extractor(scene, context);
                extractor.extract(*quad, osg::Matrixf::identity(), 0);

                EXPECT_EQ(scene.materials().getRows().size(), 1u);
                return scene.materials().getRows()[0].mTwoSided;
            };

            EXPECT_TRUE(extractOne(true));
            EXPECT_FALSE(extractOne(false));
        }

        /// A card the content doubled for its back reaches the scene as one copy, marked a sheet.
        ///
        /// Eight vertices and four triangles, because that is how a leaf is spelled in the files:
        /// the back has vertices of its own, so the pair is found by position and not by index.
        /// `ShapeFold` says why the copy goes; this says the extractor asks it and keeps its answer
        /// on the mesh.
        TEST_F(RtxSceneExtractorTest, aCardDoubledForItsBackIsFoldedToOneCopyAndMarkedASheet)
        {
            osg::ref_ptr<osg::Geometry> card = new osg::Geometry;
            card->setVertexArray(makePositions({
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
            }));
            card->addPrimitiveSet(makeTriangles({ 0, 1, 2, 0, 2, 3, 6, 5, 4, 7, 6, 4 }));
            colours(*card->getOrCreateStateSet());

            walk(*card);

            ASSERT_EQ(mScene.meshes().getRows().size(), 1u);
            EXPECT_TRUE(mScene.meshes().getRows()[0].mShape.mSheet);
            EXPECT_EQ(mScene.meshes().getRows()[0].getTriangleCount(), 2u) << "the back is gone";
            EXPECT_EQ(mScene.meshes().getRows()[0].mVertices.mCount, 8u) << "its vertices stay; nothing points at them";

            // A plain quad is a quad: nothing paired, nothing dropped, not a sheet.
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            colours(*quad->getOrCreateStateSet());

            Rtx::SceneDesc plain;
            SceneExtractor other(plain, mContext);
            other.extract(*quad, osg::Matrixf::identity(), 0);
            EXPECT_FALSE(plain.meshes().getRows()[0].mShape.mSheet);
            EXPECT_EQ(plain.meshes().getRows()[0].getTriangleCount(), 2u);
        }

        TEST_F(RtxSceneExtractorTest, aDrawableTheCallerCallsWaterIsShadedAsWaterAndTheRestAreNot)
        {
            constexpr osg::Node::NodeMask sWater = 1u << 6;
            constexpr osg::Node::NodeMask sOther = 1u << 3;

            const auto quadWith = [&](osg::Node::NodeMask mask) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                quad->setNodeMask(mask);

                // A state set of its own, because a material is keyed on one and two quads sharing
                // one would be one material between them.
                paint(*quad->getOrCreateStateSet(), "textures/water/water00.dds");

                return quad;
            };

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(quadWith(sWater));
            root->addChild(quadWith(sOther));

            // **A drawable that never set a mask, which is nearly every one in the game.** OSG
            // defaults a node mask to all ones, so a test that asks whether the water's bit is
            // *among* a drawable's bits says yes to all of them — and the whole world came out
            // shaded as sea, refracting like jelly. What names the water is that no other pass may
            // see it.
            root->addChild(quadWith(~osg::Node::NodeMask{ 0 }));

            // Nothing said, so nothing is water — which is the harness, and every caller that places
            // an analytic sea of its own instead.
            {
                Rtx::SceneDesc scene;
                SceneExtractor silent(scene, mContext);
                silent.extract(*root, osg::Matrixf::identity(), 0);

                ASSERT_EQ(scene.materials().getRows().size(), 3u);
                for (const Rtx::Material& material : scene.materials().getRows())
                    EXPECT_EQ(material.mKind, Rtx::MaterialKind::Surface);
            }

            mExtractor.setWaterMask(sWater);
            walk(*root);

            ASSERT_EQ(mScene.materials().getRows().size(), 3u);
            EXPECT_EQ(mScene.materials().getRows()[0].mKind, Rtx::MaterialKind::Water);
            EXPECT_EQ(mScene.materials().getRows()[1].mKind, Rtx::MaterialKind::Surface)
                << "a mask the caller did not name made a surface into a sea";
            EXPECT_EQ(mScene.materials().getRows()[2].mKind, Rtx::MaterialKind::Surface)
                << "a drawable with the default mask was called water, which is every drawable";

            // **What being water is actually for.** A shadow ray has to pass through the surface, or
            // every shallow in the game is lit as though the sea were a wall; the mask is where the
            // record says so, and the material kind is where it comes from.
            std::vector<Rtx::InstanceRecord> records;
            Rtx::makeInstanceRecords(mScene, records);

            ASSERT_EQ(records.size(), 3u);
            EXPECT_EQ(records[0].mMask, Rtx::Shaders::MASK_WATER);
            EXPECT_EQ(records[1].mMask, Rtx::Shaders::MASK_STATIC);
            EXPECT_EQ(records[2].mMask, Rtx::Shaders::MASK_STATIC);
            EXPECT_NE(records[0].mMask, records[1].mMask);
        }

        /// A controller of the shape `NifOsg` builds out of a `NiUVController`: it moves the texture
        /// matrix every time it is applied, and keeps everything else the state set already says.
        class ScrollController : public SceneUtil::StateSetUpdater
        {
        public:
            void setDefaults(osg::StateSet* stateset) override
            {
                SceneUtil::setupTexMatForStateSet(*stateset, 0, osg::Matrixf{});
            }

            void apply(osg::StateSet* stateset, osg::NodeVisitor*) override
            {
                mOffset += 0.01f;
                SceneUtil::setupTexMatForStateSet(*stateset, 0, osg::Matrixf::translate(mOffset, 0.0f, 0.0f));
            }

        private:
            float mOffset = 0.0f;
        };

        /// A hundred crates wear one material.
        ///
        /// One drawable under two transforms is the shape `SceneUtil::CopyOp` makes of a model
        /// placed twice: nodes copied, the drawable and its state set shared. Both placements
        /// resolve to one material.
        TEST_F(RtxSceneExtractorTest, aCopyOfADrawableWearsTheSameMaterial)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            osg::StateSet& state = *quad->getOrCreateStateSet();
            paint(state, "textures/tx_leaves.dds");
            state.setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);

            osg::ref_ptr<osg::Group> root = new osg::Group;
            for (const float x : { 0.0f, 10.0f })
            {
                osg::ref_ptr<osg::MatrixTransform> placed
                    = new osg::MatrixTransform(osg::Matrix::translate(x, 0.0, 0.0));
                placed->addChild(quad);
                root->addChild(placed);
            }

            const ExtractionStats stats = walk(*root);

            ASSERT_EQ(mScene.meshes().getRows().size(), 1u);
            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            EXPECT_FALSE(mScene.materials().getRows()[0].mAnimated);
            EXPECT_TRUE(mScene.materials().getRows()[0].isCutout());
            EXPECT_EQ(stats.mInstances, 2u);
            ASSERT_EQ(mScene.placements().getRows().size(), 2u);
            EXPECT_EQ(mScene.placements().getRows()[0].mInstance.mMaterial, 0u);
            EXPECT_EQ(mScene.placements().getRows()[1].mInstance.mMaterial, 0u);
        }

        /// A cutout under a controller is an animated material, so every placement of it reaches
        /// the any-hit.
        TEST_F(RtxSceneExtractorTest, aCutoutUnderAControllerIsAnimated)
        {
            osg::ref_ptr<osg::Group> node = new osg::Group;
            node->addChild(makeQuad());
            node->addUpdateCallback(new ScrollController);

            osg::StateSet& state = *node->getOrCreateStateSet();
            paint(state, "textures/tx_banner.dds");
            state.setAttributeAndModes(new osg::BlendFunc, osg::StateAttribute::ON);

            osgUtil::UpdateVisitor update;
            update.setTraversalNumber(1);
            node->accept(update);

            walk(*node, 0, 1);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            EXPECT_TRUE(mScene.materials().getRows()[0].mAnimated);
            EXPECT_TRUE(mScene.materials().getRows()[0].isCutout());
            ASSERT_EQ(mScene.meshes().getRows().size(), 1u);

            // And it stays animated on the frame after, when the material is read again: the flag
            // is a fact about the state set and not about what the controller wrote this time.
            update.setTraversalNumber(2);
            node->accept(update);
            mScene.clearPlacement();
            walk(*node, 0, 2);
            EXPECT_TRUE(mScene.materials().getRows()[0].mAnimated);
        }

        /// A placement wears the material of its own chain, whatever its mesh wore elsewhere.
        ///
        /// **The case the loader cannot produce, built by hand**: one drawable with no state set
        /// of its own, under two parents describing two surfaces. One mesh, two materials, and each
        /// placement wears its parent's.
        TEST_F(RtxSceneExtractorTest, aPlacementWearsTheMaterialOfItsOwnChain)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();

            osg::ref_ptr<osg::Group> root = new osg::Group;
            for (const char* const file : { "textures/tx_first.dds", "textures/tx_second.dds" })
            {
                osg::ref_ptr<osg::Group> parent = new osg::Group;
                paint(*parent->getOrCreateStateSet(), file);
                parent->addChild(quad);
                root->addChild(parent);
            }

            const ExtractionStats stats = walk(*root);

            ASSERT_EQ(mScene.meshes().getRows().size(), 1u);
            ASSERT_EQ(mScene.materials().getRows().size(), 2u);
            EXPECT_EQ(stats.mInstances, 2u);
            ASSERT_EQ(mScene.placements().getRows().size(), 2u);
            EXPECT_EQ(mScene.placements().getRows()[0].mInstance.mMaterial, 0u);
            EXPECT_EQ(mScene.placements().getRows()[1].mInstance.mMaterial, 1u);
        }

        /// **One shared state set under two parents that name two textures is two materials**
        /// (`ChainKeys`). NifOsg puts an `NiNode`'s own texturing on that node's state set, and
        /// `SharedStateManager` makes equal state sets one object across files: here one quad
        /// whose own state set turns culling off stands under two parents, each painting its own
        /// texture. Keyed on the quad's state set alone, the first chain met decided both. And the
        /// same chains walked again resolve to the same two materials, so nothing arrives twice.
        TEST_F(RtxSceneExtractorTest, aSharedStateSetUnderTwoParentsWearsEachParentsTexture)
        {
            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            quad->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);

            osg::ref_ptr<osg::Group> root = new osg::Group;
            std::vector<osg::ref_ptr<const osg::StateSet>> painted;
            for (const char* const file : { "textures/tx_first.dds", "textures/tx_second.dds" })
            {
                osg::ref_ptr<osg::Group> parent = new osg::Group;
                paint(*parent->getOrCreateStateSet(), file);
                painted.emplace_back(parent->getStateSet());
                parent->addChild(quad);
                root->addChild(parent);
            }

            walk(*root, 0, 1);

            const auto materials = mScene.materials().getRows();
            const auto textures = mScene.textures().getRows();
            ASSERT_EQ(materials.size(), 2u);
            ASSERT_EQ(textures.size(), 2u);
            const auto placements = mScene.placements().getRows();
            ASSERT_EQ(placements.size(), 2u);
            for (std::size_t at = 0; at < 2; ++at)
            {
                const Material& worn = materials[placements[at].mInstance.mMaterial];
                EXPECT_TRUE(worn.mTwoSided) << "the quad's own state set is in force at " << at;
                ASSERT_NE(worn.mDiffuse, sNoIndex);
                EXPECT_EQ(textures[worn.mDiffuse].mPath,
                    VFS::Path::NormalizedView(at == 0 ? "textures/tx_first.dds" : "textures/tx_second.dds"))
                    << "placement " << at << " wears its own parent's texture";
            }

            mExtractor.retire();
            mScene.clearPlacement();
            const ExtractionStats again = walk(*root, 0, 2);
            EXPECT_EQ(again.mMaterialsAdded, 0u);
            EXPECT_EQ(again.mMaterialsReused, 2u);
            mExtractor.retire();

            // **And a key goes with the last material under it**: the graph let go of, the walk
            // meets nothing, and nothing but this test holds the parents' state sets, which the
            // pairs of the two chains held.
            root = nullptr;
            const osg::ref_ptr<osg::Group> empty = new osg::Group;
            mScene.clearPlacement();
            walk(*empty, 0, 3);
            EXPECT_EQ(mExtractor.retire().mMaterials, 2u);
            mExtractor.getReleased().clear();
            for (const osg::ref_ptr<const osg::StateSet>& parent : painted)
                EXPECT_EQ(parent->referenceCount(), 1) << "a key outlived its material";
        }

        /// A material a controller rewrites resolves its texture out of the image, not its name.
        ///
        /// **Every frame it is met, because that is what an animated material costs.** The state set
        /// is the same object and keeps its slot, and what is inside it is read again — so the
        /// texture is asked for again as well. Asking by path built a `VFS::Path::Normalized` off
        /// the heap each time, and Morrowind scrolls 432 surfaces in one Vivec cell.
        ///
        /// Three walks, because the entry has to survive a sweep and not only a frame: the second
        /// walk is where a cache would answer and the third is where a sweep that took the entry
        /// with it would show.
        TEST_F(RtxSceneExtractorTest, anAnimatedMaterialFindsOneTextureSlotEveryFrame)
        {
            osg::ref_ptr<osg::Image> banner = new osg::Image;
            banner->setFileName("textures/tx_banner.dds");

            osg::ref_ptr<ColourController> controller = new ColourController;
            controller->mDiffuse = banner;

            osg::ref_ptr<osg::Geometry> quad = makeQuad();
            osg::ref_ptr<osg::Group> node = new osg::Group;
            node->addChild(quad);
            node->addUpdateCallback(controller);

            osgUtil::UpdateVisitor update;
            for (unsigned int frame = 1; frame <= 3; ++frame)
            {
                update.setTraversalNumber(frame);
                node->accept(update);

                mScene.clearPlacement();

                const std::size_t before = Testing::getAllocationCount();
                walk(*node, 0, frame);
                const std::size_t spent = Testing::getAllocationCount() - before;

                // **What the first frame legitimately spends, and every frame after it must not.**
                // The first walk builds the scene: the tables, the identity entries and the one
                // string this texture's path is ever read from. A steady frame reads the same graph
                // and must reach the heap not at all.
                if (frame > 1)
                {
                    EXPECT_EQ(spent, 0u) << spent << " allocations on frame " << frame;
                }

                ASSERT_EQ(mScene.materials().getRows().size(), 1u) << "on frame " << frame;
                EXPECT_TRUE(mScene.materials().getRows()[0].mAnimated);
                EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, 0u) << "the same slot, on frame " << frame;
                EXPECT_EQ(mScene.textures().getRows().size(), 1u) << "a second slot arrived on frame " << frame;

                mExtractor.retire();
            }

            // **And the slot goes when the surface does.** The walk holds a reference of its own so
            // that a slot it names cannot be handed out under it, and a hold nothing gives back is a
            // texture the scene keeps for the rest of the run.
            node->removeChild(quad);
            mScene.clearPlacement();
            walk(*node, 0, 4);
            mExtractor.retire();

            EXPECT_FALSE(mScene.textures().isLive(0)) << "the walk's own hold outlived the surface";
        }

        /// **A texture a full table refused is not asked again until the table frees a slot.** An
        /// animated material asks for its images every frame, and each refusal built the image's
        /// path off the heap only to be refused again.
        ///
        /// Three walks against the full table, one beside a clamped surface, then one after a slot is
        /// freed: the second and third are where a refusal asked again would spend and count, and
        /// the fifth is where a remembered refusal that outlived the room it lacked would leave the
        /// surface untextured.
        ///
        /// **The refusal is the wrap's and not the file's.** Slot 5 holds the banner clamped, which
        /// the full table answers all the same, so a clamped surface wearing the banner image the
        /// animated one was refused under a repeat still finds it.
        TEST_F(RtxSceneExtractorTest, aRefusedTextureIsAskedAgainOnlyOnceASlotIsFreed)
        {
            TextureTable& textures = mScene.textures();
            Testing::SceneHolds holds(mScene);
            constexpr Index clampedSlot = 5;
            for (std::size_t at = 0; at < TextureTable::sCapacity; ++at)
            {
                const Index slot = at == clampedSlot
                    ? textures.add(VFS::Path::NormalizedView("textures/tx_banner.dds"), TextureWrap::Clamp)
                    : textures.add(VFS::Path::Normalized("textures/tx_" + std::to_string(at) + ".dds"));
                ASSERT_EQ(slot, at);
                holds.texture(slot);
            }

            osg::ref_ptr<osg::Image> banner = new osg::Image;
            banner->setFileName("textures/tx_banner.dds");

            osg::ref_ptr<ColourController> controller = new ColourController;
            controller->mDiffuse = banner;

            osg::ref_ptr<osg::Group> node = new osg::Group;
            node->addChild(makeQuad());
            node->addUpdateCallback(controller);

            osg::ref_ptr<osg::Texture2D> clamp = new osg::Texture2D(banner);
            clamp->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            clamp->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            osg::ref_ptr<osg::Geometry> clamped = makeQuad();
            clamped->getOrCreateStateSet()->setTextureAttributeAndModes(0, clamp, osg::StateAttribute::ON);
            clamped->getOrCreateStateSet()->setTextureAttribute(0,
                new SceneUtil::TextureType(std::string(sTextureRoleNames.name(TextureRole::Diffuse))),
                osg::StateAttribute::ON);

            // Under one parent from the first frame, so the animated surface is one placement
            // throughout and the clamped one joins it beside.
            osg::ref_ptr<osg::Group> both = new osg::Group;
            both->addChild(node);

            osgUtil::UpdateVisitor update;
            for (unsigned int frame = 1; frame <= 3; ++frame)
            {
                update.setTraversalNumber(frame);
                both->accept(update);
                mScene.clearPlacement();

                const std::size_t before = Testing::getAllocationCount();
                walk(*both, 0, frame);
                const std::size_t spent = Testing::getAllocationCount() - before;

                if (frame > 1)
                {
                    EXPECT_EQ(spent, 0u) << spent << " allocations on frame " << frame;
                }

                EXPECT_EQ(textures.getRefused(), 1u) << "asked again on frame " << frame;
                ASSERT_EQ(mScene.materials().getRows().size(), 1u);
                EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, sNoIndex);

                mExtractor.retire();
            }

            both->addChild(clamped);

            update.setTraversalNumber(4);
            both->accept(update);
            mScene.clearPlacement();
            walk(*both, 0, 4);
            mExtractor.retire();

            EXPECT_EQ(textures.getRefused(), 1u);
            ASSERT_EQ(mScene.materials().getRows().size(), 2u);
            EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, sNoIndex);
            EXPECT_EQ(mScene.materials().getRows()[1].mDiffuse, clampedSlot) << "refused for another wrap's want";

            holds.dropTexture(9);

            update.setTraversalNumber(5);
            both->accept(update);
            mScene.clearPlacement();
            walk(*both, 0, 5);

            EXPECT_EQ(textures.getRefused(), 1u);
            ASSERT_EQ(mScene.materials().getRows().size(), 2u);
            EXPECT_EQ(mScene.materials().getRows()[0].mDiffuse, 9u) << "the freed slot is the refused texture's";
        }

        /// **A material over an image that stands names it without a string.** The image is taken by
        /// its path, normalised into a scratch the thread keeps, where a `VFS::Path::Normalized`
        /// apiece was a string at every material — a path past the short string's room, as every
        /// texture's is. Measured where nothing else of an arrival reaches the heap: a material that
        /// came and went before it left the room the third takes over.
        TEST_F(RtxSceneExtractorTest, aMaterialOverAStandingImageAllocatesNoPath)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_a_path_past_the_short_string.dds");
            const auto paintedQuad = [&](osg::StateAttribute::GLModeValue culled) {
                osg::ref_ptr<osg::Geometry> quad = makeQuad();
                paint(*quad->getOrCreateStateSet(), *image);
                quad->getStateSet()->setMode(GL_CULL_FACE, culled);
                return quad;
            };

            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(paintedQuad(osg::StateAttribute::ON));
            const auto frame = [&](std::size_t number) {
                mScene.clearPlacement();
                const std::size_t before = Testing::getAllocationCount();
                walk(*root, 0, number);
                const std::size_t spent = Testing::getAllocationCount() - before;
                mExtractor.retire();
                mExtractor.getReleased().clear();
                return spent;
            };
            frame(1);

            const osg::ref_ptr<osg::Geometry> passing = paintedQuad(osg::StateAttribute::OFF);
            root->addChild(passing);
            frame(2);
            root->removeChild(passing);
            frame(3);

            root->addChild(paintedQuad(osg::StateAttribute::OFF));
            const std::size_t spent = frame(4);
            EXPECT_EQ(mScene.materials().getLiveCount(), 2u);
            EXPECT_EQ(spent, 0u) << spent << " allocations for a material over a standing image";
        }

        /// The surface is read from its controller every frame, and from whichever controller the
        /// node carries now.
        ///
        /// **Which controller a node hangs off is a fact about this frame and not about the entry.**
        /// The walk holds a state set per animated node so that the address is the same one next
        /// frame, and it would be easy to hold the controller that wrote into it beside that — but
        /// content is free to add or remove one, and a surface would then go on being animated by a
        /// controller the graph no longer has.
        ///
        /// Three different reds, so each walk has one answer and no other.
        TEST_F(RtxSceneExtractorTest, aSurfaceIsReadEachFrameAndFollowsAControllerSwappedUnderTheWalk)
        {
            osg::ref_ptr<ColourController> first = new ColourController;
            first->mRed = 0.25f;

            osg::ref_ptr<osg::Group> node = new osg::Group;
            node->addChild(makeQuad());
            node->addUpdateCallback(first);

            walk(*node, 0, 1);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.0508761f);

            // What the entry holds is the state set and never what was written into it, so a second
            // walk reads the surface again.
            first->mRed = 0.5f;
            mScene.clearPlacement();
            walk(*node, 0, 2);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u) << "the same surface is the same slot";
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.2140411f);

            // And the chain changes under it: the controller swapped under the walk is not the one
            // that painted the surface.
            osg::ref_ptr<ColourController> second = new ColourController;
            second->mRed = 0.75f;
            node->removeUpdateCallback(first);
            node->addUpdateCallback(second);

            mScene.clearPlacement();
            walk(*node, 0, 3);

            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.5225216f);

            // **A controller moved from one chain to the other is a chain changed**, though the
            // same callbacks stand in the same order read cull first: a cull chain `first→second`
            // applies `second` last, and `second` on the update chain beside `first` on the cull
            // chain applies `first` last, as the rasterizer runs them.
            first->mRed = 0.25f;
            node->removeUpdateCallback(second);
            node->addCullCallback(first);
            node->addCullCallback(second);
            mScene.clearPlacement();
            walk(*node, 0, 4);
            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.5225216f);

            node->removeCullCallback(second);
            node->addUpdateCallback(second);
            mScene.clearPlacement();
            walk(*node, 0, 5);
            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.0508761f);

            // **A controller the game frees, and the next one made where it stood.** The entry
            // holds what it found, so the freed one's address is not the next one's while the entry
            // could take the new for the old, and lets it go once a walk finds the chains changed.
            const osg::observer_ptr<ColourController> freed = first.get();
            node->removeCullCallback(first);
            first = nullptr;
            EXPECT_TRUE(freed.valid()) << "a controller the walk found was freed under its entry";

            osg::ref_ptr<ColourController> third = new ColourController;
            third->mRed = 0.5f;
            node->addCullCallback(third);
            mScene.clearPlacement();
            walk(*node, 0, 6);
            ASSERT_EQ(mScene.materials().getRows().size(), 1u);
            expectRed(mScene.materials().getRows()[0].mDiffuseColour, 0.2140411f);
            EXPECT_FALSE(freed.valid()) << "the entry kept a controller the chains no longer hold";
        }
    }
}
