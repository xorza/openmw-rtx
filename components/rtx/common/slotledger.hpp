#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include "deviceepoch.hpp"
#include "index.hpp"
#include "rowspan.hpp"
#include "rowvector.hpp"

namespace Rtx
{
    /// What a slot holds: an image binds into a range, a structure stands in a buffer. The two never share a slab,
    /// which keeps a device's buffer and image granularity out of the arithmetic.
    enum class SlotKind : std::uint8_t
    {
        Image,
        Buffer,
    };

    inline constexpr std::size_t sSlotKindCount = 2;

    /// How a class's slots are cut: many to a small slab, a few to a medium slab that is a whole chunk, or one to a
    /// block of its own.
    enum class SlabTier : std::uint8_t
    {
        Small,
        Medium,
        Large,
    };

    struct SizeClassRows;
    struct SlabRows;
    struct BlockRows;
    using ClassIndex = RowIndex<SizeClassRows>;
    using SlabIndex = RowIndex<SlabRows>;
    using BlockIndex = RowIndex<BlockRows>;

    /// One memory of the device that content stands in, as the backend numbers them (`SceneHolder::arenaOf`).
    enum class ArenaIndex : std::uint8_t
    {
    };

    /// Device bytes by name: `stride * mAt` into slab `mSlab`. None where `mSlab` is no row.
    struct Slot
    {
        SlabIndex mSlab = sNoRow;
        std::uint16_t mAt = 0;

        friend bool operator==(const Slot&, const Slot&) = default;
    };

    /// Whose a slot is, as its owner packs it: what a drain hands back for the owner to move.
    enum class SlotOwner : std::uint32_t
    {
    };

    /// How content's memory is cut: chunks of slabs, the strides that bound each tier, the ladder a stride is rounded
    /// up, and which arena each kind stands in.
    struct SlotLayout
    {
        std::uint64_t mChunkBytes = std::uint64_t{ 64 } << 20;
        std::uint64_t mSlabBytes = std::uint64_t{ 4 } << 20;
        std::uint64_t mSmallStrideBytesMost = std::uint64_t{ 512 } << 10;
        std::uint64_t mMediumStrideBytesMost = std::uint64_t{ 8 } << 20;
        std::uint32_t mLadderStepCount = 8;
        std::uint32_t mUnitBytes = 256;
        std::array<ArenaIndex, sSlotKindCount> mArenaOf{};
    };

    /// What a drain is of: one small slab, or every slab of a chunk, and the live bytes that move.
    struct Drain
    {
        BlockIndex mBlock = sNoRow;

        /// None where the whole chunk drains.
        SlabIndex mSlab = sNoRow;

        std::uint64_t mLiveBytes = 0;

        friend bool operator==(const Drain&, const Drain&) = default;
    };

    /// What the ledger holds, by state: what a report says and a test reads.
    struct SlotCounts
    {
        std::uint32_t mBlocksAsked = 0;
        std::uint32_t mBlocksHeld = 0;
        std::uint64_t mBytesHeld = 0;
        std::uint32_t mSlabsFree = 0;
        std::uint32_t mSlabsInClass = 0;
        std::uint32_t mSlabsDraining = 0;
        std::uint32_t mSlotsFree = 0;
        std::uint32_t mSlotsReserved = 0;
        std::uint32_t mSlotsLive = 0;
        std::uint32_t mSlotsRetiring = 0;

        friend bool operator==(const SlotCounts&, const SlotCounts&) = default;
    };

    /// **Which slot of which slab of which block of device memory content stands in, and in what state**: the one
    /// record of it, in the core, with no device and no scene (`docs/architecture.md`, "Content's memory"). The slab
    /// allocator (Bonwick 1994) over chunks, as the kernel, jemalloc and memcached run it: every slot of a class is the
    /// same size, so a free slot serves any request of its class and "does it fit" is a comparison of counts. The
    /// backend maps a slot to bytes and counts nothing.
    ///
    /// Every state is a closed set with one function a transition, each asserting the state it leaves. A slot is free,
    /// reserved, live or retiring; a slab free, in a class, or draining; a block asked, held or gone.
    class SlotLedger
    {
    public:
        /// Asserts the layout: a chunk a whole number of slabs, both powers of two, the tiers rising.
        explicit SlotLedger(const SlotLayout& layout);

