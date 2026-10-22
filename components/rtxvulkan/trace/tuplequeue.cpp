#include "tuplequeue.hpp"

#include <cassert>

#include <components/crashcatcher/crash.hpp>

namespace Rtx
{
    TupleQueue::TupleQueue(const std::span<const TupleRun> runs, const std::uint32_t tuples)
        : mStarts(tuples, 0)
        , mEnds(tuples, 0)
        , mClaimed(tuples)
        , mLeft(tuples)
    {
        mOrder.reserve(runs.size());
        for (const TupleRun& run : runs)
        {
            assert(run.mTuple < tuples && "a run past the tuples");
            assert(mEnds[run.mTuple] == mStarts[run.mTuple] && "a tuple run twice");
            mStarts[run.mTuple] = mItems;
            mItems += run.mItems;
            mEnds[run.mTuple] = mItems;
            mClaimed[run.mTuple].store(mStarts[run.mTuple], std::memory_order_relaxed);
            mLeft[run.mTuple].store(run.mItems, std::memory_order_relaxed);
            mOrder.push_back(run.mTuple);
        }
    }

    std::uint32_t TupleQueue::take(const std::uint32_t tuple)
    {
        // Read first, so a run claimed whole is not counted past its end by every claim after.
        if (mClaimed[tuple].load(std::memory_order_relaxed) >= mEnds[tuple])
            return sNone;
        const std::uint32_t at = mClaimed[tuple].fetch_add(1, std::memory_order_relaxed);
        return at < mEnds[tuple] ? at : sNone;
    }

    std::uint32_t TupleQueue::claim(const std::uint32_t wanted)
    {
        if (wanted < mEnds.size())
            if (const std::uint32_t at = take(wanted); at != sNone)
                return at;

        for (const std::uint32_t tuple : mOrder)
            if (const std::uint32_t at = take(tuple); at != sNone)
                return at;

        Crash::fatal("a claim on a tuple queue found no item left");
    }

    bool TupleQueue::land(const std::uint32_t tuple)
    {
        return mLeft[tuple].fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
}
