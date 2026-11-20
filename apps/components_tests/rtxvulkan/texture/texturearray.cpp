#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/heldsubmit.hpp>
#include <apps/components_tests/rtxvulkan/support/device/hostwritten.hpp>
#include <apps/components_tests/rtxvulkan/support/device/scenestander.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/stagingring.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/texture/texturepasses.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxTextureArrayTest : Testing::DeviceTest
        {
        };

        /// Pays what `slot`'s set owes, its texel words through a batch flushed here.
        void syncSet(TextureArray& textures, CommandPool& pool, const FrameSlot slot)
        {
            Testing::flushWriting(pool, [&](Uploads& uploads) { textures.sync(slot, uploads); });
        }

        /// Writing a set's descriptors waits for the submit that bound the set, and for nothing else.
        ///
        /// **The one per-copy resource that is not a buffer.** A set is bound by handle, so no address names it, and
        /// its bindings allow an update after a bind, but not of a descriptor a pending trace samples, and a trace
        /// samples whichever slots its materials name. `getSet` stamps the set the way `Buffer::addressFor` stamps a
        /// buffer; `finishReads` waits for that stamp. Held on the queue while this thread waits, so the wait cannot
        /// return before the hold opens.
        TEST_F(RtxTextureArrayTest, syncingASetWaitsForTheSubmitThatBoundIt)
        {
            Device& device = getDevice();
            CommandPool& pool = getPool();

            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(pool);
            Testing::ArenaDevice arena(device);
            Testing::SceneStander stander(arena);
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();
            const auto stand = [&](Batch& batch, const TextureData& arrived) {
                textures.execute(
                    TextureArray::Executing{
                        .mBatch = batch, .mPasses = passes, .mArrival = rooms, .mArena = device.getContent() },
                    stander.operationsFor(arena, SceneSlot::world(), std::span(&arrived, 1)));
                stander.handedOver(DeviceEpoch{ .mValue = device.getTimeline().getNext() });
            };

            // An arrival, owed to every set and paid before either is bound: a set is handed out
            // only once `sync` has written what it owes. Ahead of the hold, because a flush behind
            // a held submit waits for the hold.
            const std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const TextureData arrived = Testing::describeTexel(white, TextureIndex(0));
            Batch arrival(pool);
            stand(arrival, arrived);
            arrival.flush();
            Testing::expectAssertDies([&] { static_cast<void>(textures.getSet(FrameSlot{ 0 })); },
                "a texture set sampled before `sync` paid what it owes");
            syncSet(textures, pool, FrameSlot{ 0 });
            syncSet(textures, pool, FrameSlot{ 1 });

            Testing::HeldSubmit hold(device);
            const LentCommands binder = pool.lend(1);
            Recording binding = pool.begin(binder[0]);
            EXPECT_NE(textures.getSet(FrameSlot{ 0 }), VK_NULL_HANDLE);

            // A second arrival, owed to both again once the first set is bound: what `sync` has to
            // write once that set is free. Deferred, so it rides ahead of the held submit.
            const TextureData second = Testing::describeTexel(white, TextureIndex(1));
            Batch secondArrival(pool);
            stand(secondArrival, second);
            secondArrival.defer();
            const std::uint64_t bound = hold.submit(std::move(binding));

            // The other set, which nothing bound, waits for nothing: it returns with the binder still held.
            const Timeline& timeline = device.getTimeline();
            textures.finishReads(FrameSlot{ 1 });
            EXPECT_FALSE(timeline.hasFinished(bound)) << "the set nothing bound waited for the other's binder";

            // A wait that returned at once is told from one that waited by the submit having run when it returns.
            hold.releaseAfter(std::chrono::milliseconds(20));
            textures.finishReads(FrameSlot{ 0 });
            EXPECT_TRUE(timeline.hasFinished(bound)) << "the sync did not wait for the set's binder";

            // A wait that returned early writes this into a set a submit the hold still keeps on
            // the queue has bound, which is the write the assert fires on.
            syncSet(textures, pool, FrameSlot{ 0 });
            syncSet(textures, pool, FrameSlot{ 1 });
        }

        /// **A change of anisotropy is in every set when it returns**, on a queue that binds none of them: a set left
        /// owing its samplers until its next placement was a picture's scene traced unplaced through samplers the
        /// graveyard had destroyed. A set bound by a submit still on the queue is a contract the renderer's drain
        /// keeps, and the write dies under it.
        TEST_F(RtxTextureArrayTest, aChangeOfAnisotropyIsInEverySetWhenItReturns)
        {
            Device& device = getDevice();
            CommandPool& pool = getPool();

            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(pool);
            TextureArray textures(device, setup, layout, passes, rooms, 1, 1);
            setup.flush();
            syncSet(textures, pool, FrameSlot{ 0 });
            syncSet(textures, pool, FrameSlot{ 1 });

            // Handed out with no sync between: the sets owe nothing.
            textures.setAnisotropy(16);
            const LentCommands binder = pool.lend(1);
            Recording binding = pool.begin(binder[0]);
            EXPECT_NE(textures.getSet(FrameSlot{ 0 }), VK_NULL_HANDLE);
            EXPECT_NE(textures.getSet(FrameSlot{ 1 }), VK_NULL_HANDLE);

            Testing::HeldSubmit hold(device);
            const std::uint64_t bound = hold.submit(std::move(binding));
            Testing::expectAssertDies(
                [&] { textures.setAnisotropy(8); }, "samplers written under a submit still bound to their set");
            hold.release();
            device.waitFor(bound, "the binder");
            textures.setAnisotropy(8);
        }

        /// **A load's staging stays at the kept size**: six files of 16 MiB each, 96 MiB past the kept 64, are
        /// recorded and flushed once four have staged, so the last two reuse the four's blocks and the ring makes four
        /// and not six. Every file stands.
        TEST_F(RtxTextureArrayTest, aLoadsStagingStaysAtTheKeptSize)
        {
            Device& device = getDevice();
            CommandPool& pool = getPool();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            constexpr std::uint32_t sFiles = 6;
            Batch setup(pool);
            Testing::ArenaDevice arena(device);
            Testing::SceneStander stander(arena);
            TextureArray textures(device, setup, layout, passes, rooms, sFiles);
            setup.flush();
            device.waitIdle();
            const std::uint64_t finished = device.getTimeline().getKnownFinished();
            pool.getStaging().releaseThrough(finished, finished);

            Testing::TestTexture large;
            Testing::paintLevels(large, 2048, 2048, 1, "large");
            ASSERT_EQ(large.mBytes.size(), std::size_t{ 16 } << 20);
            ASSERT_EQ(sStagingKept, VkDeviceSize{ 64 } << 20) << "the arithmetic above is written against this";
            std::vector<TextureData> arrived(sFiles, large.mData);
            for (std::uint32_t slot = 0; slot < sFiles; ++slot)
                arrived[slot].mSlot = TextureIndex(slot);

            const std::span<const ResidencyOp> ops = stander.operationsFor(arena, SceneSlot::world(), arrived);
            const std::size_t blocks = pool.getStaging().getBlockCount();
            Batch arrival(pool);
            textures.execute(
                TextureArray::Executing{
                    .mBatch = arrival, .mPasses = passes, .mArrival = rooms, .mArena = device.getContent() },
                ops);
            arrival.flush();
            stander.handedOver(DeviceEpoch{ .mValue = device.getTimeline().getNext() });
            EXPECT_EQ(textures.getHeld().mCount, sFiles);
            EXPECT_LE(pool.getStaging().getBlockCount() - blocks, 4u) << "the load's staging grew past the kept size";
        }

        /// **An image of a side past the device's has no price**, so residency plans from the first level within it,
        /// and one within is priced at least its texels: a 256-square RGBA8 chain, 349 524 bytes of texels, with its
        /// alignment a power of two, and the same past the side refused.
        TEST_F(RtxTextureArrayTest, anImagePastTheDevicesSideHasNoPrice)
        {
            const Device& device = getDevice();
            const std::uint32_t side
                = std::min(device.getPhysicalDevice().getTextureSide(), std::uint32_t{ Shaders::TEXTURE_SIDE_MOST });
            const std::optional<ContentPrice> within = Texture::price(device,
                ImageShape{ .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 256, .mHeight = 256, .mLevels = 9 });
            ASSERT_TRUE(within.has_value());
            EXPECT_GE(within->mBytes, 349'524u);
            EXPECT_TRUE(std::has_single_bit(within->mAlignment));
            EXPECT_EQ(
                Texture::price(device,
                    ImageShape{ .mFormat = TextureFormat::Rgba8Unorm, .mWidth = 2 * side, .mHeight = 1, .mLevels = 1 }),
                std::nullopt);
        }

        /// **An operation stands what it says**: a 64-square RGBA8 file stood from level 1 is 32 texels a side, a
        /// lowering a level further copies it to 16, and a drop leaves the slot drawing the stand-in, its texel word
        /// marked so (`TEXTURE_STANDS_IN`).
        TEST_F(RtxTextureArrayTest, anOperationStandsWhatItSays)
        {
            Device& device = getDevice();
            CommandPool& pool = getPool();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(pool);
            Testing::ArenaDevice arena(device);
            Testing::SceneStander stander(arena);
            TextureArray textures(device, setup, layout, passes, rooms, 1);
            setup.flush();
            const auto execute = [&](std::span<const ResidencyOp> ops) {
                Batch batch(pool);
                textures.execute(
                    TextureArray::Executing{
                        .mBatch = batch, .mPasses = passes, .mArrival = rooms, .mArena = device.getContent() },
                    ops);
                batch.flush();
                stander.handedOver(DeviceEpoch{ .mValue = device.getTimeline().getNext() });
            };

            Testing::TestTexture file;
            Testing::paintLevels(file, 64, 64, 7, "file");
            file.mData.mSlot = TextureIndex(0);
            file.mData.mWant = 1;
            execute(stander.operationsFor(arena, SceneSlot::world(), std::span(&file.mData, 1)));
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(32, 32));
            EXPECT_EQ(textures.getHeld().mCount, 1u);

            // A lowering names the slot it stood in, and copies on the device into another.
            const std::span<const ResidencyOp> stood = stander.operationsFor(arena, SceneSlot::world(), {});
            ASSERT_TRUE(stood.empty());
            TextureData lowered = file.mData;
            lowered.mWant = 2;
            const std::span<const ResidencyOp> restood
                = stander.operationsFor(arena, SceneSlot::world(), std::span(&lowered, 1));
            ASSERT_EQ(restood.size(), 1u);
            StandImage lowering = std::get<StandImage>(restood[0]);
            lowering.mFromLevel = 1;
            lowering.mLevel = 2;
            lowering.mTexels = nullptr;
            lowering.mUpload = {};
            execute(std::span<const ResidencyOp>(std::vector<ResidencyOp>{ lowering }));
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(16, 16));

            const std::vector<ResidencyOp> drop{ DropImage{
                .mTexture = TextureIndex(0), .mFrom = lowering.mTo.mSlot } };
            execute(drop);
            EXPECT_NE(textures.getTexels(TextureIndex(0)) & Shaders::TEXTURE_STANDS_IN, 0u);
            EXPECT_EQ(textures.getHeld().mCount, 0u);
        }
    }
}