        SlotLedger(const SlotLedger&) = delete;
        SlotLedger& operator=(const SlotLedger&) = delete;
        ~SlotLedger() = default;

        /// The stride a resource of `bytes` at `alignment` stands at: its size rounded up a ladder of
        /// `mLadderStepCount` steps an octave in units of `mUnitBytes`, then up to `alignment`.
        static std::uint64_t strideFor(const SlotLayout& layout, std::uint64_t bytes, std::uint64_t alignment);

        /// The class a resource of `bytes` at `alignment` stands in, made where new.
        ClassIndex classOf(SlotKind kind, std::uint64_t bytes, std::uint64_t alignment);
        std::uint64_t getStrideBytes(ClassIndex of) const;
        SlabTier getTier(ClassIndex of) const;
        ArenaIndex getArena(ClassIndex of) const;
        std::uint32_t getSlotsPerSlab(ClassIndex of) const;
        std::size_t getClassCount() const { return mClasses.size(); }

        /// Names a block to be acquired: a chunk, or the one a large class's resource stands in.
        [[nodiscard]] BlockIndex askChunk(ArenaIndex arena);
        [[nodiscard]] BlockIndex askLarge(ClassIndex of);

        /// Asked → held: sixteen free slabs, or one slot.
        void blockArrived(BlockIndex block);

        /// Asked → gone: the device had none.
        void blockRefused(BlockIndex block);

        /// A held block nothing of which stands in a class, or none.
        std::optional<BlockIndex> freeBlock(ArenaIndex arena) const;

        /// Held → gone: asserts it free.
        void blockReleased(BlockIndex block);

        std::uint64_t getBlockBytes(BlockIndex block) const;

        /// How many chunks of `arena` are asked or held, and how many blocks of its own `of` has asked or holds.
        std::uint32_t getChunkCount(ArenaIndex arena) const;
        std::uint32_t getLargeBlockCount(ClassIndex of) const;

        const SlotLayout& getLayout() const { return mLayout; }

        /// Whether `reserve(of, …)` has a slot: a free slot of the class outside what drains, a free slab, a free
        /// chunk for a medium class, an arrived block for a large one.
        bool hasRoom(ClassIndex of) const;

        /// A free slot of the class, reserved for `owner`: in the fullest slab of the class with one, so the emptiest
        /// is left to drain; else in a new slab. Asserts `hasRoom`.
        [[nodiscard]] Slot reserve(ClassIndex of, SlotOwner owner);

        /// Reserved → live.
        void commit(Slot slot);

        /// Reserved → free: an operation withdrawn before it rode.
        void unreserve(Slot slot);

        /// Live → retiring, until the device has finished `until`.
        void retire(Slot slot, DeviceEpoch until);

        /// Retiring → free for every slot retired at or before `finished`; a slab with no slot left goes back to the
        /// pool, and a chunk none of whose slabs is in a class is a free block.
        void collect(DeviceEpoch finished);

        /// Where a slot stands: its block, and its offset in it.
        BlockIndex getBlockOf(Slot slot) const;
        std::uint64_t getOffsetOf(Slot slot) const;
        ClassIndex getClassOf(Slot slot) const;
        SlotOwner getOwnerOf(Slot slot) const;

        /// What a plan of `counts` slots a class stands in, in `arena`, in bytes of whole chunks and large blocks: the
        /// small slabs a class needs packed sixteen to a chunk, a chunk a medium slab, a block a large slot. The plan's
        /// only arithmetic. `counts` is indexed by class, and shorter than the classes where the rest want none.
        std::uint64_t bytesFor(ArenaIndex arena, RowSpan<const std::uint32_t, ClassIndex> counts) const;

        /// What `arena`'s classes stand in now, as `bytesFor` counts it: the chunks any slab of which is in a class,
        /// and the large blocks in use.
        std::uint64_t getBytesInUse(ArenaIndex arena) const;

        /// Marks draining what must empty for a free slab of `tier` to come in `arena`, and answers it: of what can be
        /// emptied into free slots of its classes and free slabs elsewhere, what holds the fewest live bytes. Nothing
        /// where nothing can, or a free one stands already.
        std::optional<Drain> drainFor(ArenaIndex arena, SlabTier tier);

