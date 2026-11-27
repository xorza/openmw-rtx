#pragma once

#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>

namespace Rtx::Testing
{
    /// **What every backend owes residency, one suite run over each** (`ResidencyDevice`): a type-parameterized gtest
    /// suite, its tests written once here and instantiated by each binary over a factory of its device, so a backend
    /// that keeps the null device's contract keeps the card's. `Factory::make()` returns a fresh `Factory::Device`,
    /// held by its own type since the interface is never deleted through, with a cap of at least a gigabyte, and
    /// `Factory::sFramesMost` bounds the frames an answer may take.
    template <class Factory>
    class ResidencyDeviceConformance : public testing::Test
    {
    protected:
        std::unique_ptr<typename Factory::Device> mOwned = Factory::make();
        ResidencyDevice* mDevice = mOwned.get();
        DeviceEvents mEvents;
        SceneIdentities mIdentities;
        SceneDesc mScene{ mIdentities };

        /// Frames opened until `answered` holds of what the events said, at most `Factory::sFramesMost`.
        template <class Answered>
        bool openUntil(Answered answered)
        {
            for (std::uint32_t frame = 0; frame < Factory::sFramesMost; ++frame)
            {
                std::ignore = mDevice->openFrame(mEvents);
                if (answered(mEvents))
                    return true;
            }
            return false;
        }
    };

    TYPED_TEST_SUITE_P(ResidencyDeviceConformance);

    /// **A price is a power-of-two alignment and bytes that grow with the chain**: an RGBA8 256² chain costs no less
    /// than its texels, 349 524 bytes (`4 · (256² + 128² + … + 1)`), and more than the 128² chain under its first
    /// level; a chain of no format the device takes is no price. **The bytes are the device's own and no multiple of
    /// the alignment**, which Vulkan promises of neither an image's requirements nor a structure's size: a slot rounds
    /// them up (`SlotLedger::strideFor`).
    TYPED_TEST_P(ResidencyDeviceConformance, pricesAnImageAtLeastItsTexelsAndNothingItCannotTake)
    {
        const ImageShape full{ .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 256, .mHeight = 256, .mLevels = 9 };
        const ImageShape coarser{ .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 128, .mHeight = 128, .mLevels = 8 };
        const std::optional<ContentPrice> fullPrice = this->mDevice->priceImage(full);
        const std::optional<ContentPrice> coarserPrice = this->mDevice->priceImage(coarser);
        ASSERT_TRUE(fullPrice.has_value());
        ASSERT_TRUE(coarserPrice.has_value());
        EXPECT_GE(fullPrice->mBytes, 349'524u);
        EXPECT_GT(fullPrice->mBytes, coarserPrice->mBytes);
        EXPECT_TRUE(std::has_single_bit(fullPrice->mAlignment));

        EXPECT_EQ(this->mDevice->priceImage(
                      ImageShape{ .mFormat = TextureFormat::Unnamed, .mWidth = 256, .mHeight = 256, .mLevels = 9 }),
            std::nullopt);
    }

    /// **A structure's price grows with its triangles**, and starts at a power-of-two alignment, as an image's does.
    TYPED_TEST_P(ResidencyDeviceConformance, pricesAStructureByItsTriangles)
    {
        if (!this->mDevice->standsStructures())
            GTEST_SKIP() << "the device stands its structures outside the arena";
        const ContentPrice one = this->mDevice->priceStructure(StructureShape{ .mTriangles = 2, .mVertices = 4 });
        const ContentPrice many
            = this->mDevice->priceStructure(StructureShape{ .mTriangles = 20'000, .mVertices = 10'000 });
        EXPECT_GT(one.mBytes, 0u);
        EXPECT_GT(many.mBytes, one.mBytes);
        EXPECT_TRUE(std::has_single_bit(one.mAlignment));
        EXPECT_TRUE(std::has_single_bit(many.mAlignment));
    }

    /// **Epochs never fall, and what finished never runs ahead of them or back**: three empty hand-overs ride epochs
    /// each at or past the last, a device that submits nothing for nothing answering the last it submitted; the
    /// events' finished epoch never falls and never passes the last handed over, and reaches it within the device's
    /// bound.
    TYPED_TEST_P(ResidencyDeviceConformance, epochsRiseAndFinishInOrder)
    {
        std::ignore = this->mDevice->openFrame(this->mEvents);
        DeviceEpoch last;
        for (std::uint32_t at = 0; at < 3; ++at)
        {
            const DeviceEpoch epoch = this->mDevice->handOver(SceneSlot::world(), this->mScene, {});
            EXPECT_GE(epoch, last);
            last = epoch;
        }
        DeviceEpoch finished = this->mEvents.mFinished;
        EXPECT_TRUE(this->openUntil([&](const DeviceEvents& events) {
            EXPECT_GE(events.mFinished, finished);
            EXPECT_LE(events.mFinished, last);
            finished = events.mFinished;
            return events.mFinished == last;
        }));
    }

    /// **A block asked is answered once, arrived or refused**, within the device's bound, and one given back after it
    /// arrived is gone without an answer.
    TYPED_TEST_P(ResidencyDeviceConformance, aBlockAskedIsAnsweredOnce)
    {
        // Far past what a test's own ledger names on a device the binary shares.
        const BlockIndex block(4000);
        this->mDevice->acquireBlock(block, this->mDevice->arenaOf(SlotKind::Image), std::uint64_t{ 64 } << 20);
        std::uint32_t answers = 0;
        bool arrived = false;
        EXPECT_TRUE(this->openUntil([&](const DeviceEvents& events) {
            for (const BlockIndex each : events.mBlocksArrived)
                if (each == block)
                {
                    ++answers;
                    arrived = true;
                }
            for (const BlockIndex each : events.mBlocksRefused)
                answers += each == block ? 1 : 0;
            return answers > 0;
        }));
        std::ignore = this->mDevice->openFrame(this->mEvents);
        EXPECT_TRUE(this->mEvents.mBlocksArrived.empty());
        EXPECT_TRUE(this->mEvents.mBlocksRefused.empty());
        EXPECT_EQ(answers, 1u);
        if (arrived)
            this->mDevice->releaseBlock(block);
    }

    REGISTER_TYPED_TEST_SUITE_P(ResidencyDeviceConformance, pricesAnImageAtLeastItsTexelsAndNothingItCannotTake,
        pricesAStructureByItsTriangles, epochsRiseAndFinishInOrder, aBlockAskedIsAnsweredOnce);
}
