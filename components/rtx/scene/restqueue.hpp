#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <components/rtx/common/index.hpp>

namespace Rtx
{
    /// A row's place in its table's rest (`RestQueue`), kept in the row so a wake takes it out without a search.
    ///
    /// @tparam I the table's row (`RowIndex`).
    template <class I>
    struct RestPlace
    {
        /// Where a row that waits is in no batch.
        static constexpr std::uint32_t sNotWaiting = ~std::uint32_t{ 0 };

        /// Whether nothing names the row and residency still stands it on the device; where it waits among the rows
        /// that came to rest since the queue was last settled, `sNotWaiting` once it is filed; and its neighbours once
        /// filed, in the order the rows came to rest.
        bool mResting = false;
        std::uint32_t mWaitingAt = sNotWaiting;
        I mBefore = sNoRow;
        I mAfter = sNoRow;
    };

    /// **What rests of one table, oldest first**: the rows nothing names that residency still stands, a list through
    /// the rows' own `RestPlace`, which a lookup by key takes back and a full table frees from the oldest. Those that
    /// came to rest since the queue was last settled wait apart and are filed by their slots: a sweep drops them in
    /// the order of a hash map, and two processes that walked one place file them alike. What rests on the device and
    /// for how long is residency's (`Residency`'s cache); a row leaves this as residency stops standing it.
    ///
    /// `placeOf` hands the queue a row's place at each call, and the queue keeps no reference to the table.
    ///
    /// @tparam I the table's row (`RowIndex`).
    template <class I>
    class RestQueue
    {
        static constexpr std::uint32_t sNotWaiting = RestPlace<I>::sNotWaiting;

    public:
        /// Room for `slots` waiting at once, so no hand-over's rest allocates.
        void reserve(std::size_t slots) { mWaiting.reserve(slots); }

        /// Lays `slot` to rest, into the batch the next `settle` files.
        template <class PlaceOf>
        void rest(const I slot, PlaceOf&& placeOf)
        {
            RestPlace<I>& place = placeOf(slot);
            assert(!place.mResting && "a row laid to rest twice");
            place.mResting = true;
            place.mWaitingAt = static_cast<std::uint32_t>(mWaiting.size());
            mWaiting.push_back(slot);
            ++mCount;
        }

        /// Takes `slot` out of its rest, from the batch or the list.
        template <class PlaceOf>
        void wake(const I slot, PlaceOf&& placeOf)
        {
            RestPlace<I>& place = placeOf(slot);
            assert(place.mResting && "a row woken that does not rest");
            if (place.mWaitingAt != sNotWaiting)
            {
                // Out of the batch by the last in its place, which settling sorts anyway.
                const I last = mWaiting.back();
                mWaiting[place.mWaitingAt] = last;
                placeOf(last).mWaitingAt = place.mWaitingAt;
                mWaiting.pop_back();
            }
            else
            {
                if (place.mBefore != sNoRow)
                    placeOf(place.mBefore).mAfter = place.mAfter;
                else
                    mFirst = place.mAfter;
                if (place.mAfter != sNoRow)
                    placeOf(place.mAfter).mBefore = place.mBefore;
                else
                    mLast = place.mBefore;
            }
            place = RestPlace<I>{};
            assert(mCount > 0 && "a wake where nothing rests");
            --mCount;
        }

        /// Files the batch at the end of the list, by slot.
        template <class PlaceOf>
        void settle(PlaceOf&& placeOf)
        {
            std::ranges::sort(mWaiting);
            for (const I slot : mWaiting)
            {
                RestPlace<I>& place = placeOf(slot);
                place.mWaitingAt = sNotWaiting;
                place.mBefore = mLast;
                place.mAfter = sNoRow;
                if (mLast != sNoRow)
                    placeOf(mLast).mAfter = slot;
                else
                    mFirst = slot;
                mLast = slot;
            }
            mWaiting.clear();
        }

        /// The filed row that has rested longest, or `sNoRow`; the batch is filed first by `settle`.
        I first() const { return mFirst; }

        std::size_t getCount() const { return mCount; }

    private:
        I mFirst = sNoRow;
        I mLast = sNoRow;
        std::vector<I> mWaiting;
        std::size_t mCount = 0;
    };
}
