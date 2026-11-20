#include "slotledger.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t roundUp(const std::uint64_t value, const std::uint64_t to)
        {
            return (value + to - 1) / to * to;
        }

        constexpr std::uint64_t ceilDiv(const std::uint64_t value, const std::uint64_t by)
        {
            return (value + by - 1) / by;
        }
    }

    SlotLedger::SlotLedger(const SlotLayout& layout)
        : mLayout(layout)
    {
        assert(std::has_single_bit(layout.mChunkBytes) && "a chunk of no power of two");
        assert(std::has_single_bit(layout.mSlabBytes) && "a slab of no power of two");
        assert(layout.mSlabBytes <= layout.mChunkBytes && "a slab past its chunk");
        assert(layout.mChunkBytes / layout.mSlabBytes <= sWhole && "more slabs to a chunk than a place names");
        assert(std::has_single_bit(layout.mLadderStepCount) && "a ladder of no power of two steps");
        assert(std::has_single_bit(layout.mUnitBytes) && "a unit of no power of two");
        assert(layout.mSmallStrideBytesMost < layout.mMediumStrideBytesMost && "the tiers do not rise");
        assert(layout.mSlabBytes / layout.mSmallStrideBytesMost >= 8 && "a small slab of fewer than eight slots");
        assert(layout.mChunkBytes / layout.mMediumStrideBytesMost >= 8 && "a medium slab of fewer than eight slots");
        assert(layout.mSlabBytes / layout.mUnitBytes <= 0x10000 && "more slots to a slab than a slot names");
    }

    std::uint64_t SlotLedger::strideFor(
        const SlotLayout& layout, const std::uint64_t bytes, const std::uint64_t alignment)
    {
        assert(std::has_single_bit(alignment) && "an alignment of no power of two");
        // **Eight steps an octave**: past the ladder's first octave a size rounds up to a multiple of an eighth of its
        // power of two, `2^k·(8+i)/8`, at most 11% over and about 6% on average. Below it, a unit each.
        std::uint64_t units = std::max<std::uint64_t>(1, ceilDiv(bytes, layout.mUnitBytes));
        const auto octave = static_cast<std::uint32_t>(std::bit_width(units)) - 1;
        const auto stepsLog = static_cast<std::uint32_t>(std::countr_zero(layout.mLadderStepCount));
        if (octave > stepsLog)
            units = roundUp(units, std::uint64_t{ 1 } << (octave - stepsLog));
        return roundUp(units * layout.mUnitBytes, std::max<std::uint64_t>(alignment, layout.mUnitBytes));
    }

    ClassIndex SlotLedger::classOf(const SlotKind kind, const std::uint64_t bytes, const std::uint64_t alignment)
    {
        const std::uint64_t stride = strideFor(mLayout, bytes, alignment);
        for (std::uint32_t at = 0; at < mClasses.size(); ++at)
        {
            const SizeClass& held = mClasses[ClassIndex(at)];
            if (held.mKind == kind && held.mStride == stride)
                return ClassIndex(at);
        }

        SizeClass made;
        made.mKind = kind;
        made.mArena = mLayout.mArenaOf[static_cast<std::size_t>(kind)];
        made.mStride = stride;
        made.mTier = stride <= mLayout.mSmallStrideBytesMost ? SlabTier::Small
            : stride <= mLayout.mMediumStrideBytesMost       ? SlabTier::Medium
                                                             : SlabTier::Large;
        made.mSlotsPerSlab = static_cast<std::uint32_t>(slabBytesOf(made.mTier, stride) / stride);
        mClasses.push_back(std::move(made));
        return ClassIndex(static_cast<std::uint32_t>(mClasses.size() - 1));
    }

    std::uint64_t SlotLedger::slabBytesOf(const SlabTier tier, const std::uint64_t stride) const
    {
        switch (tier)
        {
            case SlabTier::Small:
                return mLayout.mSlabBytes;
            case SlabTier::Medium:
                return mLayout.mChunkBytes;
            case SlabTier::Large:
                return stride;
        }
        assert(false && "a tier past the three");
        return stride;
    }

    std::uint64_t SlotLedger::getStrideBytes(const ClassIndex of) const
    {
        return mClasses[of].mStride;
    }

    SlabTier SlotLedger::getTier(const ClassIndex of) const
    {
        return mClasses[of].mTier;
    }

    ArenaIndex SlotLedger::getArena(const ClassIndex of) const
    {
        return mClasses[of].mArena;
    }

    std::uint32_t SlotLedger::getSlotsPerSlab(const ClassIndex of) const
    {
        return mClasses[of].mSlotsPerSlab;
    }

    BlockIndex SlotLedger::takeBlockRow()
    {
        if (!mFreeBlockRows.empty())
        {
            // The lowest row first, so two processes asking alike name their blocks alike.
            const auto lowest = std::ranges::min_element(mFreeBlockRows);
            const BlockIndex row = *lowest;
            mFreeBlockRows.erase(lowest);
            return row;
        }
        mBlocks.push_back(Block{});
        return BlockIndex(static_cast<std::uint32_t>(mBlocks.size() - 1));
    }

    BlockIndex SlotLedger::askChunk(const ArenaIndex arena)
    {
        const BlockIndex row = takeBlockRow();
        Block& block = mBlocks[row];
        block = Block{};
        block.mState = BlockState::Asked;
        block.mUse = BlockUse::Chunk;
        block.mArena = arena;
        block.mBytes = mLayout.mChunkBytes;
        block.mPieces.assign(piecesPerChunk(), SlabIndex(sNoRow));
        return row;
    }

    BlockIndex SlotLedger::askLarge(const ClassIndex of)
    {
        assert(mClasses[of].mTier == SlabTier::Large && "a block of its own asked for a class that shares slabs");
        const BlockIndex row = takeBlockRow();
        Block& block = mBlocks[row];
        block = Block{};
        block.mState = BlockState::Asked;
        block.mUse = BlockUse::Large;
        block.mArena = mClasses[of].mArena;
        block.mBytes = mClasses[of].mStride;
        block.mLargeClass = of;
        return row;
    }

    void SlotLedger::blockArrived(const BlockIndex block)
    {
        Block& held = mBlocks[block];
        assert(held.mState == BlockState::Asked && "a block arrived that was not asked for");
        held.mState = BlockState::Held;
        if (held.mUse == BlockUse::Large)
            mClasses[held.mLargeClass].mArrived.push_back(block);
    }

    void SlotLedger::blockRefused(const BlockIndex block)
    {
        Block& refused = mBlocks[block];
        assert(refused.mState == BlockState::Asked && "a block refused that was not asked for");
        refused = Block{};
        mFreeBlockRows.push_back(block);
    }

    bool SlotLedger::isFreeChunk(const Block& block) const
    {
        return block.mState == BlockState::Held && block.mUse == BlockUse::Chunk && block.mWhole.isNone()
            && std::ranges::all_of(block.mPieces, [](const SlabIndex piece) { return piece.isNone(); });
    }

    std::optional<BlockIndex> SlotLedger::freeBlock(const ArenaIndex arena) const
    {
        for (std::uint32_t at = 0; at < mBlocks.size(); ++at)
        {
            const Block& block = mBlocks[BlockIndex(at)];
            if (block.mArena != arena || block.mState != BlockState::Held)
                continue;
            const bool freeLarge = block.mUse == BlockUse::Large && block.mWhole.isNone();
            if (isFreeChunk(block) || freeLarge)
                return BlockIndex(at);
        }
        return std::nullopt;
    }

    void SlotLedger::blockReleased(const BlockIndex block)
    {
        Block& released = mBlocks[block];
        assert(released.mState == BlockState::Held && "a block released that is not held");
        assert(released.mWhole.isNone() && "a block released with a slab in it");
        assert(std::ranges::all_of(released.mPieces, [](const SlabIndex piece) { return piece.isNone(); })
            && "a chunk released with a slab in it");
        if (released.mUse == BlockUse::Large)
            std::erase(mClasses[released.mLargeClass].mArrived, block);
        released = Block{};
        mFreeBlockRows.push_back(block);
    }

    std::uint64_t SlotLedger::getBlockBytes(const BlockIndex block) const
    {
        return mBlocks[block].mBytes;
    }

    std::optional<std::pair<BlockIndex, std::uint8_t>> SlotLedger::findFreePiece(const ArenaIndex arena) const
    {
        // **A chunk already cut into small slabs first, the fullest of them**, so a free chunk stays whole for a medium
        // class, and the emptiest chunk is the one left to drain.
        std::optional<std::pair<BlockIndex, std::uint8_t>> best;
        std::uint32_t bestUsed = 0;
        std::optional<BlockIndex> whole;
        for (std::uint32_t at = 0; at < mBlocks.size(); ++at)
        {
            const Block& block = mBlocks[BlockIndex(at)];
            if (block.mArena != arena || block.mState != BlockState::Held || block.mUse != BlockUse::Chunk
                || !block.mWhole.isNone())
                continue;
            std::uint32_t used = 0;
            std::optional<std::uint8_t> freePiece;
            for (std::size_t piece = 0; piece < block.mPieces.size(); ++piece)
            {
                if (!block.mPieces[piece].isNone())
                    ++used;
                else if (!freePiece.has_value())
                    freePiece = static_cast<std::uint8_t>(piece);
            }
            if (!freePiece.has_value())
                continue;
            if (used == 0)
            {
                if (!whole.has_value())
                    whole = BlockIndex(at);
                continue;
            }
            if (!best.has_value() || used > bestUsed)
            {
                best = std::pair(BlockIndex(at), *freePiece);
                bestUsed = used;
            }
        }
        if (best.has_value())
            return best;
        if (whole.has_value())
            return std::pair(*whole, std::uint8_t{ 0 });
        return std::nullopt;
    }

    std::optional<BlockIndex> SlotLedger::findFreeChunk(const ArenaIndex arena) const
    {
        for (std::uint32_t at = 0; at < mBlocks.size(); ++at)
        {
            const Block& block = mBlocks[BlockIndex(at)];
            if (block.mArena == arena && isFreeChunk(block))
                return BlockIndex(at);
        }
        return std::nullopt;
    }

    bool SlotLedger::hasRoom(const ClassIndex of) const
    {
        const SizeClass& held = mClasses[of];
        if (!held.mOpen.empty())
            return true;
        switch (held.mTier)
        {
            case SlabTier::Small:
                return findFreePiece(held.mArena).has_value();
            case SlabTier::Medium:
                return findFreeChunk(held.mArena).has_value();
            case SlabTier::Large:
                return !held.mArrived.empty();
        }
        return false;
    }

    SlabIndex SlotLedger::openSlab(const ClassIndex of, const BlockIndex block, const std::uint8_t piece)
    {
        SlabIndex row = sNoRow;
        if (!mFreeSlabRows.empty())
        {
            const auto lowest = std::ranges::min_element(mFreeSlabRows);
            row = *lowest;
            mFreeSlabRows.erase(lowest);
        }
        else
        {
            mSlabs.push_back(Slab{});
            row = SlabIndex(static_cast<std::uint32_t>(mSlabs.size() - 1));
        }

        const SizeClass& held = mClasses[of];
        Slab& slab = mSlabs[row];
        assert(slab.mState == SlabState::Unused && "a slab opened over one in use");
        slab.mState = SlabState::InClass;
        slab.mBlock = block;
        slab.mPiece = piece;
        slab.mClass = of;
        slab.mSlots.assign(held.mSlotsPerSlab, SlotState::Free);
        slab.mOwners.assign(held.mSlotsPerSlab, SlotOwner{});
        slab.mFree.clear();
        // Descending, so the lowest slot is taken first.
        for (std::uint32_t at = held.mSlotsPerSlab; at > 0; --at)
            slab.mFree.push_back(static_cast<std::uint16_t>(at - 1));
        slab.mHeld = 0;

        Block& in = mBlocks[block];
        if (piece == sWhole)
            in.mWhole = row;
        else
            in.mPieces[piece] = row;
        if (held.mTier == SlabTier::Large)
            std::erase(mClasses[of].mArrived, block);
        mClasses[of].mOpen.push_back(row);
        return row;
    }

    void SlotLedger::closeSlab(const SlabIndex row)
    {
        Slab& slab = mSlabs[row];
        assert(slab.mHeld == 0 && "a slab closed with a slot held");
        SizeClass& held = mClasses[slab.mClass];
        std::erase(held.mOpen, row);
        Block& block = mBlocks[slab.mBlock];
        if (slab.mPiece == sWhole)
            block.mWhole = sNoRow;
        else
            block.mPieces[slab.mPiece] = sNoRow;
        // A large block emptied is its class's to take again, or the device's to have back.
        if (held.mTier == SlabTier::Large)
            held.mArrived.push_back(slab.mBlock);

        slab.mState = SlabState::Unused;
        slab.mBlock = sNoRow;
        slab.mClass = sNoRow;
        slab.mSlots.clear();
        slab.mOwners.clear();
        slab.mFree.clear();
        mFreeSlabRows.push_back(row);
    }

    Slot SlotLedger::reserve(const ClassIndex of, const SlotOwner owner)
    {
        assert(hasRoom(of) && "a slot reserved in a class with no room");
        SizeClass& held = mClasses[of];

        SlabIndex chosen = sNoRow;
        if (!held.mOpen.empty())
        {
            // The fullest, lowest first among equals, so the emptiest is left to drain.
            chosen = *std::ranges::min_element(held.mOpen, [&](const SlabIndex a, const SlabIndex b) {
                const std::size_t freeA = mSlabs[a].mFree.size();
                const std::size_t freeB = mSlabs[b].mFree.size();
                return freeA != freeB ? freeA < freeB : a < b;
            });
        }
        else
        {
            switch (held.mTier)
            {
                case SlabTier::Small:
                {
                    const auto piece = findFreePiece(held.mArena);
                    chosen = openSlab(of, piece->first, piece->second);
                    break;
                }
                case SlabTier::Medium:
                    chosen = openSlab(of, *findFreeChunk(held.mArena), sWhole);
                    break;
                case SlabTier::Large:
                    chosen = openSlab(of, held.mArrived.front(), sWhole);
                    break;
            }
        }

        Slab& slab = mSlabs[chosen];
        const std::uint16_t at = slab.mFree.back();
        slab.mFree.pop_back();
        assert(slab.mSlots[at] == SlotState::Free && "a free list naming a slot in use");
        slab.mSlots[at] = SlotState::Reserved;
        slab.mOwners[at] = owner;
        ++slab.mHeld;
        if (slab.mFree.empty())
            std::erase(mClasses[of].mOpen, chosen);
        return Slot{ .mSlab = chosen, .mAt = at };
    }

    SlotLedger::Slab& SlotLedger::slabOf(const Slot slot)
    {
        assert(!slot.mSlab.isNone() && "no slot");
        return mSlabs[slot.mSlab];
    }

    const SlotLedger::Slab& SlotLedger::slabOf(const Slot slot) const
    {
        assert(!slot.mSlab.isNone() && "no slot");
        return mSlabs[slot.mSlab];
    }

    void SlotLedger::commit(const Slot slot)
    {
        Slab& slab = slabOf(slot);
        assert(slab.mSlots[slot.mAt] == SlotState::Reserved && "a slot committed that was not reserved");
        slab.mSlots[slot.mAt] = SlotState::Live;
    }

    void SlotLedger::freeSlot(const Slot slot)
    {
        Slab& slab = slabOf(slot);
        slab.mSlots[slot.mAt] = SlotState::Free;
        slab.mFree.push_back(slot.mAt);
        --slab.mHeld;
        if (slab.mHeld == 0)
        {
            closeSlab(slot.mSlab);
            return;
        }
        if (slab.mState == SlabState::InClass && slab.mFree.size() == 1)
            mClasses[slab.mClass].mOpen.push_back(slot.mSlab);
    }

    void SlotLedger::unreserve(const Slot slot)
    {
        assert(slabOf(slot).mSlots[slot.mAt] == SlotState::Reserved && "a slot withdrawn that was not reserved");
        freeSlot(slot);
    }

    void SlotLedger::retire(const Slot slot, const DeviceEpoch until)
    {
        Slab& slab = slabOf(slot);
        assert(slab.mSlots[slot.mAt] == SlotState::Live && "a slot retired that was not live");
        assert(until >= mLastRetired && "a slot retired at an epoch before one retired already");
        slab.mSlots[slot.mAt] = SlotState::Retiring;
        mRetiring.push_back(Retiring{ .mSlot = slot, .mUntil = until });
        mLastRetired = until;
    }

    void SlotLedger::collect(const DeviceEpoch finished)
    {
        while (!mRetiring.empty() && mRetiring.front().mUntil <= finished)
        {
            const Slot slot = mRetiring.front().mSlot;
            mRetiring.pop_front();
            assert(slabOf(slot).mSlots[slot.mAt] == SlotState::Retiring && "a slot collected that was not retiring");
            freeSlot(slot);
        }
    }

    BlockIndex SlotLedger::getBlockOf(const Slot slot) const
    {
        return slabOf(slot).mBlock;
    }

    std::uint64_t SlotLedger::getOffsetOf(const Slot slot) const
    {
        const Slab& slab = slabOf(slot);
        const std::uint64_t base = slab.mPiece == sWhole ? 0 : std::uint64_t{ slab.mPiece } * mLayout.mSlabBytes;
        return base + std::uint64_t{ slot.mAt } * mClasses[slab.mClass].mStride;
    }

    ClassIndex SlotLedger::getClassOf(const Slot slot) const
    {
        return slabOf(slot).mClass;
    }

    SlotOwner SlotLedger::getOwnerOf(const Slot slot) const
    {
        return slabOf(slot).mOwners[slot.mAt];
    }

    std::uint64_t SlotLedger::bytesFor(
        const ArenaIndex arena, const RowSpan<const std::uint32_t, ClassIndex> counts) const
    {
        std::uint64_t smallSlabs = 0;
        std::uint64_t bytes = 0;
        for (std::uint32_t at = 0; at < counts.size(); ++at)
        {
            const SizeClass& held = mClasses[ClassIndex(at)];
            const std::uint32_t count = counts[ClassIndex(at)];
            if (held.mArena != arena || count == 0)
                continue;
            switch (held.mTier)
            {
                case SlabTier::Small:
                    smallSlabs += ceilDiv(count, held.mSlotsPerSlab);
                    break;
                case SlabTier::Medium:
                    bytes += ceilDiv(count, held.mSlotsPerSlab) * mLayout.mChunkBytes;
                    break;
                case SlabTier::Large:
                    bytes += std::uint64_t{ count } * held.mStride;
                    break;
            }
        }
        return bytes + ceilDiv(smallSlabs, piecesPerChunk()) * mLayout.mChunkBytes;
    }

    std::uint64_t SlotLedger::getBytesInUse(const ArenaIndex arena) const
    {
        std::uint64_t bytes = 0;
        for (const Block& block : mBlocks)
        {
            if (block.mArena != arena || block.mState != BlockState::Held)
                continue;
            const bool used = !block.mWhole.isNone()
                || std::ranges::any_of(block.mPieces, [](const SlabIndex piece) { return !piece.isNone(); });
            if (used)
                bytes += block.mBytes;
        }
        return bytes;
    }

    std::uint32_t SlotLedger::freeSlotsOutside(const ClassIndex of, const SlabIndex slab, const BlockIndex except) const
    {
        std::uint32_t free = 0;
        for (const SlabIndex open : mClasses[of].mOpen)
            if (open != slab && mSlabs[open].mBlock != except)
                free += static_cast<std::uint32_t>(mSlabs[open].mFree.size());
        return free;
    }

    std::optional<Drain> SlotLedger::drainFor(const ArenaIndex arena, const SlabTier tier)
    {
        // What must move out of a slab: its slots in use, but those retiring, which free themselves.
        const auto mustMove = [&](const Slab& slab) {
            return static_cast<std::uint32_t>(std::ranges::count_if(slab.mSlots,
                [](const SlotState state) { return state == SlotState::Live || state == SlotState::Reserved; }));
        };

        if (tier == SlabTier::Small)
        {
            if (findFreePiece(arena).has_value())
                return std::nullopt;
            std::optional<SlabIndex> best;
            std::uint64_t bestBytes = 0;
            for (std::uint32_t at = 0; at < mSlabs.size(); ++at)
            {
                const Slab& slab = mSlabs[SlabIndex(at)];
                if (slab.mState != SlabState::InClass || slab.mPiece == sWhole)
                    continue;
                const SizeClass& held = mClasses[slab.mClass];
                if (held.mArena != arena)
                    continue;
                const std::uint32_t moving = mustMove(slab);
                if (moving > freeSlotsOutside(slab.mClass, SlabIndex(at), sNoRow))
                    continue;
                const std::uint64_t bytes = std::uint64_t{ moving } * held.mStride;
                if (!best.has_value() || bytes < bestBytes)
                {
                    best = SlabIndex(at);
                    bestBytes = bytes;
                }
            }
            if (!best.has_value())
                return std::nullopt;
            Slab& draining = mSlabs[*best];
            draining.mState = SlabState::Draining;
            std::erase(mClasses[draining.mClass].mOpen, *best);
            return Drain{ .mBlock = draining.mBlock, .mSlab = *best, .mLiveBytes = bestBytes };
        }

        if (tier == SlabTier::Large || findFreeChunk(arena).has_value())
            return std::nullopt;

        // A chunk: what each of its slabs holds must fit its class's free slots outside it, and what does not, the
        // free small slabs of the other chunks, a slab of its class each.
        std::uint32_t freePieces = 0;
        for (const Block& block : mBlocks)
            if (block.mArena == arena && block.mState == BlockState::Held && block.mUse == BlockUse::Chunk
                && block.mWhole.isNone())
                freePieces += static_cast<std::uint32_t>(
                    std::ranges::count_if(block.mPieces, [](const SlabIndex piece) { return piece.isNone(); }));

        std::optional<BlockIndex> best;
        std::uint64_t bestBytes = 0;
        for (std::uint32_t at = 0; at < mBlocks.size(); ++at)
        {
            const Block& block = mBlocks[BlockIndex(at)];
            if (block.mArena != arena || block.mState != BlockState::Held || block.mUse != BlockUse::Chunk)
                continue;
            std::uint32_t piecesHere = 0;
            if (block.mWhole.isNone())
                piecesHere = static_cast<std::uint32_t>(
                    std::ranges::count_if(block.mPieces, [](const SlabIndex piece) { return piece.isNone(); }));
            std::uint32_t piecesWanted = 0;
            std::uint64_t bytes = 0;
            bool fits = true;
            const auto weigh = [&](const SlabIndex row) {
                const Slab& slab = mSlabs[row];
                const SizeClass& held = mClasses[slab.mClass];
                const std::uint32_t moving = mustMove(slab);
                bytes += std::uint64_t{ moving } * held.mStride;
                const std::uint32_t outside = freeSlotsOutside(slab.mClass, row, BlockIndex(at));
                if (moving <= outside)
                    return;
                if (held.mTier != SlabTier::Small)
                {
                    fits = false;
                    return;
                }
                piecesWanted += static_cast<std::uint32_t>(ceilDiv(moving - outside, held.mSlotsPerSlab));
            };
            if (!block.mWhole.isNone())
                weigh(block.mWhole);
            for (const SlabIndex piece : block.mPieces)
                if (!piece.isNone())
                    weigh(piece);
            if (!fits || piecesWanted > freePieces - piecesHere)
                continue;
            if (!best.has_value() || bytes < bestBytes)
            {
                best = BlockIndex(at);
                bestBytes = bytes;
            }
        }
        if (!best.has_value())
            return std::nullopt;

        Block& draining = mBlocks[*best];
        const auto mark = [&](const SlabIndex row) {
            if (row.isNone())
                return;
            Slab& slab = mSlabs[row];
            slab.mState = SlabState::Draining;
            std::erase(mClasses[slab.mClass].mOpen, row);
        };
        mark(draining.mWhole);
        for (const SlabIndex piece : draining.mPieces)
            mark(piece);
        return Drain{ .mBlock = *best, .mSlab = sNoRow, .mLiveBytes = bestBytes };
    }

    std::optional<Slot> SlotLedger::nextToMove() const
    {
        for (std::uint32_t at = 0; at < mSlabs.size(); ++at)
        {
            const Slab& slab = mSlabs[SlabIndex(at)];
            if (slab.mState != SlabState::Draining)
                continue;
            for (std::size_t slot = 0; slot < slab.mSlots.size(); ++slot)
                if (slab.mSlots[slot] == SlotState::Live)
                    return Slot{ .mSlab = SlabIndex(at), .mAt = static_cast<std::uint16_t>(slot) };
        }
        return std::nullopt;
    }

    SlotCounts SlotLedger::count() const
    {
        SlotCounts counts;
        for (const Block& block : mBlocks)
        {
            counts.mBlocksAsked += block.mState == BlockState::Asked ? 1 : 0;
            if (block.mState == BlockState::Held)
            {
                ++counts.mBlocksHeld;
                counts.mBytesHeld += block.mBytes;
                if (block.mUse == BlockUse::Chunk && block.mWhole.isNone())
                    counts.mSlabsFree += static_cast<std::uint32_t>(
                        std::ranges::count_if(block.mPieces, [](const SlabIndex piece) { return piece.isNone(); }));
            }
        }
        for (const Slab& slab : mSlabs)
        {
            if (slab.mState == SlabState::Unused)
                continue;
            counts.mSlabsInClass += slab.mState == SlabState::InClass ? 1 : 0;
            counts.mSlabsDraining += slab.mState == SlabState::Draining ? 1 : 0;
            for (const SlotState state : slab.mSlots)
            {
                counts.mSlotsFree += state == SlotState::Free ? 1 : 0;
                counts.mSlotsReserved += state == SlotState::Reserved ? 1 : 0;
                counts.mSlotsLive += state == SlotState::Live ? 1 : 0;
                counts.mSlotsRetiring += state == SlotState::Retiring ? 1 : 0;
            }
        }
        return counts;
    }

    bool SlotLedger::isConsistent() const
    {
        std::uint32_t retiring = 0;
        for (std::uint32_t at = 0; at < mSlabs.size(); ++at)
        {
            const Slab& slab = mSlabs[SlabIndex(at)];
            if (slab.mState == SlabState::Unused)
                continue;
            const SizeClass& held = mClasses[slab.mClass];
            const auto free = static_cast<std::uint32_t>(std::ranges::count(slab.mSlots, SlotState::Free));
            if (slab.mSlots.size() != held.mSlotsPerSlab || free != slab.mFree.size()
                || slab.mHeld + free != slab.mSlots.size() || slab.mHeld == 0)
                return false;
            for (const std::uint16_t slot : slab.mFree)
                if (slab.mSlots[slot] != SlotState::Free)
                    return false;
            const bool open = std::ranges::find(held.mOpen, SlabIndex(at)) != held.mOpen.end();
            if (open != (slab.mState == SlabState::InClass && free > 0))
                return false;
            const Block& block = mBlocks[slab.mBlock];
            const SlabIndex named = slab.mPiece == sWhole ? block.mWhole : block.mPieces[slab.mPiece];
            if (named != SlabIndex(at) || block.mState != BlockState::Held)
                return false;
            retiring += static_cast<std::uint32_t>(std::ranges::count(slab.mSlots, SlotState::Retiring));
        }
        return retiring == mRetiring.size();
    }
}
