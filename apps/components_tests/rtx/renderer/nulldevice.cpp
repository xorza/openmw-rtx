#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/residencyconformance.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/nulldevice.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>

namespace Rtx
{
    namespace
    {
        struct NullDeviceFactory
        {
            static constexpr std::uint32_t sFramesMost = 4;

            using Device = NullDevice;

            static std::unique_ptr<NullDevice> make() { return std::make_unique<NullDevice>(std::uint64_t{ 1 } << 30); }
        };

        const Slot sSlot{ .mSlab = SlabIndex(3), .mAt = 7 };
        const SlotPlace sPlace{ .mSlot = sSlot };

        /// What a hand-over places: nothing, since the null device reads only the operations.
        struct EmptyScene
        {
            SceneIdentities mIdentities;
            SceneDesc mScene{ mIdentities };
        };

        /// **The null device prices by formula**: an RGBA8 256² chain is its 349 524 texel bytes rounded up to 256,
        /// 349 696; a BC1 one, eight bytes a 4×4 block and a block for each level under four texels, 43 704 rounded to
        /// 43 776; two triangles loose 2 · 64 + 256 = 384 bytes, refitted 2 · 96 + 256 = 448, each rounded to 512.
        TEST(RtxNullDeviceTest, pricesByFormula)
        {
            const NullDevice device(0);
            EXPECT_EQ(device.priceImage(ImageShape{
                          .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 256, .mHeight = 256, .mLevels = 9 }),
                (ContentPrice{ .mBytes = 349'696, .mAlignment = 256 }));
            EXPECT_EQ(device.priceImage(ImageShape{
                          .mFormat = TextureFormat::Bc1RgbaUnorm, .mWidth = 256, .mHeight = 256, .mLevels = 9 }),
                (ContentPrice{ .mBytes = 43'776, .mAlignment = 256 }));
            EXPECT_EQ(device.priceImage(ImageShape{
                          .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 32768, .mHeight = 1, .mLevels = 1 }),
                std::nullopt);
            EXPECT_EQ(device.priceStructure(StructureShape{ .mTriangles = 2, .mVertices = 4 }).mBytes, 512u);
            EXPECT_EQ(
                device.priceStructure(StructureShape{ .mTriangles = 2, .mVertices = 4, .mRefits = true }).mBytes, 512u);
            EXPECT_EQ(
                device.priceStructure(StructureShape{ .mTriangles = 100, .mVertices = 4, .mRefits = true }).mBytes,
                9'984u)
                << "100 · 96 + 256 = 9 856, rounded up to 39 · 256";
        }

        /// **Its events come a frame late**: what the first hand-over rode finishes at the second frame's opening, not
        /// the first's; a structure built is answered tight, half its price, at the next opening.
        TEST(RtxNullDeviceTest, answersAFrameLate)
        {
            EmptyScene empty;
            NullDevice device(0);
            DeviceEvents events;
            std::ignore = device.openFrame(events);
            const std::vector<ResidencyOp> build{ BuildStructure{
                .mMesh = MeshIndex(5), .mGeneration = 2, .mLoose = sPlace, .mBytes = 1024 } };
            const DeviceEpoch first = device.handOver(SceneSlot::world(), empty.mScene, build);
            EXPECT_EQ(first, DeviceEpoch{ .mValue = 1 });

            std::ignore = device.openFrame(events);
            EXPECT_EQ(events.mFinished, DeviceEpoch{ .mValue = 0 });
            ASSERT_EQ(events.mTightSizes.size(), 1u);
            EXPECT_EQ(events.mTightSizes[0].mMesh, MeshIndex(5));
            EXPECT_EQ(events.mTightSizes[0].mGeneration, 2u);
            EXPECT_EQ(events.mTightSizes[0].mBytes, 512u);

            std::ignore = device.handOver(SceneSlot::world(), empty.mScene, {});
            std::ignore = device.openFrame(events);
            EXPECT_EQ(events.mFinished, first);
            EXPECT_TRUE(events.mTightSizes.empty());
        }

        /// **The budget is the test's**, and a block it is told to refuse is answered refused and not held.
        TEST(RtxNullDeviceTest, takesItsBudgetAndRefusalsFromTheTest)
        {
            NullDevice device(100);
            DeviceEvents events;
            EXPECT_EQ(device.openFrame(events).mCapBytes, 100u);
            device.setCap(200);
            device.refuseNextBlock();
            device.acquireBlock(BlockIndex(0), ArenaIndex{ 0 }, 64);
            device.acquireBlock(BlockIndex(1), ArenaIndex{ 0 }, 64);
            EXPECT_EQ(device.openFrame(events).mCapBytes, 200u);
            EXPECT_EQ(events.mBlocksRefused, std::vector<BlockIndex>{ BlockIndex(0) });
            EXPECT_EQ(events.mBlocksArrived, std::vector<BlockIndex>{ BlockIndex(1) });
            EXPECT_EQ(device.getBlocksHeld(), 1u);
        }

        /// **What a card would corrupt in silence the null device refuses**: a slot stood twice, and one stood again
        /// before the epoch it was given back at finished. Given back at epoch 2, it is free once a frame's events say
        /// 2 finished, at the opening after the third hand-over.
        TEST(RtxNullDeviceTest, refusesASlotStoodTwiceOrBeforeItsEpoch)
        {
            EmptyScene empty;
            const StandImage stand{ .mTexture = TextureIndex(0), .mTo = sPlace, .mUpload = { .mFirst = 0, .mEnd = 1 } };
            Testing::expectAssertDies(
                [&] {
                    NullDevice device(0);
                    std::ignore
                        = device.handOver(SceneSlot::world(), empty.mScene, std::vector<ResidencyOp>{ stand, stand });
                },
                "a slot stood twice");

            NullDevice device(0);
            DeviceEvents events;
            std::ignore = device.handOver(SceneSlot::world(), empty.mScene, std::vector<ResidencyOp>{ stand });
            const std::vector<ResidencyOp> drop{ DropImage{ .mTexture = TextureIndex(0), .mFrom = sSlot } };
            EXPECT_EQ(device.handOver(SceneSlot::world(), empty.mScene, drop), DeviceEpoch{ .mValue = 2 });
            std::ignore = device.openFrame(events);
            Testing::expectAssertDies(
                [&] {
                    std::ignore = device.handOver(SceneSlot::world(), empty.mScene, std::vector<ResidencyOp>{ stand });
                },
                "a slot stood before the epoch it was given back at finished");
            std::ignore = device.handOver(SceneSlot::world(), empty.mScene, {});
            std::ignore = device.openFrame(events);
            ASSERT_EQ(events.mFinished, DeviceEpoch{ .mValue = 2 });
            std::ignore = device.handOver(SceneSlot::world(), empty.mScene, std::vector<ResidencyOp>{ stand });
        }
    }

    // gtest names the suite in identifiers it builds, so it is instantiated in the suite's own namespace.
    namespace Testing
    {
        INSTANTIATE_TYPED_TEST_SUITE_P(Null, ResidencyDeviceConformance, NullDeviceFactory);
    }
}
