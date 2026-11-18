#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/BoundingBox>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/rtxtool/instruments/digest.hpp>
#include <apps/rtxtool/instruments/scenedigest.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/deformertable.hpp>
#include <components/rtx/scene/instancerecord.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/shaders/skinning.h>
#include <components/vfs/pathutil.hpp>

namespace RtxTool
{
    namespace
    {
        /// An eight-cornered box, mirrored in x where `mirrored`, which is the pair of sibling shapes
        /// the host hands over in heap order. `shuffled` stores the same vertices in reverse and
        /// spells the same triangles from another corner, which is what the host's geometry merge
        /// does to a shape. `tangents` is none or one per corner, and is not shuffled.
        Rtx::Index addBox(Rtx::SceneDesc& scene, const bool mirrored, const float lift = 0.0f,
            const bool shuffled = false, std::span<const osg::Vec4f> tangents = {})
        {
            std::vector<osg::Vec3f> positions;
            std::vector<osg::Vec3f> normals;
            std::vector<osg::Vec2f> texCoords;
            for (std::int32_t corner = 0; corner < 8; ++corner)
            {
                const float x = (corner & 1) != 0 ? 143.0f : 124.0f;
                positions.emplace_back(
                    mirrored ? -x : x, (corner & 2) != 0 ? 1.0f : 0.0f, ((corner & 4) != 0 ? 1.0f : 0.0f) + lift);
                normals.emplace_back(0.0f, 0.0f, 1.0f);
                texCoords.emplace_back(static_cast<float>(corner), 0.0f);
            }

            std::vector<std::uint32_t> indices{ 0, 1, 2, 2, 1, 3 };
            if (shuffled)
            {
                std::reverse(positions.begin(), positions.end());
                std::reverse(texCoords.begin(), texCoords.end());
                indices = { 7, 6, 5, 5, 6, 4 };
            }

            return Rtx::Testing::addMesh(scene,
                Rtx::MeshArrays{ .mPositions = positions,
                    .mNormals = normals,
                    .mTexCoords = texCoords,
                    .mTangents = tangents,
                    .mIndices = indices },
                {});
        }

        /// The two boxes under one placement, added in the order given, and one lamp.
        void fillBoxes(Rtx::SceneDesc& scene, const bool mirroredFirst, const float lift, const float aside,
            const bool shuffled = false)
        {
            Rtx::Material material;
            material.mDiffuse = scene.textures().add(VFS::Path::NormalizedView("textures/box.dds"));
            const Rtx::Index wearing = scene.addMaterial(material);

            const osg::Matrixf stood = osg::Matrixf::translate(10.0f, 20.0f, 30.0f);
            for (std::int32_t which = 0; which < 2; ++which)
            {
                const bool mirrored = (which == 0) == mirroredFirst;
                Rtx::MeshInstance instance;
                instance.mTransform = mirrored ? osg::Matrixf::translate(aside, 0.0f, 0.0f) * stood : stood;
                instance.mMesh = addBox(scene, mirrored, mirrored ? lift : 0.0f, shuffled);
                instance.mMaterial = wearing;
                scene.addInstance(instance);
            }

            Rtx::Light lamp{};
            lamp.mPosition = osg::Vec3f(1.0f, 2.0f, 3.0f);
            lamp.mIntensity = osg::Vec3f(4.0f, 5.0f, 6.0f);
            scene.addLight(lamp);
        }

        std::string digestOfBoxes(
            const bool mirroredFirst, const float lift, const float aside, const bool shuffled = false)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            fillBoxes(scene, mirroredFirst, lift, aside, shuffled);
            return spellHash(digestScene(digestParts(scene)));
        }

