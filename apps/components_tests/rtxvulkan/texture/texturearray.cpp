#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/image.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/heldsubmit.hpp>
#include <apps/components_tests/rtxvulkan/support/device/hostwritten.hpp>
#include <apps/components_tests/rtxvulkan/support/device/memorylimits.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/stagingring.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/texture/texturecost.hpp>
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
            TextureArray textures(device, setup, layout, passes, rooms, 1);
            setup.flush();

            // An arrival, owed to every set and paid before either is bound: a set is handed out
            // only once `sync` has written what it owes. Ahead of the hold, because a flush behind
            // a held submit waits for the hold.
            const std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const TextureData arrived = Testing::describeTexel(white, TextureIndex(0));
            Batch arrival(pool);
            DeviceAnswer answer;
            std::vector<Refusal>& refused = answer.mRefused;
            textures.write(arrival, passes, rooms, std::span(&arrived, 1), TextureArray::noWants(), answer,
                WhenRoomless::Refuses, nullptr);
            EXPECT_TRUE(refused.empty());
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
            textures.write(secondArrival, passes, rooms, std::span(&second, 1), TextureArray::noWants(), answer,
                WhenRoomless::Refuses, nullptr);
            EXPECT_TRUE(refused.empty());
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

            const std::size_t blocks = pool.getStaging().getBlockCount();
            DeviceAnswer answer;
            Batch arrival(pool);
            {
                const Testing::BudgetLimit ample(device.getMemory(), ~VkDeviceSize{ 0 });
                textures.write(
                    arrival, passes, rooms, arrived, TextureArray::noWants(), answer, WhenRoomless::Refuses, nullptr);
            }
            arrival.flush();
            EXPECT_TRUE(answer.mRefused.empty());
            EXPECT_EQ(textures.getHeld().mCount, sFiles);
            EXPECT_LE(pool.getStaging().getBlockCount() - blocks, 4u) << "the load's staging grew past the kept size";
        }

        /// An arrival of one file is held to the largest side it fits the room at, the level a file begins at: a file
        /// from its first level within the side, a bake as large as its source is held to, and a composite, whose side
        /// is the renderer's own, at what it costs. Where the arrival fits at no side, its files are held to the side
        /// they fit at without the composite, which gives way; and where they fit at none, each begins at its last
        /// level.
        ///
        /// **Hand-computed.** The ladder is 64, 32 and 16 texels square, four bytes a texel: 21504 bytes from the top,
        /// 5120 from the second level and 1024 from the third; a bake of it is 5376 texels from 64 and 1280 from 32. A
        /// composite is 512 square in BC7 with its chain: 21847 blocks of sixteen bytes, 349552, whatever the side.
        /// The scene's first composite also brings the canvas it is baked on (349525 texels at four bytes) and the
        /// blocks it is encoded through, 1398100 + 349552 = 1747652, and 2097204 with the composite: a byte short of
        /// it and the ladder's least, the arrival fits nowhere and the ladder is held as though the composite were not
        /// there.
        ///
        /// **The companions and what a texture is made through count too.** The ladder as a normal map brings a spread
        /// of half its side to one texel and the sixteen-byte means it is built through, priced once as what the
        /// arrival grows the room by, all of it here: from 64, 1365 texels, 1365 + 21840 bytes beside the 21504, 44709
        /// in all; from 32, 10917; from 16, 2469. A one-level 64 square is its level, 16384, whatever the side, and a
        /// bake of it the same again.
        TEST_F(RtxTextureArrayTest, anArrivalIsHeldToTheLargestSideItFitsTheRoomAt)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 3);
            setup.flush();

            Testing::TestTexture ladder;
            Testing::paintLevels(ladder, 64, 64, 3, "ladder");
            const TextureData composite{
                .mSlot = TextureIndex(1), .mSource = TextureSource::GroundComposite, .mFrom = MaterialIndex(0)
            };
            const TextureData bake{
                .mSlot = TextureIndex(2), .mSource = TextureSource::SpriteBake, .mFrom = TextureIndex(0)
            };

            Testing::TestTexture normals;
            Testing::paintLevels(normals, 64, 64, 3, "normals");
            normals.mData.mEncoding = TextureEncoding::Normal;
            Testing::TestTexture single;
            Testing::paintLevels(single, 64, 64, 1, "single");

            const std::array alone{ ladder.mData };
            const std::array ground{ ladder.mData, composite };
            const std::array baked{ ladder.mData, bake };
            const std::array spread{ normals.mData };
            const std::array lone{ single.mData, bake };

            const TextureCost atTop = priceFile(normals.mData, 0);
            EXPECT_EQ(atTop.mImage, 21504u);
            EXPECT_EQ(atTop.mCompanion, 1365u);
            EXPECT_EQ(spreadMeansBytes(normals.mData, 0), 21840u);
            EXPECT_EQ(atTop.standing(), 21504u + 1365u) << "a standing texture keeps no means";

            // The first level of each side: 64 the top, 32 the second, 16 the third, and below 16
            // the third again, where the ladder has no level. The one-level file has its one.
            struct Case
            {
                std::span<const TextureData> mArrived;
                VkDeviceSize mRoom{};
                std::uint32_t mFirst{};
            };
            const std::array cases{
                Case{ alone, 21504, 0 },
                Case{ alone, 21503, 1 },
                Case{ alone, 5120, 1 },
                Case{ alone, 5119, 2 },
                Case{ alone, 1024, 2 },
                Case{ alone, 1023, 2 },
                Case{ alone, 0, 2 },
                Case{ ground, 2097204 + 21504, 0 },
                Case{ ground, 2097204 + 21503, 1 },
                Case{ ground, 2097204 + 1024, 2 },
                Case{ ground, 2097203 + 1024, 0 },
                Case{ ground, 21503, 1 },
                Case{ baked, 21504 + 21504, 0 },
                Case{ baked, 21504 + 21503, 1 },
                Case{ baked, 5120 + 5120, 1 },
                Case{ baked, 5120 + 5119, 2 },
                Case{ spread, 44709, 0 },
                Case{ spread, 44708, 1 },
                Case{ spread, 10917, 1 },
                Case{ spread, 10916, 2 },
                Case{ spread, 2469, 2 },
                Case{ spread, 2468, 2 },
                Case{ lone, 16384 + 16384, 0 },
                Case{ lone, 16384 + 16383, 0 },
            };

            std::vector<std::uint32_t> firsts;
            for (const Case& one : cases)
            {
                textures.chooseLevels(one.mArrived, one.mRoom, rooms, firsts);
                ASSERT_EQ(firsts.size(), one.mArrived.size());
                EXPECT_EQ(firsts[0], one.mFirst) << one.mArrived.size() << " textures in " << one.mRoom << " bytes";
            }
        }

        /// **Room is made by what stands moving down, the most bytes first, copied on the device** (`makeRoom`): a
        /// 128-square ladder of three levels, 65536 + 16384 + 4096 = 86016 bytes, each level painted its own value, and
        /// a 64-square file of one level, 16384, stand whole. Asked for a byte, the ladder moves a level down, its
        /// second level copied into the new image's first, 20480 bytes standing and 65536 given back, and it is named
        /// short with the 86016 it stood before; the file of one level cannot move. Asked for more than the ladder can
        /// give, it moves no further, 64 square at the floor. A slot an arrival names stays where it is.
        TEST_F(RtxTextureArrayTest, roomIsMadeByWhatStandsMovingDownCopiedOnTheDevice)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture ladder;
            Testing::paintLevels(ladder, 128, 128, 3, "ladder");
            for (std::size_t level = 0; level < ladder.mLevels.size(); ++level)
                std::fill_n(ladder.mBytes.begin() + ladder.mLevels[level].mOffset,
                    std::size_t{ ladder.mLevels[level].mWidth } * ladder.mLevels[level].mHeight * 4,
                    static_cast<std::uint8_t>(10 * (level + 1)));
            ladder.mData.mSlot = TextureIndex(0);
            Testing::TestTexture single;
            Testing::paintLevels(single, 64, 64, 1, "single");
            single.mData.mSlot = TextureIndex(1);
            {
                Batch batch(getPool());
                DeviceAnswer answer;
                const std::array both{ ladder.mData, single.mData };
                textures.write(
                    batch, passes, rooms, both, TextureArray::noWants(), answer, WhenRoomless::Waits, nullptr);
                batch.flush();
            }
            ASSERT_EQ(textures.getHeld().mBytes, 86016u + 16384u);

            DeviceAnswer answer;
            {
                Batch batch(getPool());
                const std::array kept{ ladder.mData };
                EXPECT_EQ(textures.makeRoom(rooms, TextureArray::RoomAsk{ .mBytes = 1, .mKeep = kept },
                              TextureArray::noWants(), answer),
                    0u)
                    << "the ladder kept, and the other cannot move";
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }

            {
                Batch batch(getPool());
                EXPECT_EQ(
                    textures.makeRoom(rooms, TextureArray::RoomAsk{ .mBytes = 1 }, TextureArray::noWants(), answer),
                    65536u);
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(64u, 64u));
            EXPECT_EQ(textures.getExtent(TextureIndex(1)), Shaders::uvec2(64u, 64u)) << "a file of one level";
            EXPECT_EQ(textures.getHeld().mBytes, 20480u + 16384u);
            EXPECT_EQ(textures.getHeld().mReduced, 1u);
            ASSERT_EQ(answer.mShort.size(), 1u);
            EXPECT_EQ(answer.mShort[0],
                (ShortTexture{ .mSlot = TextureIndex(0), .mStoodBytes = 20480, .mWholeBytes = 86016 }));
            EXPECT_EQ(textures.getTexels(TextureIndex(0)) & Shaders::TEXTURE_TEXELS, 64u * 64u);

            std::vector<std::uint8_t> read;
            textures.getImage(TextureIndex(0)).read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, read, 0);
            ASSERT_EQ(read.size(), 64u * 64u * 4u);
            EXPECT_EQ(read.front(), 20u) << "the ladder's second level, copied on the device";
            EXPECT_EQ(read.back(), 20u);
            textures.getImage(TextureIndex(0)).read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, read, 1);
            EXPECT_EQ(read.front(), 30u) << "its third";

            {
                Batch batch(getPool());
                EXPECT_EQ(textures.makeRoom(rooms, TextureArray::RoomAsk{ .mBytes = std::uint64_t{ 1 } << 30 },
                              TextureArray::noWants(), answer),
                    0u)
                    << "64 square, the floor";
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(64u, 64u));
        }

        /// **While the device asks for a block, the block of content's pools the array's textures fill least is emptied
        /// into the others, each texture copied as it stands, and the block goes** (`moveOut`): twenty textures of 4
        /// MiB, 1024 square in eight bits a channel, each painted its own byte, stand in two blocks of 64 MiB. A
        /// texture more than the second holds is dropped from the first, so the second is the emptiest and the first
        /// has room for all of it. Not asked, nothing moves. Asked, a budget of a byte moves one texture, the least a
        /// call moves, and the block is said to come back while it empties; a budget past the block moves the rest,
        /// each now in the first block, its texels the ones it was painted, its bytes counted as before. Once what they
        /// moved out of is buried, the library's one empty block goes at its next free.
        TEST_F(RtxTextureArrayTest, whileTheDeviceAsksTheEmptiestBlocksTexturesMoveAndTheBlockGoes)
        {
            Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            constexpr std::uint32_t count = 20;
            constexpr std::uint32_t side = 1024;
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, count);
            setup.flush();

            std::vector<Testing::TestTexture> painted(count);
            std::vector<TextureData> described;
            for (std::uint32_t at = 0; at < count; ++at)
            {
                Testing::TestTexture& texture = painted[at];
                texture.mBytes.assign(std::size_t{ side } * side * 4, static_cast<std::uint8_t>(at + 1));
                texture.mLevels = { MipLevel{ 0, side, side } };
                texture.describe(side, side, "painted");
                texture.mData.mSlot = TextureIndex(at);
                described.push_back(texture.mData);
            }
            {
                Batch batch(getPool());
                DeviceAnswer answer;
                textures.write(
                    batch, passes, rooms, described, TextureArray::noWants(), answer, WhenRoomless::Waits, nullptr);
                batch.flush();
            }
            ASSERT_EQ(textures.getHeld().mCount, count);

            const auto blockOf
                = [&](const std::uint32_t at) { return textures.getImage(TextureIndex(at)).getMemory().getHandle(); };
            std::vector<VkDeviceMemory> blocks;
            for (std::uint32_t at = 0; at < count; ++at)
                if (std::ranges::find(blocks, blockOf(at)) == blocks.end())
                    blocks.push_back(blockOf(at));
            ASSERT_EQ(blocks.size(), 2u) << "twenty textures of 4 MiB in blocks of 64";
            const auto inBlock = [&](const VkDeviceMemory block) {
                std::vector<std::uint32_t> slots;
                for (std::uint32_t at = 0; at < count; ++at)
                    if (blockOf(at) == block)
                        slots.push_back(at);
                return slots;
            };
            if (inBlock(blocks[0]).size() < inBlock(blocks[1]).size())
                std::swap(blocks[0], blocks[1]);
            const VkDeviceMemory full = blocks[0];
            const VkDeviceMemory emptiest = blocks[1];
            const std::vector<std::uint32_t> leaving = inBlock(emptiest);
            ASSERT_LT(leaving.size() + 1, inBlock(full).size());

            std::vector<TextureIndex> dropped;
            for (const std::uint32_t at : inBlock(full))
                if (dropped.size() < leaving.size() + 1)
                    dropped.push_back(TextureIndex(at));
            textures.drop(dropped);
            getPool().submitAndWait([](VkCommandBuffer) {});
            device.waitIdle();
            device.collectIdle();
            const VkDeviceSize held = textures.getHeld().mBytes;

            EXPECT_EQ(textures.moveOut(rooms, std::uint64_t{ 1 } << 30), 0u) << "not asked";
            const VkDeviceSize one = textures.getImage(TextureIndex(leaving.back())).getMemoryBytes();
            {
                const Testing::BudgetLimit asking(memory, 1);
                Batch batch(getPool());
                EXPECT_EQ(textures.moveOut(rooms, 1), one) << "one texture, the least a call moves";
                rooms.record(batch, passes, nullptr);
                batch.flush();
                EXPECT_EQ(inBlock(emptiest).size(), leaving.size() - 1);
                EXPECT_EQ(memory.getBlocksComing(), 1u) << "the block begun, coming back";
            }
            {
                Batch batch(getPool());
                EXPECT_EQ(textures.moveOut(rooms, std::uint64_t{ 1 } << 30), (leaving.size() - 1) * one)
                    << "a block begun is emptied, the device asking or not";
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }
            EXPECT_TRUE(inBlock(emptiest).empty());
            EXPECT_EQ(memory.getBlocksComing(), 1u) << "what moved out stands until the next call buries it";
            EXPECT_EQ(textures.getMovedOutBytes(), leaving.size() * one);
            EXPECT_EQ(textures.getHeld().mBytes, held);

            std::vector<std::uint8_t> read;
            for (const std::uint32_t at : leaving)
            {
                SCOPED_TRACE(at);
                EXPECT_EQ(blockOf(at), full);
                textures.getImage(TextureIndex(at)).read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, read, 0);
                ASSERT_EQ(read.size(), std::size_t{ side } * side * 4);
                EXPECT_EQ(std::ranges::count(read, static_cast<std::uint8_t>(at + 1)), std::ssize(read))
                    << "the texels it was painted, copied on the device";
            }

            // What they moved out of is buried by the next call, and the library keeps one empty block until its next
            // free, which a drop from the full block makes: the one block fewer is the emptied one, as the full one
            // still stands what it did less a texture.
            EXPECT_EQ(textures.moveOut(rooms, std::uint64_t{ 1 } << 30), 0u);
            EXPECT_EQ(memory.getBlocksComing(), 0u) << "buried: the graveyard counts it now";
            getPool().submitAndWait([](VkCommandBuffer) {});
            device.waitIdle();
            device.collectIdle();
            const std::size_t blocksBefore = memory.getBlockCount();
            const std::array last{ TextureIndex(inBlock(full).front()) };
            textures.drop(last);
            getPool().submitAndWait([](VkCommandBuffer) {});
            device.waitIdle();
            device.collectIdle();
            EXPECT_EQ(memory.getBlockCount(), blocksBefore - 1) << "the emptied block went back";
        }

        /// **What stands finer than its readers want moves down first, and is not short for it** (`makeRoom`): a
        /// 256-square ladder of three levels, 262144 + 65536 + 16384 = 344064 bytes, wanted whole, beside a 128-square
        /// one, 65536 + 16384 + 4096 = 86016, whose material is asked at step 26, footprint -6, plus its offset of
        /// 0.5 log2(128²) = 7: wanted from level 1. Asked for a byte, the smaller moves, though the larger stands more:
        /// it stands from level 1 now, 16384 + 4096 = 20480 bytes, all its readers want, so it is named moved and not
        /// short. Asked again, nothing stands above its want, and the larger moves, short of its file.
        TEST_F(RtxTextureArrayTest, whatStandsFinerThanItsReadersWantMovesFirstAndIsNotShort)
        {
            SceneIdentities identities;
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture large;
            Testing::paintLevels(large, 256, 256, 3, "large");
            large.mData.mSlot = TextureIndex(0);
            Testing::TestTexture small;
            Testing::paintLevels(small, 128, 128, 3, "small");
            small.mData.mSlot = TextureIndex(1);

            // The scene's table, its slots the array's: the small file's material asked from level 1.
            SceneDesc scene(identities);
            const osg::ref_ptr<osg::Image> largeImage = Testing::makeImage("textures/large.dds", 256, 256);
            const osg::ref_ptr<osg::Image> smallImage = Testing::makeImage("textures/small.dds", 128, 128);
            ASSERT_EQ(scene.textures().add(VFS::Path::NormalizedView("textures/large.dds"), largeImage.get()),
                TextureIndex(0));
            ASSERT_EQ(scene.textures().add(VFS::Path::NormalizedView("textures/small.dds"), smallImage.get()),
                TextureIndex(1));
            const MaterialIndex material = scene.addMaterial(Material{ .mDiffuse = TextureIndex(1) });
            MaterialHold held = scene.holdMaterial(material, HoldTier::Shown);
            scene.askMaterial(material, 26);
            ASSERT_EQ(scene.textures().getWant(TextureIndex(1)), 1u);
            ASSERT_EQ(scene.textures().getWant(TextureIndex(0)), 0u);
            {
                Batch batch(getPool());
                DeviceAnswer answer;
                const std::array both{ large.mData, small.mData };
                textures.write(
                    batch, passes, rooms, both, TextureArray::noWants(), answer, WhenRoomless::Waits, nullptr);
                batch.flush();
            }
            ASSERT_EQ(textures.getHeld().mBytes, 344064u + 86016u);

            DeviceAnswer answer;
            {
                Batch batch(getPool());
                EXPECT_EQ(
                    textures.makeRoom(rooms, TextureArray::RoomAsk{ .mBytes = 1 }, scene.textures(), answer), 65536u);
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }
            EXPECT_EQ(textures.getExtent(TextureIndex(1)), Shaders::uvec2(64u, 64u)) << "the smaller moved first";
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(256u, 256u));
            EXPECT_TRUE(answer.mShort.empty()) << "it stands all its readers want";
            ASSERT_EQ(answer.mMoved.size(), 1u);
            EXPECT_EQ(answer.mMoved[0].mSlot, TextureIndex(1));
            EXPECT_EQ(answer.mMoved[0].mStoodBytes, 20480u);
            EXPECT_EQ(answer.mMoved[0].mFrom, 1u);

            answer.clear();
            {
                Batch batch(getPool());
                EXPECT_EQ(
                    textures.makeRoom(rooms, TextureArray::RoomAsk{ .mBytes = 1 }, scene.textures(), answer), 262144u);
                rooms.record(batch, passes, nullptr);
                batch.flush();
            }
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(128u, 128u)) << "then the larger";
            ASSERT_EQ(answer.mShort.size(), 1u);
            EXPECT_EQ(answer.mShort[0],
                (ShortTexture{ .mSlot = TextureIndex(0), .mStoodBytes = 81920, .mWholeBytes = 344064 }));
            EXPECT_TRUE(answer.mMoved.empty());

            scene.unaskMaterial(material, 26);
            scene.drop(std::move(held));
        }

        /// **The file that stands the most bytes comes down a level first, and of two that stand alike the later slot,
        /// until the arrival fits.** Two of the ladders of `anArrivalIsHeldToTheLargestSideItFitsTheRoomAt`, 21504
        /// bytes whole, 5120 from the second level and 1024 from the third: in 21504 + 5120 bytes the second slot
        /// comes down one and the first stands whole, where one side for both held both to 32 and left 16384 unused; a
        /// byte short, both come down one; in 5120 + 1024, the second comes down two. A 64-square ladder beside a
        /// 32-square one, 5120 whole and 1024 from its second level, in 21504 + 1024 bytes: the large one comes down
        /// though it is the earlier slot, where taking the later slot first would have stood the large whole.
        TEST_F(RtxTextureArrayTest, theFileThatStandsTheMostComesDownFirstUntilTheArrivalFits)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture first;
            Testing::paintLevels(first, 64, 64, 3, "first");
            Testing::TestTexture second;
            Testing::paintLevels(second, 64, 64, 3, "second");
            second.mData.mSlot = TextureIndex(1);
            Testing::TestTexture small;
            Testing::paintLevels(small, 32, 32, 2, "small");
            small.mData.mSlot = TextureIndex(1);

            const std::array alike{ first.mData, second.mData };
            const std::array apart{ first.mData, small.mData };
            std::vector<std::uint32_t> firsts;

            textures.chooseLevels(alike, 21504 + 5120, rooms, firsts);
            EXPECT_EQ(firsts, (std::vector<std::uint32_t>{ 0, 1 })) << "the later slot of two alike first";
            textures.chooseLevels(alike, 21504 + 5119, rooms, firsts);
            EXPECT_EQ(firsts, (std::vector<std::uint32_t>{ 1, 1 }));
            textures.chooseLevels(alike, 5120 + 1024, rooms, firsts);
            EXPECT_EQ(firsts, (std::vector<std::uint32_t>{ 1, 2 }));

            textures.chooseLevels(apart, 21504 + 1024, rooms, firsts);
            EXPECT_EQ(firsts, (std::vector<std::uint32_t>{ 1, 0 })) << "the file that stands the most first";
            textures.chooseLevels(apart, 21504 + 5120, rooms, firsts);
            EXPECT_EQ(firsts, (std::vector<std::uint32_t>{ 0, 0 })) << "an arrival that fits came down";
        }

        /// **An arrival is priced only what its means room must grow by.** The normal map of
        /// `anArrivalIsHeldToTheLargestSideItFitsTheRoomAt` costs 44709 at the top, 21840 of it means, where the array
        /// holds none: a byte short of that and it is held to 32. Once one such map has arrived, the room holds its
        /// 21840 and the next pays none of it: 22869 is the top, and a byte short of it 32, in that array and in any
        /// other the room is handed to.
        TEST_F(RtxTextureArrayTest, anArrivalIsPricedWhatItsMeansRoomMustGrowBy)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture first;
            Testing::paintLevels(first, 64, 64, 3, "normals");
            first.mData.mEncoding = TextureEncoding::Normal;
            Testing::TestTexture second;
            Testing::paintLevels(second, 64, 64, 3, "normals again");
            second.mData.mEncoding = TextureEncoding::Normal;
            second.mData.mSlot = TextureIndex(1);

            std::vector<std::uint32_t> firsts;
            const auto firstOf = [&](TextureArray& array, const TextureData& texture, const VkDeviceSize room) {
                array.chooseLevels(std::span(&texture, 1), room, rooms, firsts);
                return firsts.at(0);
            };
            EXPECT_EQ(firstOf(textures, first.mData, 44709), 0u);
            EXPECT_EQ(firstOf(textures, first.mData, 44708), 1u);

            DeviceAnswer answer;

            std::vector<Refusal>& refused = answer.mRefused;
            Batch arrival(getPool());
            textures.write(arrival, passes, rooms, std::span(&first.mData, 1), TextureArray::noWants(), answer,
                WhenRoomless::Refuses, nullptr);
            arrival.flush();
            ASSERT_TRUE(refused.empty());

            EXPECT_EQ(firstOf(textures, second.mData, 44709 - 21840), 0u)
                << "the means the room holds were priced again";
            EXPECT_EQ(firstOf(textures, second.mData, 44709 - 21840 - 1), 1u);

            // **And so is another scene's array**: the rooms are the renderer's, one for every scene.
            Batch other(getPool());
            TextureArray elsewhere(device, other, layout, passes, rooms, 2);
            other.flush();
            EXPECT_EQ(firstOf(elsewhere, second.mData, 44709 - 21840), 0u)
                << "a second scene priced the means the shared room holds";
        }

        /// A texture past the side the device takes stands from its first level within the side, and one with no level
        /// within it draws the stand-in, refused and saying why.
        ///
        /// **One texel wider than the device takes**, so the case is the device's own and no larger than a row: the
        /// second level is half that, within the side, and is what stands, four bytes a texel. **The side is the
        /// device's, and never past `TEXTURE_SIDE_MOST`**, whose square the texel word holds whole.
        ///
        /// **The slot's texel word says which stands**: the level's count under the file's wrap, and the stand-in's
        /// four by four under its own wrap, `Repeat`, with `TEXTURE_STANDS_IN` over it, as for a slot described as the
        /// stand-in, which is how the builder describes a file that does not read.
        TEST_F(RtxTextureArrayTest, aTexturePastTheDevicesSideBeginsAtItsFirstLevelWithinIt)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            const std::uint32_t limit = textures.getSideLimit();
            EXPECT_EQ(limit, std::min(device.getPhysicalDevice().getTextureSide(), Shaders::TEXTURE_SIDE_MOST));

            Testing::TestTexture wide;
            Testing::paintLevels(wide, limit + 1, 1, 2, "wide");
            wide.mData.mWrap = TextureWrap::ClampT;
            Testing::TestTexture single;
            Testing::paintLevels(single, limit + 1, 1, 1, "single");
            single.mData.mSlot = TextureIndex(1);
            single.mData.mWrap = TextureWrap::Clamp;
            Testing::TestTexture unread;
            Testing::paintLevels(unread, 4, 4, 1, "unread");
            unread.mData.mSlot = TextureIndex(2);
            unread.mData.mSource = TextureSource::StandIn;

            const std::array arrived{ wide.mData, single.mData, unread.mData };
            DeviceAnswer answer;
            std::vector<Refusal>& refused = answer.mRefused;
            Batch arrival(getPool());
            textures.write(
                arrival, passes, rooms, arrived, TextureArray::noWants(), answer, WhenRoomless::Refuses, nullptr);
            arrival.flush();

            ASSERT_EQ(refused.size(), 1u);
            EXPECT_EQ(refused[0].mKind, Refused::Texture);
            EXPECT_EQ(refused[0].mName, "single");
            EXPECT_EQ(refused[0].mWhy,
                "its smallest level is " + std::to_string(limit + 1) + " by 1 texels, and the device takes "
                    + std::to_string(limit) + " a side");

            const TexturesHeld held = textures.getHeld();
            EXPECT_EQ(held.mCount, 1u) << "a texture the device takes at no level stood";
            EXPECT_EQ(held.mBytes, VkDeviceSize{ 4 } * ((limit + 1) >> 1));
            EXPECT_EQ(held.mReduced, 1u) << "a texture standing from its second level was not counted as smaller";
            EXPECT_EQ(held.mBySource.mFiles, held.mBytes) << "a file counted under another source";
            EXPECT_EQ(held.mBySource.mSpriteBakes + held.mBySource.mGroundComposites + held.mBySource.mGroundGloss, 0u);
            EXPECT_EQ(textures.getRoomHeld(), 0u) << "the device's side is not the room's";

            EXPECT_EQ(textures.getTexels(TextureIndex(0)), ((limit + 1) >> 1) | (2u << Shaders::TEXTURE_WRAP_SHIFT))
                << "the level that stands, clamped along t";
            EXPECT_EQ(textures.getTexels(TextureIndex(1)), Shaders::TEXTURE_STANDS_IN | 16u) << "past the side";
            EXPECT_EQ(textures.getTexels(TextureIndex(2)), Shaders::TEXTURE_STANDS_IN | 16u)
                << "described as the stand-in";

            // Across and down apart, which a texel count is not: the second level of a row past the
            // side is half the row by the one texel a level never goes under.
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2((limit + 1) >> 1, 1u))
                << "the level that stands";
            EXPECT_EQ(textures.getExtent(TextureIndex(1)), Shaders::uvec2(4u, 4u)) << "the stand-in's, past the side";

            // **The totals follow a slot given back**, which they are kept by and not walked for:
            // the one texture that stood is dropped, and nothing stands, nothing is smaller, and
            // nothing costs. Dropping the stand-in's slot as well takes nothing off.
            textures.drop(std::array{ TextureIndex(0), TextureIndex(1) });
            const TexturesHeld dropped = textures.getHeld();
            EXPECT_EQ(dropped.mCount, 0u);
            EXPECT_EQ(dropped.mBytes, 0u);
            EXPECT_EQ(dropped.mReduced, 0u);
            EXPECT_EQ(dropped.mBySource.mFiles, 0u);

            // **And each source under its own name**: a one-level 64 square and a bake of it, each
            // standing at its one level, 16384 bytes
            // (`anArrivalIsHeldToTheLargestSideItFitsTheRoomAt` derives it).
            Testing::TestTexture lone;
            Testing::paintLevels(lone, 64, 64, 1, "lone");
            const TextureData bake{ .mSlot = TextureIndex(1),
                .mSource = TextureSource::SpriteBake,
                .mFrom = TextureIndex(0),
                .mFormat = TextureFormat::Rgba8Unorm };
            {
                Batch again(getPool());
                textures.write(again, passes, rooms, std::array{ lone.mData, bake }, TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                again.flush();
            }
            const TexturesHeld bySource = textures.getHeld();
            EXPECT_EQ(bySource.mBySource.mFiles, 16384u);
            EXPECT_EQ(bySource.mBySource.mSpriteBakes, 16384u);
            EXPECT_EQ(bySource.mBytes, 2u * 16384u);
        }

        /// **A file the device has no room for waits where what stands moves down for it**, though nothing rests and
        /// nothing is buried: a 1024-square ladder of three levels, 4 MiB + 1 MiB + 256 KiB, stands; with the
        /// textures' ceiling 32 MiB over what the heap holds, a 4096-square file of one level, 64 MiB and so memory of
        /// its own, is refused its room. The ladder moves down a level for it, 512 square, and no further in the run,
        /// whose copies would read the image it wrote; so the room is coming: the file is named short, waiting on its
        /// whole image's range, and refused nothing. Where nothing could move, the next test's lone file is refused.
        TEST_F(RtxTextureArrayTest, aFileTheDeviceHasNoRoomForWaitsWhereWhatStandsMovesDownForIt)
        {
            Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture ladder;
            Testing::paintLevels(ladder, 1024, 1024, 3, "ladder");
            ladder.mData.mSlot = TextureIndex(0);
            Testing::TestTexture large;
            Testing::paintLevels(large, 4096, 4096, 1, "large");
            large.mData.mSlot = TextureIndex(1);
            {
                Batch arrival(getPool());
                DeviceAnswer answer;
                textures.write(arrival, passes, rooms, std::span(&ladder.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
                ASSERT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(1024u, 1024u));
            }

            DeviceAnswer answer;
            {
                const Testing::BudgetLimit tight(
                    memory, Testing::budgetAbove(memory, MemoryUse::Texture, VkDeviceSize{ 32 } << 20));
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&large.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(answer.mRefused.empty()) << "a file refused where what stood could move down for it";
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(512u, 512u)) << "the ladder, a level down";
            EXPECT_EQ(textures.getTexels(TextureIndex(1)), Shaders::TEXTURE_STANDS_IN | 16u)
                << "the stand-in, while it waits";
            EXPECT_NE(std::ranges::find(answer.mShort, ShortTexture{ .mSlot = TextureIndex(1), .mStoodBytes = 0 }),
                answer.mShort.end())
                << "the file is not named short";
            const VkMemoryRequirements whole = Image::requirementsFor(device, Texture::describeFile(large.mData, 0));
            EXPECT_NE(std::ranges::find(answer.mWants,
                          RoomWant{ .mWaiter = TextureIndex(1), .mHole = whole.size, .mAlignment = whole.alignment }),
                answer.mWants.end())
                << "the file waits on no range of its whole image";
            device.waitIdle();
            device.collectIdle();
        }

        /// A texture the device has no room for comes down a level at a time, and one it has room for at no level
        /// draws the stand-in, refused and saying why.
        ///
        /// **Room for one block, and every gap filled.** The ladder's first level, 3072 square at four bytes, is 36
        /// MiB, past half a block and so an allocation of its own; a small image the test holds comes first and opens
        /// the block, which leaves a megabyte under the ceiling, and the level is refused although the side chosen for
        /// the arrival counted the block as room for it. Its second level, 1536 square, is 9 MiB and fits the block:
        /// 9437184 bytes. The single level of the same 36 MiB has nothing further down. The slot's texel word says the
        /// stand-in while it stands in, and is the file's own count once it stands. **A file stood short waits on a
        /// range of its whole image's size and alignment** (`TextureArray::wantOf`), and every file that stands gives
        /// back its image's range once it rests.
        TEST_F(RtxTextureArrayTest, aTextureTheDeviceHasNoRoomForComesDownALevelOrDrawsTheStandIn)
        {
            Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture ladder;
            Testing::paintLevels(ladder, 3072, 3072, 2, "ladder");
            Testing::TestTexture lone;
            Testing::paintLevels(lone, 3072, 3072, 1, "lone");
            lone.mData.mSlot = TextureIndex(1);

            const Testing::NoRoomForContent full(device);
            EXPECT_EQ(textures.getHeld().mReduced + textures.getHeld().mShortOfRoom, 0u)
                << "an array that stands nothing held something back";

            const Testing::BudgetLimit oneBlock(
                memory, Testing::budgetAbove(memory, MemoryUse::Texture, VkDeviceSize{ 65 } << 20));
            Result<Image, MemoryRefusal> opener = Image::tryMake(MemoryUse::Texture, device,
                ImageDescription{
                    .mWidth = 32, .mHeight = 32, .mFormat = VK_FORMAT_R16_UNORM, .mUsage = VK_IMAGE_USAGE_SAMPLED_BIT },
                "block opener");
            ASSERT_TRUE(opener.isOk()) << "the image that opens the block";

            DeviceAnswer answer;

            std::vector<Refusal>& refused = answer.mRefused;
            {
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&ladder.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty()) << "a texture with a level the device had room for was refused";
            EXPECT_EQ(textures.getRoomHeld(), 0u) << "the room chose the level, and not the device";
            EXPECT_EQ(textures.getHeld().mBytes, VkDeviceSize{ 1536 } * 1536 * 4);
            EXPECT_EQ(textures.getHeld().mReduced, 1u);
            EXPECT_GT(textures.getHeld().mReduced, 0u) << "a texture stood smaller than its file and held nothing back";
            EXPECT_EQ(answer.mShort,
                (std::vector<ShortTexture>{
                    { .mSlot = ladder.mData.mSlot, .mStoodBytes = VkDeviceSize{ 1536 } * 1536 * 4 } }))
                << "a texture the room held smaller was not named short, with the bytes it stands";
            const auto requirementsOf = [&](const TextureData& data) {
                return Image::requirementsFor(device, Texture::describeFile(data, 0));
            };
            EXPECT_EQ(answer.mWants,
                (std::vector<RoomWant>{ { .mWaiter = ladder.mData.mSlot,
                    .mHole = requirementsOf(ladder.mData).size,
                    .mAlignment = requirementsOf(ladder.mData).alignment } }));
            EXPECT_EQ(answer.mTextureFits,
                (std::vector<FitBytes<TextureIndex>>{ { .mSlot = ladder.mData.mSlot,
                    .mBytes = Image::bytesFor(device, Texture::describeFile(ladder.mData, 1)),
                    .mAlignment = textures.getImage(ladder.mData.mSlot).getMemoryAlignment() } }))
                << "what the ladder's second level gives back once it rests";
            answer.mShort.clear();
            answer.mWants.clear();
            answer.mTextureFits.clear();

            {
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&lone.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
            }
            ASSERT_EQ(refused.size(), 1u);
            EXPECT_EQ(refused[0].mKind, Refused::Texture);
            EXPECT_EQ(refused[0].mName, "lone");
            EXPECT_EQ(refused[0].mWhy, "no device memory is left for it");
            EXPECT_EQ(textures.getHeld().mCount, 1u) << "a texture with no room stood";
            EXPECT_EQ(textures.getTexels(TextureIndex(0)), 1536u * 1536u);
            EXPECT_EQ(textures.getTexels(TextureIndex(1)), Shaders::TEXTURE_STANDS_IN | 16u) << "no room";
            EXPECT_EQ(textures.getExtent(TextureIndex(0)), Shaders::uvec2(1536u, 1536u)) << "the level the room took";
            EXPECT_EQ(textures.getHeld().mShortOfRoom, 1u) << "refused for want of room";

            // **Where room is coming, the file waits rather than being refused**: named by its slot among those the
            // core hands over again, drawing the stand-in meanwhile, and no refusal.
            refused.clear();
            {
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&lone.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Waits, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty()) << "a file room is coming for was refused";
            EXPECT_EQ(answer.mShort, (std::vector<ShortTexture>{ { .mSlot = lone.mData.mSlot, .mStoodBytes = 0 } }));
            EXPECT_EQ(answer.mWants,
                (std::vector<RoomWant>{ { .mWaiter = lone.mData.mSlot,
                    .mHole = requirementsOf(lone.mData).size,
                    .mAlignment = requirementsOf(lone.mData).alignment } }));
            EXPECT_TRUE(answer.mTextureFits.empty()) << "the stand-in gave back a range";
            EXPECT_EQ(textures.getTexels(TextureIndex(1)), Shaders::TEXTURE_STANDS_IN | 16u)
                << "a file that waits drew its own";
            EXPECT_EQ(textures.getHeld().mShortOfRoom, 1u) << "waiting, and counted once";
            answer.mShort.clear();
            answer.mWants.clear();

            refused.clear();
            {
                const Testing::BudgetLimit ample(memory, ~VkDeviceSize{ 0 });
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&lone.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty());
            EXPECT_TRUE(answer.mWants.empty()) << "a file stood whole waits";
            EXPECT_EQ(answer.mTextureFits,
                (std::vector<FitBytes<TextureIndex>>{ { .mSlot = lone.mData.mSlot,
                    .mBytes = Image::bytesFor(device, Texture::describeFile(lone.mData, 0)),
                    .mAlignment = textures.getImage(lone.mData.mSlot).getMemoryAlignment() } }));
            EXPECT_EQ(textures.getHeld().mCount, 2u);
            EXPECT_EQ(textures.getHeld().mReduced, 1u) << "a texture standing as its file was counted as smaller";
            EXPECT_EQ(textures.getTexels(TextureIndex(1)), 3072u * 3072u)
                << "a slot that stands at last still says the stand-in";
            EXPECT_EQ(textures.getExtent(TextureIndex(1)), Shaders::uvec2(3072u, 3072u)) << "and still measures it";

            // **What the room held back is what still stands short of it**: the single level stands whole, no slot
            // is short of room, and once the ladder stands whole again nothing is smaller than its file either, and
            // the array says nothing was held back.
            EXPECT_EQ(textures.getHeld().mShortOfRoom, 0u) << "the single level stood whole at last";
            {
                const Testing::BudgetLimit ample(memory, ~VkDeviceSize{ 0 });
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&ladder.mData, 1), TextureArray::noWants(), answer,
                    WhenRoomless::Refuses, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty());
            EXPECT_EQ(textures.getHeld().mReduced, 0u);
            EXPECT_EQ(textures.getHeld().mShortOfRoom, 0u);
            EXPECT_EQ(textures.getHeld().mReduced + textures.getHeld().mShortOfRoom, 0u)
                << "a refusal kept past the slots that went without";

            // Before the array goes: what the writes replaced is buried, and the fillers give their
            // room back after it.
            device.waitIdle();
            device.collectIdle();
        }
    }
}
