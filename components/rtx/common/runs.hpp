#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "index.hpp"

namespace Rtx
{
    /// `into`, refilled with every row below `count`: a whole table walked through the path a list
    /// of arrivals takes.
    template <class I>
    std::span<const I> everyIndexBelow(const std::size_t count, std::vector<I>& into)
    {
        into.clear();
        into.reserve(count);
        for (std::uint32_t at = 0; at < count; ++at)
            into.push_back(I(at));
        return into;
    }

    /// Splits the items below `count` into runs whose bytes fit `room`, in order, each run worked over the room the
    /// one before used: an item that would carry its run past the room starts the next, and one larger than the room
    /// is a run of its own, whose room is then what it needs. `bytesOf(item)` is an item's bytes, or nothing for an
    /// item that takes no part; `place(item, offset)` is told where in its run's room each item that does starts.
    /// `ends` is refilled with where each run ends, nothing where no item takes part, and what is answered is the
    /// largest run's bytes, which the room is grown to.
    template <class BytesOf, class Place>
    std::uint64_t splitIntoRuns(const std::size_t count, const std::uint64_t room, BytesOf&& bytesOf, Place&& place,
        std::vector<std::size_t>& ends)
    {
        ends.clear();
        std::uint64_t run = 0;
        std::uint64_t most = 0;
        bool holds = false;
        for (std::size_t at = 0; at < count; ++at)
        {
            const std::optional<std::uint64_t> bytes = bytesOf(at);
            if (!bytes.has_value())
                continue;

            if (holds && run + *bytes > room)
            {
                ends.push_back(at);
                run = 0;
            }

            place(at, run);
            run += *bytes;
            holds = true;
            most = std::max(most, run);
        }

        if (holds)
            ends.push_back(count);
        return most;
    }

    /// A run inside a buffer: where it starts, and how many elements it holds. A mesh's vertices,
    /// a material's layers, a layer's mask weights and an emitter's sprites are all runs.
    struct Run
    {
        std::uint32_t mOffset = 0;
        std::uint32_t mCount = 0;

        std::uint32_t getEnd() const { return mOffset + mCount; }
        bool empty() const { return mCount == 0; }

        /// The part of `all` this run names.
        template <class T>
        std::span<T> in(std::span<T> all) const
        {
            return all.subspan(mOffset, mCount);
        }

        bool operator==(const Run& other) const = default;
    };

    /// Hands out runs inside a buffer whose contents never move: closing a hole by moving what is
    /// above it would renumber every offset a shader reads and every device address a
    /// bottom-level acceleration structure was built from. Best fit and not first fit, because the
    /// free list is tens of entries and first fit would spend a cathedral's hole on a crate. Freed
    /// runs that touch are merged.
    class RunAllocator
    {
    public:
        /// @param block a boundary no run may straddle, or zero for a buffer with no such rule —
        ///        what makes appending to a device buffer possible, because a blocked buffer is a
        ///        list of allocations made once and never moved. The tail of a block too short for
        ///        the next run becomes a hole like any other.
        explicit RunAllocator(std::uint32_t block = 0)
            : mBlock(block)
        {
        }

        /// A run of `count` elements, taken from the smallest hole that can hold it, and appended
        /// past the end when none can. `count` must be at least one and, where there is a block
        /// size, no larger than it.
        Run allocate(std::uint32_t count);

        /// Gives a run back. Merged with whatever it touches, and an empty run is not a run. The
        /// run must be one `allocate` returned and must not already be free — asserted, off the
        /// two holes it lands between, since a release finds them anyway.
        void release(Run span);

        /// Forgets every run. The buffer behind it is emptied by whoever owns it.
        void clear();

        /// How far into the buffer anything has ever reached, which is how long the buffer has to
        /// be. Runs given back at the very end shrink it again.
        std::uint32_t getEnd() const { return mEnd; }

        /// How many elements below `getEnd` are in holes.
        std::uint32_t getFree() const;

        /// How many are handed out, which is what a report of a buffer built on this calls live.
        std::uint32_t getUsed() const { return getEnd() - getFree(); }

        // Read by the tests and by nothing else.
        /// How many separate holes those elements are in: a measure of fragmentation, and what says
        /// that releases merged.
        std::size_t getHoleCount() const { return mFree.size(); }

    private:
        /// Where in `hole` a run of `count` can go without straddling a block, or a count of zero
        /// where it cannot go there at all.
        Run place(const Run& hole, std::uint32_t count) const;

        /// The holes, ordered by offset and never touching one another. Ordered so that a release
        /// can find its neighbours, and disjoint-and-separated so that finding them is enough.
        std::vector<Run> mFree;

        std::uint32_t mEnd = 0;
        std::uint32_t mBlock = 0;
    };

    /// A `RunAllocator` and the buffer its runs name, so the buffer reaches the allocator's end
    /// before anything is written into it. Grown and never shrunk. A buffer in blocks is a
    /// `RunAllocator` of the block beside the `BlockedValues` it names, and `DeformerTable`'s bind
    /// runs name a table the backend owns, which is why `RunAllocator` also stands alone.
    template <class T>
    class RunBuffer
    {
    public:
        /// Holds room for `count` elements, so no allocation up to it moves the values.
        void reserve(std::uint32_t count) { mValues.reserve(count); }

        /// Room for `count` elements, holding whatever its last tenant left, so a caller writes the
        /// whole of it before anything reads it. `allocateZeroed` is the other answer.
        Run allocate(std::uint32_t count)
        {
            const Run run = mRuns.allocate(count);
            if (mValues.size() < mRuns.getEnd())
                mValues.resize(mRuns.getEnd());

            return run;
        }

        /// The same room, holding `values`.
        Run allocate(std::span<const T> values)
        {
            assert(!values.empty() && "a run of nothing is not a run");

            const Run run = allocate(static_cast<std::uint32_t>(values.size()));
            std::copy(values.begin(), values.end(), mValues.begin() + run.mOffset);

            return run;
        }

        /// The same room, value-initialised. A pose nothing can equal, which is what
        /// `DeformerTable::stand` hands a mesh that has not been posed yet.
        Run allocateZeroed(std::uint32_t count)
        {
            const Run run = allocate(count);
            std::fill_n(mValues.begin() + run.mOffset, count, T{});

            return run;
        }

        /// Gives a run back. Merged with whatever it touches, and the buffer keeps its room.
        void release(Run run) { mRuns.release(run); }

        std::span<const T> getAll() const { return mValues; }

        /// One run's elements, for a caller writing into what it took.
        std::span<T> in(Run run) { return run.in(std::span<T>(mValues)); }

        /// How far into the buffer anything has ever reached, which is how much of it is uploaded.
        std::uint32_t getEnd() const { return mRuns.getEnd(); }

        std::uint32_t getUsed() const { return mRuns.getUsed(); }

        // Read by the tests and by nothing else.
        std::size_t getHoleCount() const { return mRuns.getHoleCount(); }

    private:
        RunAllocator mRuns;
        std::vector<T> mValues;
    };
}
