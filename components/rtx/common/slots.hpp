#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "runs.hpp"

namespace Rtx
{
    /// The slots of a table that nothing stands in. The lowest is what a take answers with, never
    /// the last one freed: `Rtx::Identity` hashes by address, so a sweep retires in whatever order
    /// the allocator left its map in, and a list taken from the back would hand the same live set
    /// different slots in two processes, which `omw repeat` catches. Its own type
    /// because `GuiTextures` and the view scenes hold rows `SlotRows` cannot — a `unique_ptr` is
    /// move-only and `SlotRows::take` copies. A byte per slot says whether it is on the heap, so
    /// a slot freed twice is an assert here and not two entries the heap hands out as two slots.
    class SlotPool
    {
    public:
        /// A free slot, or `sNoIndex` where there is none and the caller has to append.
        Index take()
        {
            if (mFree.empty())
                return sNoIndex;

            std::pop_heap(mFree.begin(), mFree.end(), std::greater<>());
            const Index index = mFree.back();
            mFree.pop_back();
            mIsFree[index] = 0;

            return index;
        }

        /// Puts `slot` back, so the next `take` may answer with it. The only way onto the list, or
        /// it is no heap and the pop above answers with whatever the top happens to be.
        void free(Index slot)
        {
            // Grown here and not by the table, as `SlotSet::addMakingRoom` grows: the first free of
            // a slot is after the growth that made it, and a free is not the frame path's busy part.
            if (slot >= mIsFree.size())
                mIsFree.resize(std::size_t{ slot } + 1, 0);
            assert(mIsFree[slot] == 0 && "a slot freed twice");

            mIsFree[slot] = 1;
            mFree.push_back(slot);
            std::push_heap(mFree.begin(), mFree.end(), std::greater<>());
        }

        /// Whether `slot` is on the list — what a caller asks before it gives one back.
        bool isFree(Index slot) const { return slot < mIsFree.size() && mIsFree[slot] != 0; }

        /// How many slots stand empty, which a table subtracts from its length to count what lives.
        std::size_t size() const { return mFree.size(); }

        /// Every empty slot, in the heap's own order. For a mark, which walks all of them rather
        /// than searching per row.
        std::span<const Index> getSlots() const { return mFree; }

    private:
        /// A min-heap of the slots nothing stands in.
        std::vector<Index> mFree;

        /// A byte per slot ever freed, set for exactly the slots the heap holds.
        std::vector<std::uint8_t> mIsFree;
    };

    /// The slots of one table that something is true of, in the order they were named, each once.
    /// The list is what a frame walks and the byte is what keeps a slot named twice from appearing
    /// twice without searching the list — N²/2 comparisons for the N movers of a crowded cell.
    /// Kept together, because apart they fall out of step in the one direction nothing catches.
    class SlotSet
    {
    public:
        /// Makes room for at least `count` slots, which a table does as it takes one. Never shrinks
        /// the bytes: a table emptied is exactly when the next is about to be filled.
        void grow(std::size_t count)
        {
            if (count > mFlags.size())
                mFlags.resize(count);
        }

        /// Puts `slot` in the set, once however many times it is named.
        void add(Index slot)
        {
            assert(slot < mFlags.size() && "a slot the table has not grown to");
            if (mFlags[slot] != 0)
                return;

            mFlags[slot] = 1;
            mSlots.push_back(slot);
        }

        /// The same, for a caller that is told one slot at a time and never sees the table.
        void addMakingRoom(Index slot)
        {
            grow(std::size_t{ slot } + 1);
            add(slot);
        }

        /// Takes `slot` out. The list is left holding it until `compact` runs, because a sweep
        /// takes thousands back and erasing from the middle is that many moves apiece; `getSlots`
        /// will not answer while one is outstanding.
        void remove(Index slot)
        {
            assert(slot < mFlags.size() && "a slot the table has not grown to");
            if (mFlags[slot] == 0)
                return;

            mFlags[slot] = 0;
            mStale = true;
        }

