#include <limits>

#include <gtest/gtest.h>

#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/image.hpp>
#include <components/rtx/scene/levelasks.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/materialtable.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/textureasks.hpp>
#include <components/rtx/scene/texturetable.hpp>

namespace Rtx
{
    namespace
    {
        /// **The finest ask stands until it is taken back**, whatever stands beside it: two at 3, one at 5, the
        /// finest 3 until both at 3 are taken back, then 5, then none.
        TEST(RtxTextureAsksTest, theFinestAskStandsUntilItIsTakenBack)
        {
            LevelAsks<8> asks;
            EXPECT_FALSE(asks.finest().has_value());

            asks.ask(5);
            asks.ask(3);
            asks.ask(3);
            EXPECT_EQ(asks.finest(), 3u);

            asks.unask(3);
            EXPECT_EQ(asks.finest(), 3u) << "one ask at 3 still stands";
            asks.unask(3);
            EXPECT_EQ(asks.finest(), 5u);
            asks.unask(5);
            EXPECT_TRUE(asks.empty());
        }

        /// **A footprint is rounded to the finer step at each end.** The scale starts at -32: -31.5 rounds down to
        /// -32, step 0, and -31 is step 1; nought is step 32; past the coarsest, 15, every footprint is step 47. Minus
        /// infinity and not a number are the finest.
        TEST(RtxTextureAsksTest, aFootprintIsRoundedToTheFinerStep)
        {
            EXPECT_EQ(footprintStep(-std::numeric_limits<float>::infinity()), 0u);
            EXPECT_EQ(footprintStep(std::numeric_limits<float>::quiet_NaN()), 0u);
            EXPECT_EQ(footprintStep(-40.0f), 0u);
            EXPECT_EQ(footprintStep(-31.5f), 0u);
            EXPECT_EQ(footprintStep(-31.0f), 1u);
            EXPECT_EQ(footprintStep(-0.5f), 31u);
            EXPECT_EQ(footprintStep(0.0f), 32u);
            EXPECT_EQ(footprintStep(15.0f), 47u);
            EXPECT_EQ(footprintStep(100.0f), 47u);
            EXPECT_EQ(footprintStep(std::numeric_limits<float>::infinity()), 47u);
        }

        /// **The level is the step plus the texture's own offset, rounded down and held to its ceiling.** Step 30,
        /// footprint -2, of a 1024-square texture, offset 10: level 8; at an offset of 10.9 still 8. Step 22, -10:
        /// level 0. Step 45, 13, plus 10 is past the fifteenth, held there, and to a ceiling of 5 where one is given.
        /// An offset of minus infinity, a texture read by direction, is level 0 at any step.
        TEST(RtxTextureAsksTest, theLevelIsTheStepPlusTheTexturesOffset)
        {
            EXPECT_EQ(levelAsked(30, 10.0f), 8u);
            EXPECT_EQ(levelAsked(30, 10.9f), 8u);
            EXPECT_EQ(levelAsked(22, 10.0f), 0u);
            EXPECT_EQ(levelAsked(45, 10.0f), 15u);
            EXPECT_EQ(levelAsked(45, 10.0f, 5), 5u);
            EXPECT_EQ(levelAsked(47, -std::numeric_limits<float>::infinity()), 0u);
        }

