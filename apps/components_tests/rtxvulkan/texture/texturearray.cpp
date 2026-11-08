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
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/device/heldsubmit.hpp>
#include <apps/components_tests/rtx/support/device/memorylimits.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
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

        /// Writing a set's descriptors waits for the submit that bound the set, and for nothing
        /// else.
        ///
        /// **The one per-copy resource that is not a buffer.** A set is bound by handle, so no
        /// address names it, and its bindings allow an update after a bind — but not of a
        /// descriptor a pending trace samples, and a trace samples whichever slots its materials
        /// name. `getSet` is the hand-out and stamps the set the way `Buffer::addressFor` stamps a
        /// buffer; `finishReads` waits for that stamp. Held on the queue while this thread waits,
        /// so the wait cannot return before the hold opens and lasts at least the hold's length.
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
            const TextureData arrived = Testing::describeTexel(white, 0);
            Batch arrival(pool);
            std::vector<Refusal> refused;
            textures.write(arrival, passes, rooms, std::span(&arrived, 1), refused, nullptr);
            EXPECT_TRUE(refused.empty());
            arrival.flush();
            Testing::expectAssertDies([&] { static_cast<void>(textures.getSet(FrameSlot{ 0 })); },
                "a texture set sampled before `sync` paid what it owes");
            textures.sync(FrameSlot{ 0 });
            textures.sync(FrameSlot{ 1 });

            Testing::HeldSubmit hold(device);
            const LentCommands binder = pool.lend(1);
            Recording binding = pool.begin(binder[0]);
            EXPECT_NE(textures.getSet(FrameSlot{ 0 }), VK_NULL_HANDLE);

            // A second arrival, owed to both again once the first set is bound: what `sync` has to
            // write once that set is free. Deferred, so it rides ahead of the held submit.
            const TextureData second = Testing::describeTexel(white, 1);
            Batch secondArrival(pool);
            textures.write(secondArrival, passes, rooms, std::span(&second, 1), refused, nullptr);
            EXPECT_TRUE(refused.empty());
            secondArrival.defer();
            hold.submit(std::move(binding));

            // Long enough that a wait which returned at once is told from one that waited, under three
            // shards of this binary sharing the device.
            constexpr std::chrono::milliseconds held{ 200 };
            const auto asked = std::chrono::steady_clock::now();
            hold.releaseAfter(held);

            textures.finishReads(FrameSlot{ 0 });
            EXPECT_GE(std::chrono::steady_clock::now() - asked, held) << "the sync did not wait for the set's binder";

            // A wait that returned early writes this into a set a submit the hold still keeps on
            // the queue has bound, which is the write the assert fires on.
            textures.sync(FrameSlot{ 0 });

            // And the other set, which nothing bound, waits for nothing.
            const auto other = std::chrono::steady_clock::now();
            textures.finishReads(FrameSlot{ 1 });
            EXPECT_LT(std::chrono::steady_clock::now() - other, held);
            textures.sync(FrameSlot{ 1 });
        }

        /// An arrival of one file is held to the largest side it fits the room at, the level a
        /// file begins at: a file from its first level within the side, a bake as large as its
        /// source is held to, and a composite, whose side is the renderer's own, at what it costs.
        /// Where the arrival fits at no side, its files are held to the side they fit at without
        /// the composite, which gives way; and where they fit at none, each begins at its last
        /// level.
        ///
        /// **Hand-computed.** The ladder is 64, 32 and 16 texels square, four bytes a texel: 16384,
        /// 4096 and 1024 bytes, 21504 from the top, 5120 from the second level and 1024 from the
        /// third, and a colour brings nothing beside it. The ladder alone is 21504 bytes whole, 5120
        /// held to 32 and 1024 held to 16, and no smaller for anything below 16, where it has no
        /// level. A bake of it is four bytes a texel over the levels the ladder stands with — 5376
        /// texels from 64 and 1280 from 32, which is what the ladder itself comes to. A composite is
        /// 512 square in BC7 with its chain, a block of sixteen bytes a 4 by 4 and one each for the two
        /// levels under a block: 16384 + 4096 + 1024 + 256 + 64 + 16 + 4 + 1 + 1 + 1 = 21847 blocks,
        /// 349552 bytes, whatever the side. The scene's first composite brings the canvas it is baked
        /// on, a chain of 349525 texels at four bytes, and the blocks it is encoded through, 1398100 +
        /// 349552 = 1747652, and 2097204 with the composite — so a byte short of it and the ladder's
        /// least, the arrival fits nowhere and the ladder is held as though the composite were not
        /// there.
        ///
        /// **The companions and what a texture is made through count too.** The ladder as a normal
        /// map brings a spread of half its side to one texel, a byte a texel, and the sixteen-byte
        /// means it is built through, which are the arrival's room and priced once as what it grows
        /// by — all of it here, where the array holds none yet: from 64, 32 square down is 1365
        /// texels, 1365 + 21840 bytes beside the 21504, 44709 in all; from 32, 341 texels and 5120,
        /// 10917; from 16, 85 texels and 1024, 2469. A one-level 64 square is its level, 16384,
        /// whatever the side, because the file has no smaller level, and a bake of it the same again.
        TEST_F(RtxTextureArrayTest, anArrivalIsHeldToTheLargestSideItFitsTheRoomAt)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            const TextureArray textures(device, setup, layout, passes, rooms, 3);
            setup.flush();

            Testing::TestTexture ladder;
            Testing::paintLevels(ladder, 64, 64, 3, "ladder");
            const TextureData composite{ .mSlot = 1, .mSource = TextureSource::GroundComposite, .mFrom = 0 };
            const TextureData bake{ .mSlot = 2, .mSource = TextureSource::SpriteBake, .mFrom = 0 };

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
                VkDeviceSize mRoom;
                std::uint32_t mFirst;
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

        /// **The file that stands the most bytes comes down a level first, and of two that stand
        /// alike the later slot, until the arrival fits.** Two of the ladders of
        /// `anArrivalIsHeldToTheLargestSideItFitsTheRoomAt`, 21504 bytes whole, 5120 from the second
        /// level and 1024 from the third: in 21504 + 5120 bytes the second slot comes down one and
        /// the first stands whole, where one side for both held both to 32 and left 16384 unused; a
        /// byte short, both come down one; in 5120 + 1024, the second comes down two. A 64-square
        /// ladder beside a 32-square one, 5120 whole and 1024 from its second level, in 21504 + 1024
        /// bytes: the large one comes down, though it is the earlier slot, and the small one stands
        /// whole, where taking the later slot first would have stood the large whole.
        TEST_F(RtxTextureArrayTest, theFileThatStandsTheMostComesDownFirstUntilTheArrivalFits)
        {
            Device& device = getDevice();
            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(getPool());
            const TextureArray textures(device, setup, layout, passes, rooms, 2);
            setup.flush();

            Testing::TestTexture first;
            Testing::paintLevels(first, 64, 64, 3, "first");
            Testing::TestTexture second;
            Testing::paintLevels(second, 64, 64, 3, "second");
            second.mData.mSlot = 1;
            Testing::TestTexture small;
            Testing::paintLevels(small, 32, 32, 2, "small");
            small.mData.mSlot = 1;

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
        /// `anArrivalIsHeldToTheLargestSideItFitsTheRoomAt` costs 44709 at the top, 21840 of it means,
        /// where the array holds none: a byte short of that and it is held to 32. Once one such map
        /// has arrived, the room holds its 21840 and the next pays none of it: 22869 is the top, and a
        /// byte short of it 32, in that array and in any other the room is handed to.
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
            second.mData.mSlot = 1;

            std::vector<std::uint32_t> firsts;
            const auto firstOf = [&](const TextureArray& array, const TextureData& texture, const VkDeviceSize room) {
                array.chooseLevels(std::span(&texture, 1), room, rooms, firsts);
                return firsts.at(0);
            };
            EXPECT_EQ(firstOf(textures, first.mData, 44709), 0u);
            EXPECT_EQ(firstOf(textures, first.mData, 44708), 1u);

            std::vector<Refusal> refused;
            Batch arrival(getPool());
            textures.write(arrival, passes, rooms, std::span(&first.mData, 1), refused, nullptr);
            arrival.flush();
            ASSERT_TRUE(refused.empty());

            EXPECT_EQ(firstOf(textures, second.mData, 44709 - 21840), 0u)
                << "the means the room holds were priced again";
            EXPECT_EQ(firstOf(textures, second.mData, 44709 - 21840 - 1), 1u);

            // **And so is another scene's array**: the rooms are the renderer's, one for every scene.
            Batch other(getPool());
            const TextureArray elsewhere(device, other, layout, passes, rooms, 2);
            other.flush();
            EXPECT_EQ(firstOf(elsewhere, second.mData, 44709 - 21840), 0u)
                << "a second scene priced the means the shared room holds";
        }

        /// A texture past the side the device takes stands from its first level within the side,
        /// and one with no level within it draws the stand-in, refused and saying why.
        ///
        /// **One texel wider than the device takes**, so the case is the device's own and no
        /// larger than a row: the second level is half that, within the side, and is what stands —
        /// four bytes a texel.
        ///
        /// **The slot's texel word says which stands**: the level's count, and the stand-in's four by
        /// four with `TEXTURE_STANDS_IN` over it — as it does for a slot described as the stand-in,
        /// which is how the builder describes a file that does not read.
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

            Testing::TestTexture wide;
            Testing::paintLevels(wide, limit + 1, 1, 2, "wide");
            Testing::TestTexture single;
            Testing::paintLevels(single, limit + 1, 1, 1, "single");
            single.mData.mSlot = 1;
            Testing::TestTexture unread;
            Testing::paintLevels(unread, 4, 4, 1, "unread");
            unread.mData.mSlot = 2;
            unread.mData.mSource = TextureSource::StandIn;

            const std::array arrived{ wide.mData, single.mData, unread.mData };
            std::vector<Refusal> refused;
            Batch arrival(getPool());
            textures.write(arrival, passes, rooms, arrived, refused, nullptr);
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

            EXPECT_EQ(textures.getTexels(0), (limit + 1) >> 1) << "the level that stands";
            EXPECT_EQ(textures.getTexels(1), Shaders::TEXTURE_STANDS_IN | 16u) << "past the side";
            EXPECT_EQ(textures.getTexels(2), Shaders::TEXTURE_STANDS_IN | 16u) << "described as the stand-in";

            // Across and down apart, which a texel count is not: the second level of a row past the
            // side is half the row by the one texel a level never goes under.
            EXPECT_EQ(textures.getExtent(0), Shaders::uvec2((limit + 1) >> 1, 1u)) << "the level that stands";
            EXPECT_EQ(textures.getExtent(1), Shaders::uvec2(4u, 4u)) << "the stand-in's, past the side";

            // **The totals follow a slot given back**, which they are kept by and not walked for:
            // the one texture that stood is dropped, and nothing stands, nothing is smaller, and
            // nothing costs. Dropping the stand-in's slot as well takes nothing off.
            textures.drop(std::array{ 0u, 1u });
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
            const TextureData bake{
                .mSlot = 1, .mSource = TextureSource::SpriteBake, .mFrom = 0, .mFormat = TextureFormat::Rgba8Unorm
            };
            {
                Batch again(getPool());
                textures.write(again, passes, rooms, std::array{ lone.mData, bake }, refused, nullptr);
                again.flush();
            }
            const TexturesHeld bySource = textures.getHeld();
            EXPECT_EQ(bySource.mBySource.mFiles, 16384u);
            EXPECT_EQ(bySource.mBySource.mSpriteBakes, 16384u);
            EXPECT_EQ(bySource.mBytes, 2u * 16384u);
        }

        /// A texture the device has no room for comes down a level at a time, and one it has room
        /// for at no level draws the stand-in, refused and saying why.
        ///
        /// **Room for one block, and every gap filled.** The ladder's first level, 3072 square at
        /// four bytes, is 36 MiB, past half a block and so an allocation of its own; a small image the
        /// test holds comes first and opens the block, which leaves a megabyte under the ceiling, and
        /// the level is refused although the side chosen for the arrival counted the block as room for
        /// it. Its second level, 1536 square, is 9 MiB and fits the block: 9437184 bytes.
        /// The single level of the same 36 MiB has nothing further down. The slot's texel word says
        /// the stand-in while it stands in, and is the file's own count once it stands.
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
            lone.mData.mSlot = 1;

            const Testing::NoRoomForContent full(device);
            EXPECT_FALSE(textures.wasHeldBack()) << "an array that stands nothing held something back";

            const Testing::BudgetLimit oneBlock(
                memory, Testing::budgetAbove(memory, MemoryUse::Texture, VkDeviceSize{ 65 } << 20));
            Misc::Result<Image, std::string_view> opener = Image::tryMake(MemoryUse::Texture, device,
                ImageDescription{
                    .mWidth = 32, .mHeight = 32, .mFormat = VK_FORMAT_R16_UNORM, .mUsage = VK_IMAGE_USAGE_SAMPLED_BIT },
                "block opener");
            ASSERT_TRUE(opener.isOk()) << "the image that opens the block";

            std::vector<Refusal> refused;
            {
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&ladder.mData, 1), refused, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty()) << "a texture with a level the device had room for was refused";
            EXPECT_EQ(textures.getRoomHeld(), 0u) << "the room chose the level, and not the device";
            EXPECT_EQ(textures.getHeld().mBytes, VkDeviceSize{ 1536 } * 1536 * 4);
            EXPECT_EQ(textures.getHeld().mReduced, 1u);
            EXPECT_TRUE(textures.wasHeldBack()) << "a texture stood smaller than its file and held nothing back";

            {
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&lone.mData, 1), refused, nullptr);
                arrival.flush();
            }
            ASSERT_EQ(refused.size(), 1u);
            EXPECT_EQ(refused[0].mKind, Refused::Texture);
            EXPECT_EQ(refused[0].mName, "lone");
            EXPECT_EQ(refused[0].mWhy, "no device memory is left for it");
            EXPECT_EQ(textures.getHeld().mCount, 1u) << "a texture with no room stood";
            EXPECT_EQ(textures.getTexels(0), 1536u * 1536u);
            EXPECT_EQ(textures.getTexels(1), Shaders::TEXTURE_STANDS_IN | 16u) << "no room";
            EXPECT_EQ(textures.getExtent(0), Shaders::uvec2(1536u, 1536u)) << "the level the room took";

            refused.clear();
            {
                const Testing::BudgetLimit ample(memory, ~VkDeviceSize{ 0 });
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&lone.mData, 1), refused, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty());
            EXPECT_EQ(textures.getHeld().mCount, 2u);
            EXPECT_EQ(textures.getHeld().mReduced, 1u) << "a texture standing as its file was counted as smaller";
            EXPECT_EQ(textures.getTexels(1), 3072u * 3072u) << "a slot that stands at last still says the stand-in";
            EXPECT_EQ(textures.getExtent(1), Shaders::uvec2(3072u, 3072u)) << "and still measures it";

            // **A refusal is kept past the slot that went without**: the ladder stood again whole
            // leaves nothing smaller than its file, and the array still says the room held it back.
            {
                const Testing::BudgetLimit ample(memory, ~VkDeviceSize{ 0 });
                Batch arrival(getPool());
                textures.write(arrival, passes, rooms, std::span(&ladder.mData, 1), refused, nullptr);
                arrival.flush();
            }
            EXPECT_TRUE(refused.empty());
            EXPECT_EQ(textures.getHeld().mReduced, 0u);
            EXPECT_TRUE(textures.wasHeldBack()) << "a refusal was forgotten once its slot stood";

            // Before the array goes: what the writes replaced is buried, and the fillers give their
            // room back after it.
            device.waitIdle();
            device.collectIdle();
        }
    }
}