        /// Drops what `remove` took, in one pass over the list rather than one per slot.
        void compact();

        /// Whether `slot` is in the set. Answers while a `remove` is outstanding, where `getSlots`
        /// will not: the flags are exact from the moment a slot is taken out.
        bool has(Index slot) const { return slot < mFlags.size() && mFlags[slot] != 0; }

        std::span<const Index> getSlots() const
        {
            assert(!mStale && "the list was read between a remove and the compact that settles it");
            return mSlots;
        }

        bool empty() const { return getSlots().empty(); }

        /// Empties the set. Only the slots in it are put back, rather than the whole table: a
        /// worldspace is thousands of slots and what a frame names is tens.
        void clear();

    private:
        std::vector<Index> mSlots;

        /// A byte per slot of the table beside it, set for exactly the slots `mSlots` names —
        /// except between a `remove` and the `compact` that settles it.
        std::vector<std::uint8_t> mFlags;

        bool mStale = false;
    };

    /// What has happened to one slot of a table since the last `clearArrivals`.
    enum class SlotNews : std::uint8_t
    {
        Arrived,
        Freed,
    };

    /// Which slots of one table arrived and which were given back, since a frame last read them.
    /// Two sets, and a slot stands in at most one of them: the last word wins, so a slot that
    /// arrived and went inside one frame is reported gone, and one that went and was taken over
    /// is reported arrived. A backend then releases a slot it never built and builds into one it
    /// never released — both of which it takes as nothing, and the hand-over relies on it. A
    /// backend comparing table sizes could not tell, and a slot taken over in place would tell it
    /// nothing at all.
    class SlotChanges
    {
    public:
        /// Makes room for at least `count` slots, which a table does as it takes one.
        void grow(std::size_t count)
        {
            mArrived.grow(count);
            mFreed.grow(count);
        }

        /// Records `slot` as having arrived or gone. The last word wins and the earlier one is
        /// taken back, because what a reader has to know is where the slot stands at the end of the
        /// frame.
        void note(Index slot, SlotNews what);

        std::span<const Index> getArrived() const { return mArrived.getSlots(); }
        std::span<const Index> getFreed() const { return mFreed.getSlots(); }

        /// Empties both sets.
        void clearArrivals()
        {
            mArrived.clear();
            mFreed.clear();
        }

    private:
        SlotSet mArrived;
        SlotSet mFreed;
    };

    /// A table of fixed-size rows: the rows, the slots nothing stands in, and what holds each. A
    /// slot is never moved and never closed up — a mesh index names a bottom-level acceleration
    /// structure and a texture index is what a material points at — so a dropped row leaves a hole
    /// and the next arrival takes the lowest one (`SlotPool`). The hold count lives here and the
    /// table frees the row the drop after which nothing holds it: a texture is held by materials,
    /// a rig by the meshes on it, a mesh by the identity that met it and the placements standing
    /// on it. What a freed row holds is the table's business too — a mesh row keeps its last
    /// tenant's offsets because a backend walks every slot, a material row is emptied — so `free`
    /// writes nothing.
    template <class Row>
    class SlotRows
    {
    public:
        std::size_t size() const { return mRows.size(); }

        /// How many slots hold a row.
        std::size_t getLiveCount() const { return mRows.size() - mFree.size(); }

        /// How many rows were ever freed, which is what says how many went between two looks.
        std::uint64_t getFreedCount() const { return mFreedCount; }

        std::span<const Row> getRows() const { return mRows; }

        /// Whether `slot` holds a row: what an index into this table has to name to mean anything,
        /// and what a placement's mesh or material is checked against. Asked of the free list, which
        /// keeps a byte per slot for it, so the answer has one source.
        bool isLive(Index slot) const
        {
            assert(slot < mRows.size());
            return !mFree.isFree(slot);
        }

        const Row& at(Index slot) const
        {
            assert(slot < mRows.size());
            return mRows[slot];
        }