        /// **A texture is wanted from the finest level its readers ask**, each reader's ask moving with it: a
        /// material asked at a footprint asks each texture it names at that footprint plus the texture's own
        /// offset; a placement the walk stands asks the finest; a material rewritten moves its asks to what it names
        /// now; a holder that reads a texture itself asks level 0; and a texture nothing asks is wanted whole.
        TEST(RtxTextureAsksTest, aTextureIsWantedFromTheFinestLevelItsReadersAsk)
        {
            SceneDesc scene;
            TextureTable& textures = scene.textures();

            // 1024 square: an offset of 0.5 log2(1024 × 1024) = 10.
            const osg::ref_ptr<osg::Image> large = Testing::makeImage("textures/large.dds", 1024, 1024);
            const osg::ref_ptr<osg::Image> other = Testing::makeImage("textures/other.dds", 1024, 1024);
            const Index first = textures.add(VFS::Path::NormalizedView("textures/large.dds"), large.get());
            const Index second = textures.add(VFS::Path::NormalizedView("textures/other.dds"), other.get());
            const Index sheet = textures.add(VFS::Path::NormalizedView("textures/sheet.dds"), large.get());

            // Held, as the identity a walk or the ring met it under holds it while it asks.
            const Index material = scene.addMaterial(Material{ .mDiffuse = first });
            MaterialHold held = scene.holdMaterial(material);
            EXPECT_EQ(textures.getWant(first), 0u) << "nothing asks it, so it is wanted whole";

            // Step 34, footprint 2: level 12. Step 30, footprint -2, beside it: 8, the finer, stands.
            scene.askMaterial(material, 34);
            EXPECT_EQ(textures.getWant(first), 12u);
            scene.askMaterial(material, 30);
            EXPECT_EQ(textures.getWant(first), 8u);
            scene.unaskMaterial(material, 30);
            EXPECT_EQ(textures.getWant(first), 12u);

            // A placement the walk stands wearing it asks the finest, and taking it out gives the ring's back.
            const Index mesh = Testing::addQuadMesh(scene);
            const Index placed = scene.addInstance(MeshInstance{ .mMesh = mesh, .mMaterial = material });
            EXPECT_EQ(textures.getWant(first), 0u);
            scene.dropInstance(placed, Stander::Walk);
            EXPECT_EQ(textures.getWant(first), 12u);

            // A transform that lays twice the texture each way stretches its area by 4: an offset of 11, level 13.
            Material stretched = scene.materials().getRows()[material];
            stretched.mTextureTransform = osg::Vec4f(2.0f, 2.0f, 0.0f, 0.0f);
            scene.setMaterial(material, stretched);
            EXPECT_EQ(textures.getWant(first), 13u);

            // Rewritten to name the other texture: the asks go with what it names.
            Material moved = stretched;
            moved.mDiffuse = second;
            scene.setMaterial(material, moved);
            EXPECT_EQ(textures.getWant(second), 13u);
            EXPECT_EQ(textures.getWant(first), 0u) << "asked by nothing now";
            EXPECT_TRUE(textures.getRows()[first].mAsks.empty());

            // A sheet read by direction asks level 0 at any footprint.
            Material shining = moved;
            shining.mEnvironment = sheet;
            scene.setMaterial(material, shining);
            EXPECT_EQ(textures.getWant(sheet), 0u);
            EXPECT_FALSE(textures.getRows()[sheet].mAsks.empty()) << "asked, and at level 0";

            // A holder that reads a texture itself asks level 0, and gives it back with its hold.
            ReadingHold reading = scene.holdReading(second);
            EXPECT_EQ(textures.getWant(second), 0u);
            scene.drop(std::move(reading));
            EXPECT_EQ(textures.getWant(second), 13u);

            scene.unaskMaterial(material, 34);
            EXPECT_TRUE(scene.materials().getAsks(material).empty());
            EXPECT_TRUE(textures.getRows()[second].mAsks.empty());
            EXPECT_TRUE(textures.getRows()[sheet].mAsks.empty());
            scene.drop(std::move(held));
        }

