#include <array>

#include <gtest/gtest.h>

#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/waterbuilder.hpp>

namespace RtxBridge
{
    namespace
    {
        ESM::Cell makeCell(int flags, int x, int y, float water)
        {
            ESM::Cell cell;
            cell.mData.mFlags = flags;
            cell.mData.mX = x;
            cell.mData.mY = y;
            cell.mWater = water;
            return cell;
        }

        /// Something for an interior's pool to be sized against: a 200 by 100 floor about the origin.
        Rtx::SceneDesc makeRoom()
        {
            const std::array positions{
                osg::Vec3f(-100.0f, -50.0f, 0.0f),
                osg::Vec3f(100.0f, -50.0f, 0.0f),
                osg::Vec3f(100.0f, 50.0f, 0.0f),
                osg::Vec3f(-100.0f, 50.0f, 0.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            Rtx::SceneDesc scene;
            scene.addInstance(Rtx::MeshInstance{
                .mTransform = osg::Matrixf::identity(),
                .mMesh = scene.addMesh(positions, {}, {}, indices),
            });
            return scene;
        }

        /// The sea is at zero everywhere out of doors, and a cell needs its own footprint of it.
        ///
        /// Not one shipped exterior carries a height of its own, so Vvardenfell is one body of water
        /// and there is no per-cell value to read or boundary to interpolate across.
        TEST(RtxWaterBuilderTest, anExteriorGetsItsOwnFootprintOfSeaAtZero)
        {
            constexpr auto side = static_cast<float>(Constants::CellSizeInUnits);

            Rtx::SceneDesc scene;
            // A height on the record, to prove it is not the one used.
            addWater(scene, makeCell(0, -2, 3, 512.0f));

            ASSERT_EQ(scene.getInstances().size(), 1u);
            const Rtx::MeshInstance& placed = scene.getInstances().front();

            const osg::Vec3f centre = placed.mTransform.getTrans();
            EXPECT_FLOAT_EQ(centre.x(), -1.5f * side);
            EXPECT_FLOAT_EQ(centre.y(), 3.5f * side);
            EXPECT_FLOAT_EQ(centre.z(), 0.0f) << "the sea, not the record's height";

            // A unit quad scaled to the cell, so its corners land on the cell's own boundaries.
            const osg::Vec3f corner = osg::Vec3f(-0.5f, -0.5f, 0.0f) * placed.mTransform;
            EXPECT_FLOAT_EQ(corner.x(), -2.0f * side);
            EXPECT_FLOAT_EQ(corner.y(), 3.0f * side);

            ASSERT_EQ(scene.getMaterials().size(), 1u);
            EXPECT_EQ(scene.getMaterials().front().mKind, Rtx::MaterialKind::Water);
        }

        /// The flag is the gate and the height is not.
        ///
        /// Every interior in the game carries a water height whether or not it has water, so taking
        /// the height's presence as the answer floods several hundred dry rooms.
        TEST(RtxWaterBuilderTest, anInteriorIsFloodedOnlyWhenItSaysSo)
        {
            Rtx::SceneDesc dry = makeRoom();
            addWater(dry, makeCell(ESM::Cell::Interior, 0, 0, -32.0f));
            EXPECT_EQ(dry.getInstances().size(), 1u) << "the floor and nothing else";

            Rtx::SceneDesc flooded = makeRoom();
            addWater(flooded, makeCell(ESM::Cell::Interior | ESM::Cell::HasWater, 0, 0, -32.0f));
            ASSERT_EQ(flooded.getInstances().size(), 2u);

            // At the height the record names, spanning what the room holds — which is as much as a
            // flat quad can know about a cave.
            const osg::Matrixf& transform = flooded.getInstances().back().mTransform;
            EXPECT_FLOAT_EQ(transform.getTrans().z(), -32.0f);

            const osg::Vec3f low = osg::Vec3f(-0.5f, -0.5f, 0.0f) * transform;
            const osg::Vec3f high = osg::Vec3f(0.5f, 0.5f, 0.0f) * transform;
            EXPECT_FLOAT_EQ(low.x(), -100.0f);
            EXPECT_FLOAT_EQ(low.y(), -50.0f);
            EXPECT_FLOAT_EQ(high.x(), 100.0f);
            EXPECT_FLOAT_EQ(high.y(), 50.0f);
        }

        /// A room with nothing in it has no bounds to size a pool by, and gets none.
        TEST(RtxWaterBuilderTest, anEmptyInteriorGetsNoPool)
        {
            Rtx::SceneDesc scene;
            addWater(scene, makeCell(ESM::Cell::Interior | ESM::Cell::HasWater, 0, 0, -32.0f));
            EXPECT_TRUE(scene.getInstances().empty());
        }
    }
}
