#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <components/rtx/common/index.hpp>

namespace Rtx
{
    /// A row's place in its table's rest (`RestQueue`), kept in the row so a wake takes it out without a search.
    struct RestPlace
    {
        /// Whether nothing names the row and it stands on the device until its room is needed; where it waits among
        /// the rows that came to rest since the queue was last settled, `sNoIndex` once it is filed; and its
        /// neighbours once filed, in the order the rows came to rest.
        bool mResting = false;
        Index mWaitingAt = sNoIndex;
        Index mBefore = sNoIndex;
        Index mAfter = sNoIndex;

        /// When it was filed, on the scene's clock, which orders the rests of every table as one (`SceneDesc`); and
        /// its place among the rows its queue filed, which its lists follow, as one clock tick may file twice.
        std::uint64_t mFiledAt = 0;
        std::uint64_t mOrder = 0;

        /// What its eviction gives back where content the device waits on can take it (`RoomWant`, `FitBytes`), resting
        /// or not, nought until the device has said; and its neighbours once filed among the rows that give back as
        /// much within a power of two, in the order they came to rest: what finds the oldest row whose eviction surely
        /// makes a want its room.
        std::uint64_t mFit = 0;

        /// The coarsest power of two that range starts at (`FitBytes::mAlignment`).
        std::uint64_t mFitAlignment = 1;

        Index mClassBefore = sNoIndex;
        Index mClassAfter = sNoIndex;
    };

    /// **What everything that rests may stand on the device, and how fast it comes down to it**
    /// (`docs/architecture.md`, "One residency policy for the content"): the bytes, and the most freed past the limit
    /// in one eviction beside the room an arrival needs, a row at least, so a limit that drops is followed over several
    /// hand-overs and not in one frame. Unbounded and unpaced by default, for a scene no backend bounds.
    struct RestLimit
    {
        std::uint64_t mBytes = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t mStep = std::numeric_limits<std::uint64_t>::max();
    };

    /// **What rests of one table, oldest first**: the rows nothing names that still stand on the device, a list through
    /// the rows' own `RestPlace`, and the bytes they stand. Those that came to rest since the queue was last settled
    /// wait apart and are filed by their slots, on the scene's clock: a sweep drops them in the order of a hash map,
    /// and two processes that walked one place file them alike. One queue a table, and the scene frees the oldest
    /// across its tables by when each was filed, so there is one order and one limit for everything that rests.
    ///
    /// **And filed again by what each gives back where a want can take it**, a list a power of two, each in the order
    /// the rows came to rest: the oldest row that gives back at least a hole is the oldest head among the classes past
    /// the hole's, or a row of the hole's own class older than that head, so finding it reads a class at most and never
    /// the whole rest.
    ///
    /// `placeOf` hands the queue a row's place at each call, and the queue keeps no reference to the table.
    class RestQueue
    {
    public:
        /// Room for `slots` waiting at once, so no hand-over's rest allocates.
        void reserve(std::size_t slots) { mWaiting.reserve(slots); }

        /// Lays `slot`, standing `bytes` on the device, to rest, into the batch the next `settle` files.
        template <class PlaceOf>
        void rest(const Index slot, const std::uint64_t bytes, PlaceOf&& placeOf)
        {
            RestPlace& place = placeOf(slot);
            assert(!place.mResting && "a row laid to rest twice");
            place.mResting = true;
            place.mWaitingAt = static_cast<Index>(mWaiting.size());
            mWaiting.push_back(slot);
            ++mCount;
            mBytes += bytes;
        }

        /// Takes `slot`, which stood `bytes`, out of its rest, from the batch or the list.
        template <class PlaceOf>
        void wake(const Index slot, const std::uint64_t bytes, PlaceOf&& placeOf)
        {
            RestPlace& place = placeOf(slot);
            assert(place.mResting && "a row woken that does not rest");
            if (place.mWaitingAt != sNoIndex)
            {
                // Out of the batch by the last in its place, which settling sorts anyway.
                const Index last = mWaiting.back();
                mWaiting[place.mWaitingAt] = last;
                placeOf(last).mWaitingAt = place.mWaitingAt;
                mWaiting.pop_back();
            }
            else
            {
                if (place.mBefore != sNoIndex)
                    placeOf(place.mBefore).mAfter = place.mAfter;
                else
                    mFirst = place.mAfter;
                if (place.mAfter != sNoIndex)
                    placeOf(place.mAfter).mBefore = place.mBefore;
                else
                    mLast = place.mBefore;
                unclass(slot, placeOf);
            }
            // What it gives back stays: the device stands what it stood.
            place = RestPlace{ .mFit = place.mFit, .mFitAlignment = place.mFitAlignment };
            assert(mCount > 0 && "a wake where nothing rests");
            assert(mBytes >= bytes && "a wake past the bytes that rest");
            --mCount;
            mBytes -= bytes;
        }

        /// A row that rests now stands `after` bytes where it stood `before`, moved down on the device in its rest; its
        /// place in the order is its own still.
        void restand(const std::uint64_t before, const std::uint64_t after)
        {
            assert(mBytes >= before && "a row restood past the bytes that rest");
            mBytes = mBytes - before + after;
        }