        /// **The meter splits what a texture stood whole by its chain's own levels.** A 1024-square RGBA file's chain
        /// to one texel is 4 × (1024² + 512² + … + 1) = 4 × 1398101 = 5592404 bytes, of which level 0 is 4194304.
        /// Asked at step 23, footprint -9, it is wanted from level floor(-9 + 10) = 1: level 0's bytes stand above
        /// it. A texture nothing asks is wanted whole and counts only in what stood; one not stood whole counts in
        /// neither. One that rests counts against the level its last readers asked.
        TEST(RtxTextureAsksTest, theMeterSplitsWhatATextureStoodByItsChainsLevels)
        {
            SceneDesc scene;
            TextureTable& textures = scene.textures();
            const osg::ref_ptr<osg::Image> image = Testing::makeImage("textures/large.dds", 1024, 1024);
            const Index far = textures.add(VFS::Path::NormalizedView("textures/far.dds"), image.get());
            const Index whole = textures.add(VFS::Path::NormalizedView("textures/whole.dds"), image.get());
            const Index unstood = textures.add(VFS::Path::NormalizedView("textures/unstood.dds"), image.get());
            textures.stand(far, 5592404, false, 0);
            textures.stand(whole, 5592404, false, 0);

            const Index material = scene.addMaterial(Material{ .mDiffuse = far });
            MaterialHold held = scene.holdMaterial(material);
            const Index other = scene.addMaterial(Material{ .mDiffuse = unstood });
            MaterialHold otherHeld = scene.holdMaterial(other);
            scene.askMaterial(material, 23);
            scene.askMaterial(other, 23);
            EXPECT_EQ(textures.getWant(far), 1u);
            EXPECT_EQ(textures.getWant(unstood), 1u);

            EXPECT_EQ(textures.measureAboveWant(),
                (TexturesAboveWant{ .mStood = 2 * 5592404, .mAbove = 4194304, .mTextures = 1 }));

            scene.unaskMaterial(material, 23);
            scene.unaskMaterial(other, 23);
            EXPECT_EQ(textures.measureAboveWant(), (TexturesAboveWant{ .mStood = 2 * 5592404 }));

            // The far texture goes to rest with its material: it counts as resting, against the level its last
            // readers asked, 1, so level 0's bytes again. The texture that was never stood whole is freed.
            scene.drop(std::move(held));
            scene.drop(std::move(otherHeld));
            EXPECT_TRUE(textures.getRows()[far].mRest.mResting);
            EXPECT_EQ(textures.measureAboveWant(),
                (TexturesAboveWant{ .mStood = 5592404, .mRestingStood = 5592404, .mRestingAbove = 4194304 }));
        }

        /// **A want finer than what stood is handed over again; a coarser one is not.** A texture stood whole from
        /// level 3 and asked at level 15 does not arrive again; asked at level 1 as well, it arrives at the next
        /// `compact`, once.
        TEST(RtxTextureAsksTest, aWantFinerThanWhatStoodArrivesAgain)
        {
            SceneDesc scene;
            TextureTable& textures = scene.textures();
            const osg::ref_ptr<osg::Image> image = Testing::makeImage("textures/large.dds", 1024, 1024);
            const Index slot = textures.add(VFS::Path::NormalizedView("textures/stood.dds"), image.get());
            textures.compact(0);
            textures.clearArrivals();
            textures.stand(slot, 64, false, 3);

            const Index material = scene.addMaterial(Material{ .mDiffuse = slot });
            MaterialHold held = scene.holdMaterial(material);

            // Step 37, footprint 5, plus the offset of 10: level 15, coarser than 3. Nothing arrives.
            scene.askMaterial(material, 37);
            textures.compact(1);
            EXPECT_TRUE(textures.getArrived().empty());

            // Step 23, footprint -9: level 1, finer than 3. It arrives again, once.
            scene.askMaterial(material, 23);
            textures.compact(2);
            ASSERT_EQ(textures.getArrived().size(), 1u);
            EXPECT_EQ(textures.getArrived()[0], slot);
            textures.clearArrivals();
            textures.compact(3);
            EXPECT_TRUE(textures.getArrived().empty()) << "handed over once";

            scene.unaskMaterial(material, 23);
            scene.unaskMaterial(material, 37);
            scene.drop(std::move(held));
        }
    }
}