        std::string layoutOfBoxes(
            const bool mirroredFirst, const float lift, const float aside, const bool shuffled = false)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            fillBoxes(scene, mirroredFirst, lift, aside, shuffled);
            return spellHash(digestLayout(digestParts(scene)));
        }

        /// **Two siblings swapped is one scene, a shape stored in another order is one scene, and
        /// a box moved or reshaped is another.** The first two are what the host's optimizer does
        /// between one process and the next; the digest has to be blind to them and to nothing else.
        TEST(RtxSceneDigestTest, storageOrderIsNotAChangeAndAMovedBoxIs)
        {
            const std::string one = digestOfBoxes(false, 0.0f, 0.0f);
            EXPECT_EQ(one, digestOfBoxes(true, 0.0f, 0.0f)) << "siblings swapped";
            EXPECT_EQ(one, digestOfBoxes(false, 0.0f, 0.0f, true)) << "vertices stored in another order";

            EXPECT_NE(one, digestOfBoxes(false, 1.0f, 0.0f)) << "a vertex moved is a change";
            EXPECT_NE(one, digestOfBoxes(false, 0.0f, 1.0f)) << "a placement moved is a change";
            EXPECT_NE(digestOfBoxes(false, 1.0f, 0.0f), digestOfBoxes(false, 0.0f, 1.0f));
        }

        /// **The layout digest answers the two questions the other one refuses**, which is the whole
        /// of why there are two. A structure is built over the index buffer as written, so a scene
        /// stored two ways is two scenes to a ray tracer even where it is one cell to a reader.
        TEST(RtxSceneDigestTest, layoutSeesStorageOrderAndEverythingTheOtherDoes)
        {
            const std::string one = layoutOfBoxes(false, 0.0f, 0.0f);
            EXPECT_EQ(one, layoutOfBoxes(false, 0.0f, 0.0f)) << "one scene built twice";

            EXPECT_NE(one, layoutOfBoxes(true, 0.0f, 0.0f)) << "siblings swapped";
            EXPECT_NE(one, layoutOfBoxes(false, 0.0f, 0.0f, true)) << "vertices stored in another order";
            EXPECT_NE(one, layoutOfBoxes(false, 1.0f, 0.0f)) << "a vertex moved";
            EXPECT_NE(one, layoutOfBoxes(false, 0.0f, 1.0f)) << "a placement moved";
        }

        /// **Where the pair disagrees is the fault neither could name alone.** A run whose scene
        /// digest holds while its layout digest moves has been handed one cell stored two ways, and
        /// that is what a report has to be able to say.
        TEST(RtxSceneDigestTest, storageOrderIsWhereTheTwoDigestsPartCompany)
        {
            EXPECT_EQ(digestOfBoxes(false, 0.0f, 0.0f), digestOfBoxes(false, 0.0f, 0.0f, true));
            EXPECT_NE(layoutOfBoxes(false, 0.0f, 0.0f), layoutOfBoxes(false, 0.0f, 0.0f, true));
        }

        /// The four maps a mod's surface binds beside the diffuse, each the diffuse's own texture.
        void wearModMaps(Rtx::Material& material)
        {
            material.mDetail = material.mDiffuse;
            material.mDecal = material.mDiffuse;
            material.mBump = material.mDiffuse;
            material.mGloss = material.mDiffuse;
        }

        /// One box under a material `change` has been applied to, digested both ways.
        std::pair<std::string, std::string> digestsOfMaterial(void (*change)(Rtx::Material&))
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();

            Rtx::Material material;
            material.mDiffuse = scene.textures().add(VFS::Path::NormalizedView("textures/box.dds"));
            change(material);

            Rtx::MeshInstance instance;
            instance.mMaterial = scene.addMaterial(material);
            instance.mMesh = addBox(scene, false);
            scene.addInstance(instance);

            return { spellHash(digestScene(digestParts(scene))), spellHash(digestLayout(digestParts(scene))) };
        }

        /// **Every field of a material reaches both digests**, which is what one field list buys.
        ///
        /// The three flags below are the ones a reader would least expect to matter, and each does:
        /// two runs that disagreed about whether a chunk was flattening would otherwise have held
        /// one scene as far as the digest could say.
        TEST(RtxSceneDigestTest, everyMaterialFieldReachesBothDigests)
        {
            const auto [scene, layout] = digestsOfMaterial([](Rtx::Material&) {});

            for (const auto& [what, change] : std::initializer_list<std::pair<const char*, void (*)(Rtx::Material&)>>{
                     { "flatten", [](Rtx::Material& m) { m.mFlatten = Rtx::GroundFlattening::Every; } },
                     { "animated", [](Rtx::Material& m) { m.mAnimated = true; } },
                     { "never solid", [](Rtx::Material& m) { m.mDiffuseNeverSolid = true; } },
                     { "relief", [](Rtx::Material& m) { m.mRelief = Rtx::AlphaRange{}; } },
                     { "detail", [](Rtx::Material& m) { m.mDetail = m.mDiffuse; } },
                     { "decal", [](Rtx::Material& m) { m.mDecal = m.mDiffuse; } },
                     { "bump", [](Rtx::Material& m) { m.mBump = m.mDiffuse; } },
                     { "gloss", [](Rtx::Material& m) { m.mGloss = m.mDiffuse; } },
                 })
            {
                const auto [movedScene, movedLayout] = digestsOfMaterial(change);
                EXPECT_NE(scene, movedScene) << what;
                EXPECT_NE(layout, movedLayout) << what;
            }

            // And the heights a relief spans, apart from whether there is one.
            const auto [whole, wholeLayout]
                = digestsOfMaterial([](Rtx::Material& m) { m.mRelief = Rtx::AlphaRange{}; });
            const auto [spanned, spannedLayout]
                = digestsOfMaterial([](Rtx::Material& m) { m.mRelief = Rtx::AlphaRange{ .mLow = 21, .mHigh = 175 }; });
            EXPECT_NE(whole, spanned) << "the relief's heights";
            EXPECT_NE(wholeLayout, spannedLayout) << "the relief's heights";

            // And what each of a mod's maps carries beside itself, apart from whether there is one: each against the
            // same four maps without it.
            const auto [mapped, mappedLayout] = digestsOfMaterial(wearModMaps);
            for (const auto& [what, change] : std::initializer_list<std::pair<const char*, void (*)(Rtx::Material&)>>{
                     { "the detail map's unit",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mDetailUnit = 1;
                         } },
                     { "the decal's unit",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mDecalUnit = 1;
                         } },
                     { "the bump map's unit",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mBumpUnit = 1;
                         } },
                     { "the bump matrix",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mBumpMatrix = osg::Vec4f(0.0f, 0.0f, 0.25f, 0.0f);
                         } },
                     { "the bump luma",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mBumpLuma = osg::Vec2f(0.0f, 1.0f);
                         } },
                     { "the gloss map's unit",
                         [](Rtx::Material& m) {
                             wearModMaps(m);
                             m.mGlossUnit = 1;
                         } },
                 })
            {
                const auto [movedScene, movedLayout] = digestsOfMaterial(change);
                EXPECT_NE(mapped, movedScene) << what;
                EXPECT_NE(mappedLayout, movedLayout) << what;
            }
        }

        /// One box under one placement, its corners carrying `tangents`, digested whole and part by
        /// part.
        std::pair<std::string, ScenePartDigests> digestsOfTangents(std::span<const osg::Vec4f> tangents)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            Rtx::MeshInstance instance;
            instance.mMesh = addBox(scene, false, 0.0f, false, tangents);
            scene.addInstance(instance);
            return { spellHash(digestScene(digestParts(scene))), digestParts(scene) };
        }

        /// **A tangent moves the normals column, the meshes column and the scene, and no tangent
        /// moves nothing.** The words are hashed only where a vertex has one and the mesh row's
        /// claim to them only where it is made, so a box whose tangents all packed to none digests
        /// as the box that brought none — which is every vanilla scene, and so the baselines on
        /// record hold.
        TEST(RtxSceneDigestTest, aTangentMovesTheNormalsAndMeshesColumnsAndNoTangentMovesNothing)
        {
            const auto [bare, bareParts] = digestsOfTangents({});

            std::array<osg::Vec4f, 8> none{};
            const auto [nothing, nothingParts] = digestsOfTangents(none);
            EXPECT_EQ(nothing, bare) << "tangents of no length moved the scene";
            EXPECT_EQ(nothingParts, bareParts) << "tangents of no length moved a column";

            std::array<osg::Vec4f, 8> alongX;
            alongX.fill(osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f));
            const auto [tangent, tangentParts] = digestsOfTangents(alongX);
            EXPECT_NE(tangent, bare);
            for (std::size_t at = 0; at < bareParts.size(); ++at)
            {
                const auto part = static_cast<ScenePart>(at);
                EXPECT_EQ(tangentParts[at] != bareParts[at],
                    part == ScenePart::Normals || part == ScenePart::Meshes || part == ScenePart::Content)
                    << nameOf(part);
            }

            // The handedness alone is a change as well.
            std::array<osg::Vec4f, 8> flipped;
            flipped.fill(osg::Vec4f(1.0f, 0.0f, 0.0f, -1.0f));
            const auto [other, otherParts] = digestsOfTangents(flipped);
            EXPECT_NE(other, tangent);
            EXPECT_NE(otherParts[static_cast<std::size_t>(ScenePart::Normals)],
                tangentParts[static_cast<std::size_t>(ScenePart::Normals)]);
        }

        /// One sprite under one emitter that adds where `additive` and falls where `falls`,
        /// digested part by part.
        ScenePartDigests partsOfPlume(const bool additive, const bool falls)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            const Rtx::Index texture = scene.textures().add(VFS::Path::NormalizedView("textures/puff.dds"));
            const std::array sprites{ Rtx::Sprite{ .mPosition = osg::Vec3f(1.0f, 2.0f, 3.0f),
                .mRadius = 4.0f,
                .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                .mAlpha = 1.0f } };
            scene.addEmitter(sprites,
                Rtx::EmitterSpec{ .mTexture = texture,
                    .mBlend = additive ? Rtx::BlendKind::Add : Rtx::BlendKind::Over,
                    .mFalls = falls });

            return digestParts(scene);
        }

        /// **An emitter's flag word moves the emitters column and the content.** The table is hashed whole,
        /// so a bit that changed what the trace does with a plume — whether it adds, whether a roof
        /// keeps it off — has to be a change the report can see, and a sprite's own row names its
        /// emitter, which is the sprites column's to see.
        TEST(RtxSceneDigestTest, anEmittersFlagsMoveTheEmittersColumn)
        {
            const ScenePartDigests smoke = partsOfPlume(false, false);
            EXPECT_EQ(smoke, partsOfPlume(false, false)) << "one plume built twice";

            const ScenePartDigests flame = partsOfPlume(true, false);
            const ScenePartDigests rain = partsOfPlume(false, true);
            for (std::size_t at = 0; at < smoke.size(); ++at)
            {
                const auto part = static_cast<ScenePart>(at);
                const bool moves = part == ScenePart::Emitters || part == ScenePart::Content;
                EXPECT_EQ(smoke[at] != flame[at], moves) << nameOf(part) << " under a flame";
                EXPECT_EQ(smoke[at] != rain[at], moves) << nameOf(part) << " under rain";
            }
        }

        /// One quad on a one-bone rig whose single influence weighs `weight`, posed with its bone
        /// `up` units high, digested part by part.
        ScenePartDigests partsOfSkin(const float weight, const float up)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();

            const std::array<std::uint32_t, 4> runs{ 1u, 1u, 1u, 1u };
            const std::array influences{ Rtx::Shaders::GpuInfluence{ .mBone = 0, .mWeight = weight } };

            const std::array positions{ osg::Vec3f(), osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f) };
            const std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };
            const Rtx::Index quad
                = Rtx::Testing::addMesh(scene, Rtx::MeshArrays{ .mPositions = positions, .mIndices = indices }, {},
                    Rtx::RigSpec{ .mRuns = runs, .mInfluences = influences, .mBones = 1 })
                      .mMesh;

            scene.addInstance(Rtx::MeshInstance{ .mMesh = quad });

            const std::array bones{ Rtx::toGpuBone(osg::Matrixf::translate(0.0f, 0.0f, up)) };
            std::vector<Rtx::PoseWord> words;
            Rtx::packRig(Rtx::toGpuBone(osg::Matrixf::identity()), bones, words);
            // One reach whatever the pose, because the reach is the mesh's own row and so the
            // meshes column's: what this asks is what the words move on their own.
            scene.pose(quad, words, osg::BoundingBoxf(osg::Vec3f(), osg::Vec3f(1.0f, 1.0f, 10.0f)));

            return digestParts(scene);
        }

        /// **A pose moves the poses column, and a skin the deformers column, and each the content**, as a
        /// placement of the mesh is posed by both. The two are one table and one buffer in the scene; a column
        /// apiece is what lets a pair say whether the actor moved or the actor changed.
        TEST(RtxSceneDigestTest, aPoseAndASkinEachMoveTheirOwnColumn)
        {
            const ScenePartDigests one = partsOfSkin(1.0f, 5.0f);
            EXPECT_EQ(one, partsOfSkin(1.0f, 5.0f)) << "one scene built twice";

            const ScenePartDigests posed = partsOfSkin(1.0f, 7.0f);
            const ScenePartDigests reskinned = partsOfSkin(0.5f, 5.0f);
            for (std::size_t at = 0; at < one.size(); ++at)
            {
                const auto part = static_cast<ScenePart>(at);
                EXPECT_EQ(one[at] != posed[at], part == ScenePart::Poses || part == ScenePart::Content)
                    << nameOf(part) << " under another pose";
                EXPECT_EQ(one[at] != reskinned[at], part == ScenePart::Deformers || part == ScenePart::Content)
                    << nameOf(part) << " under another skin";
            }
        }

        /// **Where a placement stood the frame before, and the water's ripples, are the content too**: a motion
        /// vector is the difference of the two places, and the ripple field is stepped by what disturbed it.
        /// One box placed at `there`, having moved there from the origin or jumped, with a ripple or none.
        std::string contentOfMotion(const bool jumped, const bool rippled)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            Rtx::MeshInstance instance;
            instance.mMesh = addBox(scene, false);
            const Rtx::Index slot = scene.addInstance(instance);
            const osg::Matrixf there = osg::Matrixf::translate(5.0f, 0.0f, 0.0f);
            if (jumped)
                scene.placements().jump(slot, there);
            else
                scene.placements().move(slot, there);
            if (rippled)
                scene.addRipple(Rtx::RippleImpulse{ .mAt = osg::Vec2f(1.0f, 2.0f), .mSize = 3.0f });
            return spellHash(digestScene(digestParts(scene)));
        }

        TEST(RtxSceneDigestTest, wherePlacementsStoodAndTheRipplesAreTheContent)
        {
            const std::string moved = contentOfMotion(false, false);
            EXPECT_EQ(moved, contentOfMotion(false, false)) << "one scene built twice";
            EXPECT_NE(moved, contentOfMotion(true, false))
                << "one place, stood at the frame before or moved from the origin";
            EXPECT_NE(moved, contentOfMotion(false, true)) << "a ripple";
        }

        /// One field of a fixture's placement, lamp or sprite set off its default.
        enum class Field
        {
            None,
            Clockwise,
            LampBody,
            Stander,
            SourceRadius,
            Clearance,
            Traits,
            Axis,
        };

        /// One box, one lamp and one rain streak under one emitter, with `field` set off its default, digested part
        /// by part.
        ScenePartDigests partsWith(const Field field)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            Rtx::MeshInstance instance;
            instance.mMesh = addBox(scene, false);
            instance.mClockwise = field == Field::Clockwise;
            instance.mLampBody = field == Field::LampBody;
            instance.mStander = field == Field::Stander ? Rtx::Stander::Ring : Rtx::Stander::Walk;
            scene.addInstance(instance);

            Rtx::Light lamp{};
            lamp.mPosition = osg::Vec3f(1.0f, 2.0f, 3.0f);
            lamp.mIntensity = osg::Vec3f(4.0f, 5.0f, 6.0f);
            lamp.mSourceRadius = field == Field::SourceRadius ? 2.0f : 0.0f;
            lamp.mClearance = field == Field::Clearance ? 2.0f : 0.0f;
            if (field == Field::Traits)
                lamp.mTraits = 0;
            scene.addLight(lamp);

            const Rtx::Index texture = scene.textures().add(VFS::Path::NormalizedView("textures/rain.dds"));
            const std::array sprites{ Rtx::Sprite{ .mPosition = osg::Vec3f(1.0f, 2.0f, 3.0f),
                .mRadius = 4.0f,
                .mAxis = osg::Vec3f(0.0f, 0.0f, field == Field::Axis ? -2.0f : -1.0f),
                .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                .mAlpha = 1.0f } };
            scene.addEmitter(
                sprites, Rtx::EmitterSpec{ .mTexture = texture, .mBlend = Rtx::BlendKind::Over, .mWidth = 0.1f });

            return digestParts(scene);
        }

        /// **Every field a frame draws by is the content, and who stood a placement is not**: a mirrored face, a
        /// lantern's paper, a lamp's flame, its shadow's clearance and the classes it lights, and a streak's length
        /// each move it and the layout column that holds the field; the stander moves the instances column alone.
        TEST(RtxSceneDigestTest, everyFieldAFrameDrawsByIsTheContent)
        {
            const ScenePartDigests plain = partsWith(Field::None);
            EXPECT_EQ(plain, partsWith(Field::None)) << "one scene built twice";

            // The emitter's reach holds its sprites' rims, so a longer streak moves the emitters column too.
            const std::array cases{
                std::pair{ Field::Clockwise, std::vector{ ScenePart::Instances } },
                std::pair{ Field::LampBody, std::vector{ ScenePart::Instances } },
                std::pair{ Field::SourceRadius, std::vector{ ScenePart::Lights } },
                std::pair{ Field::Clearance, std::vector{ ScenePart::Lights } },
                std::pair{ Field::Traits, std::vector{ ScenePart::Lights } },
                std::pair{ Field::Axis, std::vector{ ScenePart::Sprites, ScenePart::Emitters } },
            };
            for (const auto& [field, columns] : cases)
            {
                const ScenePartDigests changed = partsWith(field);
                for (std::size_t at = 0; at < plain.size(); ++at)
                {
                    const auto part = static_cast<ScenePart>(at);
                    EXPECT_EQ(plain[at] != changed[at],
                        part == ScenePart::Content || std::ranges::find(columns, part) != columns.end())
                        << nameOf(part) << " under field " << static_cast<std::int32_t>(field);
                }
            }

            const ScenePartDigests stood = partsWith(Field::Stander);
            for (std::size_t at = 0; at < plain.size(); ++at)
            {
                const auto part = static_cast<ScenePart>(at);
                EXPECT_EQ(plain[at] != stood[at], part == ScenePart::Instances) << nameOf(part) << " under the ring";
            }
        }

        /// **The geometry's parts are each mesh's digests from its arrival**, so the host letting the geometry go after
        /// a hand-over moves none of them, and a digester answers what a digest made from nothing does. A placement
        /// moved moves the instances alone; a mesh added, let go of or landing in the hole it left moves the positions.
        TEST(RtxSceneDigestTest, theGeometrysPartsOutliveTheArrivalsTheyWereMadeFrom)
        {
            Rtx::SceneIdentities identities;
            Rtx::SceneDesc scene(identities);
            scene.keepDigests();
            fillBoxes(scene, false, 0.0f, 0.0f);

            SceneDigester digester;
            const ScenePartDigests first = digester.digest(scene);
            EXPECT_EQ(first, digestParts(scene)) << "a digester and a digest from nothing disagree";

            scene.clearArrivals();
            EXPECT_EQ(digester.digest(scene), first) << "a part read the geometry the host let go of";

            scene.placements().move(0, osg::Matrixf::translate(1.0f, 0.0f, 0.0f));
            const ScenePartDigests moved = digester.digest(scene);
            EXPECT_EQ(moved[static_cast<std::size_t>(ScenePart::Positions)],
                first[static_cast<std::size_t>(ScenePart::Positions)]);
            EXPECT_NE(moved[static_cast<std::size_t>(ScenePart::Instances)],
                first[static_cast<std::size_t>(ScenePart::Instances)]);

            const Rtx::Index third = addBox(scene, true, 2.0f);
            const ScenePartDigests grown = digester.digest(scene);
            EXPECT_NE(grown[static_cast<std::size_t>(ScenePart::Positions)],
                moved[static_cast<std::size_t>(ScenePart::Positions)]);
            EXPECT_EQ(grown, digestParts(scene));

            scene.drop(scene.holdMesh(third));
            const ScenePartDigests released = digester.digest(scene);
            EXPECT_EQ(released[static_cast<std::size_t>(ScenePart::Positions)],
                moved[static_cast<std::size_t>(ScenePart::Positions)])
                << "a slot given back still counted in the positions";
            EXPECT_EQ(released, digestParts(scene));

            EXPECT_EQ(addBox(scene, false, 3.0f), third) << "the freed slot is the one handed out";
            const ScenePartDigests refilled = digester.digest(scene);
            EXPECT_NE(refilled[static_cast<std::size_t>(ScenePart::Positions)],
                released[static_cast<std::size_t>(ScenePart::Positions)]);
            EXPECT_NE(refilled[static_cast<std::size_t>(ScenePart::Positions)],
                grown[static_cast<std::size_t>(ScenePart::Positions)]);
            EXPECT_EQ(refilled, digestParts(scene));
        }
    }
}