        Row& at(Index slot)
        {
            assert(slot < mRows.size());
            return mRows[slot];
        }

        /// Puts `row` in a free slot, or in a new one. The slot arrives with no holds, and the
        /// caller holds it before anything can drop it. Everything a table knows about a slot is in
        /// the row, so nothing beside the rows has to follow a growth. By value and moved in, so a
        /// row that owns a name is built once.
        Index take(Row row)
        {
            const Index index = mFree.take();
            if (index == sNoIndex)
            {
                mRows.push_back(std::move(row));
                mHolds.push_back(0);
                return static_cast<Index>(mRows.size() - 1);
            }

            assert(mHolds[index] == 0 && "a free slot something still holds");
            mRows[index] = std::move(row);
            return index;
        }

        /// Puts `slot` back. What its row now holds is the caller's to have decided.
        void free(Index slot)
        {
            assert(slot < mRows.size());
            assert(mHolds[slot] == 0 && "a slot freed while something holds it");
            assert(isLive(slot) && "a slot freed twice");
            mFree.free(slot);
            ++mFreedCount;
        }

        void hold(Index slot)
        {
            assert(slot < mRows.size());
            assert(isLive(slot) && "a hold on a slot nothing stands in");
            ++mHolds[slot];
        }

        /// Counts one holder off `slot`, and says whether that was the last, which is where the
        /// table frees the row.
        bool drop(Index slot)
        {
            assert(slot < mRows.size());
            assert(mHolds[slot] > 0 && "a slot given back more often than it was held");
            return --mHolds[slot] == 0;
        }

        std::uint32_t getHolds(Index slot) const
        {
            assert(slot < mRows.size());
            return mHolds[slot];
        }

    private:
        std::vector<Row> mRows;

        /// How many things hold each row, parallel to the rows.
        std::vector<std::uint32_t> mHolds;

        SlotPool mFree;

        std::uint64_t mFreedCount = 0;
    };

    /// The half of `SlotRows` a reader uses, for a table whose rows go in through its own `add`
    /// and out through its own `drop`: `take`, `at`, `free` and the drop stay with the table, which
    /// alone knows what a freed row holds and what it gives back. One base and not the forwarders
    /// written per table, which were one policy twice.
    template <class Row>
    class HeldRows
    {
    public:
        std::size_t size() const { return mRows.size(); }
        std::size_t getLiveCount() const { return mRows.getLiveCount(); }
        std::uint64_t getFreedCount() const { return mRows.getFreedCount(); }
        bool isLive(Index slot) const { return mRows.isLive(slot); }
        std::span<const Row> getRows() const { return mRows.getRows(); }
        std::uint32_t getHolds(Index slot) const { return mRows.getHolds(slot); }

    protected:
        SlotRows<Row> mRows;
    };

    /// Orders rows by the key `KeyOf` takes from each, and takes a bare key on either side, so a
    /// `boost::container::flat_set` of rows is searched by the key alone. One type for both the
    /// insertion and the search, so no reader can disagree with another about whether a key is
    /// held — which is what a `std::lower_bound` at each site with a comparator of its own could.
    ///
    /// @tparam Key what a row is ordered by, a type distinct from the row's.
    /// @tparam KeyOf what a row's key is, as a stateless callable taking the row.
    template <class Key, class KeyOf>
    struct KeyedLess
    {
        using is_transparent = void;

        template <class Left, class Right>
        bool operator()(const Left& left, const Right& right) const
        {
            return keyOf(left) < keyOf(right);
        }

    private:
        /// A key as it is, and a row by what `KeyOf` takes from it. By convertibility and not by
        /// overload, because a lookup by a pointer to non-const is a key too, and an overload on
        /// `const Key&` would lose to the row template's exact match there.
        template <class T>
        static Key keyOf(const T& value)
        {
            if constexpr (std::is_convertible_v<const T&, Key>)
                return value;
            else
                return KeyOf{}(value);
        }
    };
}
