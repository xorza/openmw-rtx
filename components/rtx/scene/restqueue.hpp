#pragma once

#include <algorithm>
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

        /// When it was filed, on the scene's clock, which orders the rests of every table as one (`SceneDesc`).
        std::uint64_t mFiledAt = 0;
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
            }
            place = RestPlace{};
            assert(mCount > 0 && "a wake where nothing rests");
            assert(mBytes >= bytes && "a wake past the bytes that rest");
            --mCount;
            mBytes -= bytes;
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
                if (mLast != sNoIndex)
                    placeOf(mLast).mAfter = slot;
                else
                    mFirst = slot;
                mLast = slot;
            }
            mWaiting.clear();
        }

        /// The filed row that has rested longest, or `sNoIndex`; the batch is filed first by `settle`.
        Index first() const { return mFirst; }

        std::size_t getCount() const { return mCount; }
        std::uint64_t getBytes() const { return mBytes; }

    private:
        Index mFirst = sNoIndex;
        Index mLast = sNoIndex;
        std::vector<Index> mWaiting;
        std::size_t mCount = 0;
        std::uint64_t mBytes = 0;
    };
}