        /// The next live slot of what drains, lowest slab and slot first; none once nothing live is left in it.
        std::optional<Slot> nextToMove() const;

        SlotCounts count() const;

        /// Every slot in one state, and every count its sum: what a debug build and a test ask.
        bool isConsistent() const;

    private:
        enum class SlotState : std::uint8_t
        {
            Free,
            Reserved,
            Live,
            Retiring,
        };

        enum class SlabState : std::uint8_t
        {
            Unused,
            InClass,
            Draining,
        };

        enum class BlockState : std::uint8_t
        {
            Gone,
            Asked,
            Held,
        };

        enum class BlockUse : std::uint8_t
        {
            Chunk,
            Large,
        };

        static constexpr std::uint8_t sWhole = 0xFF;

        struct SizeClass
        {
            SlotKind mKind = SlotKind::Image;
            ArenaIndex mArena{};
            std::uint64_t mStride = 0;
            SlabTier mTier = SlabTier::Small;
            std::uint32_t mSlotsPerSlab = 0;

            /// Its slabs with a free slot that do not drain, in no order: a reserve takes the fullest.
            std::vector<SlabIndex> mOpen;

            /// Its large blocks arrived and not yet in use.
            std::vector<BlockIndex> mArrived;
        };

        struct Slab
        {
            SlabState mState = SlabState::Unused;
            BlockIndex mBlock = sNoRow;

            /// Its place in its chunk, or `sWhole` for a medium slab or a large block's.
            std::uint8_t mPiece = sWhole;
            ClassIndex mClass = sNoRow;
            std::vector<SlotState> mSlots;
            std::vector<SlotOwner> mOwners;

            /// Its free slots, the last taken first.
            std::vector<std::uint16_t> mFree;
            std::uint32_t mHeld = 0;
        };

        struct Block
        {
            BlockState mState = BlockState::Gone;
            BlockUse mUse = BlockUse::Chunk;
            ArenaIndex mArena{};
            std::uint64_t mBytes = 0;
            ClassIndex mLargeClass = sNoRow;

            /// A chunk's small slabs by place, or one whole slab: a medium class's or a large block's.
            std::vector<SlabIndex> mPieces;
            SlabIndex mWhole = sNoRow;
        };

        struct Retiring
        {
            Slot mSlot;
            DeviceEpoch mUntil;
        };

        std::uint32_t piecesPerChunk() const
        {
            return static_cast<std::uint32_t>(mLayout.mChunkBytes / mLayout.mSlabBytes);
        }
        std::uint64_t slabBytesOf(SlabTier tier, std::uint64_t stride) const;

        Slab& slabOf(Slot slot);
        const Slab& slabOf(Slot slot) const;

        /// A held chunk of `arena` none of whose pieces is in a class.
        bool isFreeChunk(const Block& block) const;

        /// A small slab's place free in a held chunk of `arena`, the fullest chunk first, or none.
        std::optional<std::pair<BlockIndex, std::uint8_t>> findFreePiece(ArenaIndex arena) const;
        std::optional<BlockIndex> findFreeChunk(ArenaIndex arena) const;

        SlabIndex openSlab(ClassIndex of, BlockIndex block, std::uint8_t piece);
        void closeSlab(SlabIndex row);
        void freeSlot(Slot slot);
        BlockIndex takeBlockRow();

        /// What emptying `slab` needs elsewhere: whether its live slots fit its class's free slots outside it and
        /// outside `except`, plus `freePieces` free small slabs.
        std::uint32_t freeSlotsOutside(ClassIndex of, SlabIndex slab, BlockIndex except) const;

        SlotLayout mLayout;
        RowVector<SizeClass, ClassIndex> mClasses;
        RowVector<Slab, SlabIndex> mSlabs;
        std::vector<SlabIndex> mFreeSlabRows;
        RowVector<Block, BlockIndex> mBlocks;
        std::vector<BlockIndex> mFreeBlockRows;
        std::deque<Retiring> mRetiring;
        DeviceEpoch mLastRetired;
    };
}
