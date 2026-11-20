#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/rowspan.hpp>
#include <components/rtx/common/slotledger.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t sKiB = 1024;
        constexpr std::uint64_t sMiB = 1024 * sKiB;
        constexpr ArenaIndex sImages{ 0 };
        constexpr ArenaIndex sBuffers{ 1 };

        /// Images and buffers in two arenas, as a driver that gives them two memory types has them.
        SlotLayout twoArenas()
        {
            SlotLayout layout;
            layout.mArenaOf = { sImages, sBuffers };
            return layout;
        }

        constexpr DeviceEpoch epoch(const std::uint64_t value)
        {
            return DeviceEpoch{ .mValue = value };
        }

        /// A chunk asked and arrived.
        BlockIndex chunkOf(SlotLedger& ledger, const ArenaIndex arena)
        {
            const BlockIndex chunk = ledger.askChunk(arena);
            ledger.blockArrived(chunk);
            return chunk;
        }

        std::uint64_t bytesFor(
            const SlotLedger& ledger, const ArenaIndex arena, const std::vector<std::uint32_t>& counts)
        {
            return ledger.bytesFor(arena, RowSpan<const std::uint32_t, ClassIndex>(std::span(counts)));
        }

        /// **A stride is the size rounded up a ladder of eight steps an octave, in units of 256 bytes, then up to its
        /// alignment**: one byte and 256 are a unit; 257 two; eight units are exact and nine too, the ladder's first
        /// octave counting a unit a step; seventeen units round up to eighteen, a step of two; 4097 units, past 4096,
        /// to 4608, a step of 512, 11% over; 300 bytes at an alignment of 4096 are 4096. At each step, a byte past it
        /// is the next.
        TEST(RtxSlotLedgerTest, aStrideIsTheSizeRoundedUpTheLadderThenTheAlignment)
        {
            const SlotLayout layout;
            EXPECT_EQ(SlotLedger::strideFor(layout, 1, 1), 256u);
            EXPECT_EQ(SlotLedger::strideFor(layout, 256, 1), 256u);
            EXPECT_EQ(SlotLedger::strideFor(layout, 257, 1), 512u);
            EXPECT_EQ(SlotLedger::strideFor(layout, 8 * 256, 1), 8u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 8 * 256 + 1, 1), 9u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 16 * 256 + 1, 1), 18u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 18 * 256, 1), 18u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 18 * 256 + 1, 1), 20u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 4097 * 256, 1), 4608u * 256);
            EXPECT_EQ(SlotLedger::strideFor(layout, 300, 4096), 4096u);
            EXPECT_EQ(SlotLedger::strideFor(layout, 0, 1), 256u) << "nothing still takes a unit";
        }

        /// **A class is its kind and its stride, and its tier and slots a slab follow from the stride**: 512 KiB is
        /// small, eight to a 4 MiB slab; a byte more rounds to 576 KiB, medium, 113 to a 64 MiB chunk; 8 MiB is medium,
        /// eight a chunk; a byte past it is large, a block of its own. The same size and kind is one class, another
        /// kind another, and each kind's class stands in its own arena.
        TEST(RtxSlotLedgerTest, aClassIsItsKindAndStrideAndItsTierFollows)
        {
            SlotLedger ledger(twoArenas());
            const ClassIndex small = ledger.classOf(SlotKind::Image, 512 * sKiB, 1);
            EXPECT_EQ(ledger.getTier(small), SlabTier::Small);
            EXPECT_EQ(ledger.getSlotsPerSlab(small), 8u);

            const ClassIndex justMedium = ledger.classOf(SlotKind::Image, 512 * sKiB + 1, 1);
            EXPECT_EQ(ledger.getStrideBytes(justMedium), 576 * sKiB);
            EXPECT_EQ(ledger.getTier(justMedium), SlabTier::Medium);
            EXPECT_EQ(ledger.getSlotsPerSlab(justMedium), 113u);

            const ClassIndex medium = ledger.classOf(SlotKind::Image, 8 * sMiB, 1);
            EXPECT_EQ(ledger.getTier(medium), SlabTier::Medium);
            EXPECT_EQ(ledger.getSlotsPerSlab(medium), 8u);

            const ClassIndex large = ledger.classOf(SlotKind::Image, 8 * sMiB + 1, 1);
            EXPECT_EQ(ledger.getTier(large), SlabTier::Large);
            EXPECT_EQ(ledger.getSlotsPerSlab(large), 1u);

            EXPECT_EQ(ledger.classOf(SlotKind::Image, 500 * sKiB, 1), small) << "the same stride";
            const ClassIndex buffer = ledger.classOf(SlotKind::Buffer, 512 * sKiB, 1);
            EXPECT_NE(buffer, small);
            EXPECT_EQ(ledger.getArena(small), sImages);
            EXPECT_EQ(ledger.getArena(buffer), sBuffers);
            EXPECT_EQ(ledger.getClassCount(), 5u);
        }

        /// **A reserve fills the fullest slab, and a slab goes back to the pool as its last slot is collected**: a
        /// class of sixteen slots a slab. Seventeen reserved fill one slab and open a second, slot 0 of each first, at
        /// offsets of its stride into its place. Committed and retired at epoch 5, nothing is free at epoch 4; at 5 all
        /// are, both slabs return and the chunk is free again. A slot freed in the first slab while the second has
        /// room sends the next reserve to the fuller first.
        TEST(RtxSlotLedgerTest, aReserveFillsTheFullestSlabAndASlabReturnsAsItsLastSlotIsCollected)
        {
            SlotLedger ledger(twoArenas());
            const ClassIndex of = ledger.classOf(SlotKind::Image, 256 * sKiB, 1);
            ASSERT_EQ(ledger.getSlotsPerSlab(of), 16u);
            EXPECT_FALSE(ledger.hasRoom(of)) << "no chunk";
            const BlockIndex chunk = chunkOf(ledger, sImages);
            EXPECT_EQ(ledger.freeBlock(sImages), std::optional<BlockIndex>(chunk));
            ASSERT_TRUE(ledger.hasRoom(of));

            std::vector<Slot> slots;
            slots.reserve(17);
            for (std::uint32_t at = 0; at < 17; ++at)
                slots.push_back(ledger.reserve(of, SlotOwner{ at }));
            EXPECT_EQ(slots[0].mAt, 0u);
            EXPECT_EQ(slots[15].mAt, 15u);
            EXPECT_EQ(slots[16].mAt, 0u);
            EXPECT_NE(slots[16].mSlab, slots[0].mSlab);
            EXPECT_EQ(ledger.getOffsetOf(slots[15]), 15 * 256 * sKiB);
            EXPECT_EQ(ledger.getOffsetOf(slots[16]), 4 * sMiB) << "the chunk's second slab";
            EXPECT_EQ(ledger.getBlockOf(slots[16]), chunk);
            EXPECT_EQ(ledger.getOwnerOf(slots[7]), SlotOwner{ 7 });
            EXPECT_EQ(ledger.freeBlock(sImages), std::nullopt);
            EXPECT_EQ(ledger.getBytesInUse(sImages), 64 * sMiB);

            ledger.commit(slots[3]);
            ledger.leave(slots[3]);
            ledger.retire(slots[3], epoch(2));
            ledger.collect(epoch(2));
            EXPECT_EQ(ledger.reserve(of, SlotOwner{ 99 }), slots[3]) << "the fuller slab first";

            for (const Slot slot : slots)
                ledger.commit(slot);
            for (const Slot slot : slots)
            {
                ledger.leave(slot);
                ledger.retire(slot, epoch(5));
            }
            ledger.collect(epoch(4));
            EXPECT_EQ(ledger.count().mSlotsRetiring, 17u);
            EXPECT_EQ(ledger.count().mSlotsLive, 0u);
            ledger.collect(epoch(5));
            const SlotCounts counts = ledger.count();
            EXPECT_EQ(counts.mSlotsRetiring, 0u);
            EXPECT_EQ(counts.mSlabsInClass, 0u);
            EXPECT_EQ(counts.mSlabsFree, 16u);
            EXPECT_EQ(ledger.freeBlock(sImages), std::optional<BlockIndex>(chunk));
            EXPECT_TRUE(ledger.isConsistent());
        }

        /// **What a plan stands in is its small slabs packed sixteen a chunk, a chunk a medium slab and a block a large
        /// slot, in its arena alone**: seventeen of sixteen a slab are two slabs, one of 4 KiB one more; three small
        /// slabs are one chunk, 64 MiB. Seventeen of a 4 MiB medium class, sixteen a chunk, are two chunks; two of
        /// 16 MiB, large, 32 MiB. 224 MiB. The other arena's class adds nothing, and nought slots stand in nothing.
        TEST(RtxSlotLedgerTest, aPlanStandsInItsSlabsPackedIntoChunksAndItsLargeBlocks)
        {
            SlotLedger ledger(twoArenas());
            const ClassIndex sixteen = ledger.classOf(SlotKind::Image, 256 * sKiB, 1);
            const ClassIndex tiny = ledger.classOf(SlotKind::Image, 4 * sKiB, 1);
            const ClassIndex medium = ledger.classOf(SlotKind::Image, 4 * sMiB, 1);
            const ClassIndex large = ledger.classOf(SlotKind::Image, 16 * sMiB, 1);
            const ClassIndex buffer = ledger.classOf(SlotKind::Buffer, 4 * sKiB, 1);
            ASSERT_EQ(ledger.getSlotsPerSlab(medium), 16u);

            std::vector<std::uint32_t> counts(5, 0);
            counts[sixteen.at()] = 17;
            counts[tiny.at()] = 1;
            counts[medium.at()] = 17;
            counts[large.at()] = 2;
            counts[buffer.at()] = 1000;
            EXPECT_EQ(bytesFor(ledger, sImages, counts), 224 * sMiB);
            EXPECT_EQ(bytesFor(ledger, sBuffers, counts), 64 * sMiB);
            EXPECT_EQ(bytesFor(ledger, sImages, std::vector<std::uint32_t>(5, 0)), 0u);
            EXPECT_EQ(bytesFor(ledger, sImages, { 16 }), 64 * sMiB) << "counts shorter than the classes";
        }

        /// **A medium class takes a whole chunk and gives it back; a large class a block of its own, which may be
        /// refused**: a medium slot takes the one free chunk, and a small class then has no room; freed, the chunk is
        /// free again. A large block asked, refused, is gone and its class still has no room; asked again and arrived,
        /// one slot stands in it, and freed, the block is free to give back.
        TEST(RtxSlotLedgerTest, aMediumClassTakesAChunkAndALargeOneABlockThatMayBeRefused)
        {
            SlotLedger ledger(twoArenas());
            const ClassIndex medium = ledger.classOf(SlotKind::Image, 4 * sMiB, 1);
            const ClassIndex small = ledger.classOf(SlotKind::Image, 4 * sKiB, 1);
            const BlockIndex chunk = chunkOf(ledger, sImages);
            const Slot slot = ledger.reserve(medium, SlotOwner{ 1 });
            EXPECT_EQ(ledger.getBlockOf(slot), chunk);
            EXPECT_EQ(ledger.getOffsetOf(slot), 0u);
            EXPECT_FALSE(ledger.hasRoom(small)) << "the chunk is the medium slab's";
            EXPECT_TRUE(ledger.hasRoom(medium)) << "fifteen more slots in its slab";
            ledger.unreserve(slot);
            EXPECT_EQ(ledger.freeBlock(sImages), std::optional<BlockIndex>(chunk));
            EXPECT_TRUE(ledger.hasRoom(small));

            const ClassIndex large = ledger.classOf(SlotKind::Image, 20 * sMiB, 1);
            EXPECT_EQ(ledger.getChunkCount(sImages), 1u);
            EXPECT_EQ(ledger.getLargeBlockCount(large), 0u);
            const BlockIndex refused = ledger.askLarge(large);
            EXPECT_EQ(ledger.getLargeBlockCount(large), 1u) << "asked counts";
            EXPECT_EQ(ledger.getChunkCount(sImages), 1u) << "a large block is no chunk";
            EXPECT_EQ(ledger.getBlockBytes(refused), 20 * sMiB);
            EXPECT_EQ(ledger.count().mBlocksAsked, 1u);
            EXPECT_FALSE(ledger.hasRoom(large)) << "asked is not arrived";
            ledger.blockRefused(refused);
            EXPECT_EQ(ledger.count().mBlocksAsked, 0u);
            EXPECT_EQ(ledger.getLargeBlockCount(large), 0u) << "refused is gone";
            EXPECT_FALSE(ledger.hasRoom(large));

            const BlockIndex block = ledger.askLarge(large);
            EXPECT_EQ(block, refused) << "the row of a block refused is named again";
            ledger.blockArrived(block);
            ASSERT_TRUE(ledger.hasRoom(large));
            const Slot one = ledger.reserve(large, SlotOwner{ 2 });
            EXPECT_EQ(ledger.getBlockOf(one), block);
            EXPECT_FALSE(ledger.hasRoom(large));
            ledger.commit(one);
            ledger.leave(one);
            ledger.retire(one, epoch(1));
            ledger.collect(epoch(1));
            EXPECT_TRUE(ledger.hasRoom(large)) << "its block emptied is its class's again";
            ledger.blockReleased(chunk);
            EXPECT_EQ(ledger.getChunkCount(sImages), 0u);
            EXPECT_EQ(ledger.freeBlock(sImages), std::optional<BlockIndex>(block));
            ledger.blockReleased(block);
            EXPECT_EQ(ledger.getLargeBlockCount(large), 0u);
            EXPECT_EQ(ledger.count().mBlocksHeld, 0u);
            EXPECT_TRUE(ledger.isConsistent());
        }

        /// **A drain is of what holds the fewest live bytes and fits elsewhere, and takes no new slot**: two chunks
        /// full of small slabs of one class, sixteen 256 KiB slots a slab, one chunk filled whole and the other's first
        /// slab holding three; nine slots freed across the full chunk's slabs give the three room. A small slab wanted
        /// with none free drains the one of three, 768 KiB, which then takes no reserve; its three move to slots of
        /// the full chunk, the old retired, and once collected the slab is free. A whole chunk wanted drains the chunk
        /// whose content fits the other's free room.
        TEST(RtxSlotLedgerTest, aDrainIsOfTheFewestLiveBytesThatFitElsewhereAndTakesNoSlot)
        {
            SlotLayout layout = twoArenas();
            layout.mChunkBytes = 8 * sMiB;
            layout.mMediumStrideBytesMost = sMiB;
            SlotLedger ledger(layout);
            const ClassIndex of = ledger.classOf(SlotKind::Image, 256 * sKiB, 1);
            const BlockIndex full = chunkOf(ledger, sImages);
            std::vector<Slot> held;
            held.reserve(32);
            for (std::uint32_t at = 0; at < 32; ++at)
                held.push_back(ledger.reserve(of, SlotOwner{ at }));
            const BlockIndex other = chunkOf(ledger, sImages);
            std::vector<Slot> few;
            few.reserve(3);
            for (std::uint32_t at = 0; at < 3; ++at)
                few.push_back(ledger.reserve(of, SlotOwner{ 100 + at }));
            for (const Slot slot : held)
                ledger.commit(slot);
            for (const Slot slot : few)
                ledger.commit(slot);
            for (std::size_t at = 0; at < 9; ++at)
            {
                ledger.leave(held[at * 3]);
                ledger.retire(held[at * 3], epoch(1));
            }
            ledger.collect(epoch(1));
            ASSERT_EQ(ledger.getBlockOf(few[0]), other);

            EXPECT_EQ(ledger.drainFor(sImages, SlabTier::Small), std::nullopt)
                << "a free slab stands in the other chunk";
            const ClassIndex filler = ledger.classOf(SlotKind::Image, 128 * sKiB, 1);
            const Slot fills = ledger.reserve(filler, SlotOwner{ 500 });
            ledger.commit(fills);
            ASSERT_EQ(ledger.getBlockOf(fills), other);
            ASSERT_EQ(ledger.count().mSlabsFree, 0u);

            const std::optional<Drain> drain = ledger.drainFor(sImages, SlabTier::Small);
            ASSERT_TRUE(drain.has_value());
            EXPECT_EQ(drain->mSlab, few[0].mSlab);
            EXPECT_EQ(drain->mLiveBytes, 3 * 256 * sKiB);
            EXPECT_EQ(ledger.count().mSlabsDraining, 1u);
            const std::optional<Slot> next = ledger.nextToMove();
            ASSERT_TRUE(next.has_value());
            EXPECT_EQ(*next, few[0]);
            EXPECT_EQ(ledger.getOwnerOf(*next), SlotOwner{ 100 });

            // **A slot left is not moved again** before the epoch it is read in is known: each of the three is the
            // next to move once, and once all three left, nothing is, nor must a drain move them.
            for (const Slot slot : few)
            {
                EXPECT_EQ(ledger.nextToMove(), slot);
                const Slot to = ledger.reserve(of, ledger.getOwnerOf(slot));
                EXPECT_EQ(ledger.getBlockOf(to), full) << "a draining slab takes no new slot";
                ledger.commit(to);
                ledger.leave(slot);
            }
            EXPECT_EQ(ledger.nextToMove(), std::nullopt) << "what leaves moves by itself";
            EXPECT_EQ(ledger.count().mSlotsRetiring, 3u) << "a slot leaving counted as retiring";
            for (const Slot slot : few)
                ledger.retire(slot, epoch(2));
            ledger.collect(epoch(2));
            EXPECT_EQ(ledger.count().mSlabsDraining, 0u);
            EXPECT_EQ(ledger.count().mSlabsFree, 1u);
            EXPECT_TRUE(ledger.isConsistent());

            // No chunk empties: the full one's two slabs want two free slabs elsewhere and one stands, and the other's
            // filler has no room of its class outside it.
            EXPECT_EQ(ledger.drainFor(sImages, SlabTier::Medium), std::nullopt);
            EXPECT_EQ(ledger.count().mSlabsDraining, 0u);
        }

        /// **A whole chunk drains where its content fits the free slots of its classes elsewhere and the free slabs of
        /// the other chunks**: two 8 MiB chunks of 256 KiB slots, the first full but for two freed slots, the second
        /// holding two. The second's two fit the first's two free slots, 512 KiB; the first's thirty want a slab beside
        /// the second's fourteen free slots, which the second's free slab gives, 7.5 MiB. The second drains, all its
        /// slabs, and takes no slot.
        TEST(RtxSlotLedgerTest, aWholeChunkDrainsWhereItsContentFitsElsewhere)
        {
            SlotLayout layout = twoArenas();
            layout.mChunkBytes = 8 * sMiB;
            layout.mMediumStrideBytesMost = sMiB;
            SlotLedger ledger(layout);
            const ClassIndex of = ledger.classOf(SlotKind::Image, 256 * sKiB, 1);
            const ClassIndex medium = ledger.classOf(SlotKind::Image, sMiB, 1);
            chunkOf(ledger, sImages);
            std::vector<Slot> first;
            first.reserve(32);
            for (std::uint32_t at = 0; at < 32; ++at)
                first.push_back(ledger.reserve(of, SlotOwner{ at }));
            const BlockIndex second = chunkOf(ledger, sImages);
            std::vector<Slot> two{ ledger.reserve(of, SlotOwner{ 40 }), ledger.reserve(of, SlotOwner{ 41 }) };
            for (const Slot slot : first)
                ledger.commit(slot);
            for (const Slot slot : two)
                ledger.commit(slot);
            ledger.leave(first[0]);
            ledger.retire(first[0], epoch(1));
            ledger.leave(first[20]);
            ledger.retire(first[20], epoch(1));
            ledger.collect(epoch(1));
            ASSERT_FALSE(ledger.hasRoom(medium));

            const std::optional<Drain> drain = ledger.drainFor(sImages, SlabTier::Medium);
            ASSERT_TRUE(drain.has_value());
            EXPECT_EQ(*drain, (Drain{ .mBlock = second, .mSlab = sNoRow, .mLiveBytes = 2 * 256 * sKiB }));
            EXPECT_EQ(ledger.count().mSlabsDraining, 1u);
            for (const Slot slot : two)
            {
                const Slot to = ledger.reserve(of, ledger.getOwnerOf(slot));
                EXPECT_NE(ledger.getBlockOf(to), second);
                ledger.commit(to);
                ledger.leave(slot);
                ledger.retire(slot, epoch(2));
            }
            ledger.collect(epoch(2));
            EXPECT_TRUE(ledger.hasRoom(medium)) << "the chunk emptied";
            EXPECT_TRUE(ledger.isConsistent());
        }

        /// **Two arenas share no slab**: a chunk of the images' arena is no room for a buffer's class, and the
        /// buffers' chunk none for an image's.
        TEST(RtxSlotLedgerTest, twoArenasShareNoSlab)
        {
            SlotLedger ledger(twoArenas());
            const ClassIndex image = ledger.classOf(SlotKind::Image, 4 * sKiB, 1);
            const ClassIndex buffer = ledger.classOf(SlotKind::Buffer, 4 * sKiB, 1);
            chunkOf(ledger, sImages);
            EXPECT_TRUE(ledger.hasRoom(image));
            EXPECT_FALSE(ledger.hasRoom(buffer));
            const BlockIndex buffers = chunkOf(ledger, sBuffers);
            ASSERT_TRUE(ledger.hasRoom(buffer));
            EXPECT_EQ(ledger.getBlockOf(ledger.reserve(buffer, SlotOwner{ 1 })), buffers);
        }

        /// **Every transition asserts the state it leaves.**
        TEST(RtxSlotLedgerTest, everyTransitionAssertsTheStateItLeaves)
        {
            const auto made = [] {
                auto ledger = std::make_unique<SlotLedger>(twoArenas());
                ledger->blockArrived(ledger->askChunk(sImages));
                return ledger;
            };
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const Slot slot = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->leave(slot);
                },
                "a slot left that was not live");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const Slot slot = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->commit(slot);
                    ledger->retire(slot, epoch(1));
                },
                "a slot retired that was not leaving");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const Slot slot = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->commit(slot);
                    ledger->leave(slot);
                    ledger->leave(slot);
                },
                "a slot left that was not live");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const Slot slot = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->commit(slot);
                    ledger->commit(slot);
                },
                "a slot committed that was not reserved");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const Slot slot = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->commit(slot);
                    ledger->unreserve(slot);
                },
                "a slot withdrawn that was not reserved");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const ClassIndex of = ledger->classOf(SlotKind::Image, 1, 1);
                    const Slot one = ledger->reserve(of, SlotOwner{});
                    const Slot two = ledger->reserve(of, SlotOwner{});
                    ledger->commit(one);
                    ledger->commit(two);
                    ledger->leave(one);
                    ledger->leave(two);
                    ledger->retire(one, epoch(3));
                    ledger->retire(two, epoch(2));
                },
                "a slot retired at an epoch before one retired already");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    std::ignore = ledger->reserve(ledger->classOf(SlotKind::Buffer, 1, 1), SlotOwner{});
                },
                "a slot reserved in a class with no room");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const BlockIndex chunk = *ledger->freeBlock(sImages);
                    ledger->blockArrived(chunk);
                },
                "a block arrived that was not asked for");
            Testing::expectAssertDies(
                [&] {
                    auto ledger = made();
                    const BlockIndex chunk = *ledger->freeBlock(sImages);
                    std::ignore = ledger->reserve(ledger->classOf(SlotKind::Image, 1, 1), SlotOwner{});
                    ledger->blockReleased(chunk);
                },
                "a chunk released with a slab in it");
            Testing::expectAssertDies(
                [&] {
                    SlotLayout broken = twoArenas();
                    broken.mSlabBytes = 3 * sMiB;
                    SlotLedger ledger(broken);
                },
                "a slab of no power of two");
        }
    }
}