        /// Files the batch at the end of the list, by slot, each stamped `clock`.
        template <class PlaceOf>
        void settle(const std::uint64_t clock, PlaceOf&& placeOf)
        {
            std::ranges::sort(mWaiting);
            for (const Index slot : mWaiting)
            {
                RestPlace& place = placeOf(slot);
                place.mWaitingAt = sNoIndex;
                place.mBefore = mLast;
                place.mAfter = sNoIndex;
                place.mFiledAt = clock;
                place.mOrder = ++mFiled;
                if (mLast != sNoIndex)
                    placeOf(mLast).mAfter = slot;
                else
                    mFirst = slot;
                mLast = slot;

                // Last of its class, as of the queue: filed in one order, the two lists agree on it.
                ClassEnds& ends = mClasses[classOf(place.mFit)];
                place.mClassBefore = ends.mLast;
                place.mClassAfter = sNoIndex;
                if (ends.mLast != sNoIndex)
                    placeOf(ends.mLast).mClassAfter = slot;
                else
                    ends.mFirst = slot;
                ends.mLast = slot;
            }
            mWaiting.clear();
        }

        /// Says `slot` gives back `fit` where a want can take it now, a filed row keeping its place in the order the
        /// rows came to rest: a mesh's copy made tight lands whenever the device answers, a rest or none before.
        template <class PlaceOf>
        void fit(const Index slot, const std::uint64_t fit, const std::uint64_t alignment, PlaceOf&& placeOf)
        {
            assert(alignment > 0 && (alignment & (alignment - 1)) == 0 && "an alignment is a power of two");
            RestPlace& place = placeOf(slot);
            place.mFitAlignment = alignment;
            if (!place.mResting || place.mWaitingAt != sNoIndex)
            {
                place.mFit = fit;
                return;
            }

            unclass(slot, placeOf);
            place.mFit = fit;

            // Back among its new class by when it was filed, from the newest: a copy lands a hand-over or two after
            // its mesh came to rest, which is among the newest of any class.
            ClassEnds& ends = mClasses[classOf(fit)];
            Index after = ends.mLast;
            while (after != sNoIndex && placeOf(after).mOrder > place.mOrder)
                after = placeOf(after).mClassBefore;
            const Index before = after != sNoIndex ? placeOf(after).mClassAfter : ends.mFirst;
            place.mClassBefore = after;
            place.mClassAfter = before;
            if (after != sNoIndex)
                placeOf(after).mClassAfter = slot;
            else
                ends.mFirst = slot;
            if (before != sNoIndex)
                placeOf(before).mClassBefore = slot;
            else
                ends.mLast = slot;
        }

        /// The filed row that has rested longest, or `sNoIndex`; the batch is filed first by `settle`.
        Index first() const { return mFirst; }

        /// The filed row that has rested longest of those that give back at least `hole` at an alignment at least
        /// `alignment`, or `sNoIndex`: the oldest aligned row of the classes past the hole's, which all give back more,
        /// or a row of the hole's own class that gives back as much and was filed before it. Each class is walked from
        /// its oldest to its first aligned row, or to a row filed after what was found.
        template <class PlaceOf>
        Index firstFitting(const std::uint64_t hole, const std::uint64_t alignment, PlaceOf&& placeOf) const
        {
            assert(hole > 0 && "a hole of nought, which any row fits");
            const auto aligned = [&](const Index row) { return placeOf(row).mFitAlignment >= alignment; };
            const std::size_t own = classOf(hole);
            Index found = sNoIndex;
            for (std::size_t past = own + 1; past < mClasses.size(); ++past)
                for (Index row = mClasses[past].mFirst; row != sNoIndex; row = placeOf(row).mClassAfter)
                {
                    if (found != sNoIndex && placeOf(row).mOrder > placeOf(found).mOrder)
                        break;
                    if (aligned(row))
                    {
                        found = row;
                        break;
                    }
                }
            for (Index row = mClasses[own].mFirst; row != sNoIndex; row = placeOf(row).mClassAfter)
            {
                if (found != sNoIndex && placeOf(row).mOrder > placeOf(found).mOrder)
                    break;
                if (placeOf(row).mFit >= hole && aligned(row))
                    return row;
            }
            return found;
        }

        std::size_t getCount() const { return mCount; }
        std::uint64_t getBytes() const { return mBytes; }

    private:
        /// The first and last filed row of one class.
        struct ClassEnds
        {
            Index mFirst = sNoIndex;
            Index mLast = sNoIndex;
        };

        /// The class of what gives back `fit`: its bit width, so a class holds `[2^(c-1), 2^c)` and the first nought.
        static std::size_t classOf(const std::uint64_t fit) { return static_cast<std::size_t>(std::bit_width(fit)); }

        /// Takes the filed `slot` out of its class's list.
        template <class PlaceOf>
        void unclass(const Index slot, PlaceOf&& placeOf)
        {
            RestPlace& place = placeOf(slot);
            ClassEnds& ends = mClasses[classOf(place.mFit)];
            if (place.mClassBefore != sNoIndex)
                placeOf(place.mClassBefore).mClassAfter = place.mClassAfter;
            else
                ends.mFirst = place.mClassAfter;
            if (place.mClassAfter != sNoIndex)
                placeOf(place.mClassAfter).mClassBefore = place.mClassBefore;
            else
                ends.mLast = place.mClassBefore;
            place.mClassBefore = sNoIndex;
            place.mClassAfter = sNoIndex;
        }

        std::array<ClassEnds, 65> mClasses{};
        std::uint64_t mFiled = 0;
        Index mFirst = sNoIndex;
        Index mLast = sNoIndex;
        std::vector<Index> mWaiting;
        std::size_t mCount = 0;
        std::uint64_t mBytes = 0;
    };
}
