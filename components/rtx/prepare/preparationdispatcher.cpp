#include "preparationdispatcher.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Rtx
{
    PreparationDispatcher::PreparationDispatcher(
        const std::size_t threads, const std::size_t walks, const std::optional<std::uint64_t> budget)
        : mThreads(threads)
        , mWalks(walks)
        , mBudget(budget)
    {
        assert(threads >= 1 && "a dispatcher with no thread to hand work to");
    }

    std::size_t PreparationDispatcher::queue(const ChainNeed& need)
    {
        const std::size_t chain = mChains.size();
        mChains.push_back(need);
        mQueuedImages += need.mImageBytes;

        // The largest scratch first, and of two alike the one queued first.
        const auto at = std::upper_bound(mQueue.begin(), mQueue.end(), need.mScratch.total(),
            [&](std::size_t total, std::size_t queued) { return total > mChains[queued].mScratch.total(); });
        mQueue.insert(at, chain);
        return chain;
    }

    std::uint64_t PreparationDispatcher::keptOf(const Thread& thread) const
    {
        return thread.mKept.total() + thread.mWalkKept;
    }

    std::uint64_t PreparationDispatcher::growthOf(const Thread& thread, const ChainNeed& chain) const
    {
        return thread.mKept.growthTo(chain.mScratch);
    }

    std::uint64_t PreparationDispatcher::getHeld() const
    {
        std::uint64_t held = mQueuedImages;
        for (const Thread& thread : mThreads)
            held += keptOf(thread) + thread.mAdds;
        return held;
    }

    bool PreparationDispatcher::fits(const std::uint64_t adds) const
    {
        return !mBudget.has_value() || getHeld() + adds <= *mBudget;
    }

    bool PreparationDispatcher::isFinished() const
    {
        return mQueue.empty() && mNextWalk == mWalks
            && std::ranges::all_of(mThreads, [](const Thread& thread) { return thread.mState == State::Idle; });
    }

    std::optional<PreparationDispatcher::Reservation> PreparationDispatcher::reserve(const std::uint64_t needs) const
    {
        assert(mBudget.has_value() && "a reservation where nothing is bounded");

        // The room now with every idle thread's scratch let go, then each running job's at its end, the
        // earliest first.
        const std::uint64_t held = getHeld();
        std::uint64_t room = *mBudget - std::min(held, *mBudget);
        std::vector<const Thread*> running;
        for (const Thread& thread : mThreads)
        {
            if (thread.mState == State::Idle)
                room += keptOf(thread);
            else
                running.push_back(&thread);
        }
        if (room >= needs)
            return Reservation{ .mAt = 0.0, .mSpare = room - needs };

        std::ranges::sort(running, [](const Thread* a, const Thread* b) { return a->mEnds < b->mEnds; });
        for (const Thread* thread : running)
        {
            room += keptOf(*thread) + thread->mAdds;
            if (room >= needs)
                return Reservation{ .mAt = thread->mEnds, .mSpare = room - needs };
        }
        return std::nullopt;
    }

    Dispatch PreparationDispatcher::startChain(
        const std::size_t thread, const std::size_t at, const double now, const bool alone)
    {
        Thread& taker = mThreads[thread];
        const std::size_t chain = mQueue[at];
        const ChainNeed& need = mChains[chain];
        mQueue.erase(mQueue.begin() + static_cast<std::ptrdiff_t>(at));
        mQueuedImages -= need.mImageBytes;

        taker.mState = State::Making;
        taker.mAdds = growthOf(taker, need) + need.mImageBytes;
        taker.mEnds = now + need.mSeconds;
        taker.mChain = chain;
        mHeldMost = std::max(mHeldMost, getHeld());
        return Dispatch{ .mKind = Dispatch::Kind::Chain, .mJob = chain, .mAlone = alone };
    }

    Dispatch PreparationDispatcher::next(const std::size_t thread, const double now)
    {
        Thread& me = mThreads[thread];
        assert(me.mState == State::Idle && "a thread asked for work while it held a job");

        if (std::exchange(me.mAskedToLetGo, false) && keptOf(me) > 0)
            return Dispatch{ .mKind = Dispatch::Kind::LetGo };

        const bool running
            = std::ranges::any_of(mThreads, [](const Thread& other) { return other.mState != State::Idle; });
        if (mQueue.empty() && mNextWalk == mWalks)
            return Dispatch{ .mKind = running ? Dispatch::Kind::Wait : Dispatch::Kind::Finished };

        // **Every other idle thread asked to let go of its scratch**, where a chain waits on the room:
        // answers whether one still keeps any.
        const auto othersLetGo = [&] {
            bool keeping = false;
            for (std::size_t at = 0; at < mThreads.size(); ++at)
            {
                Thread& other = mThreads[at];
                if (at != thread && other.mState == State::Idle && keptOf(other) > 0)
                {
                    other.mAskedToLetGo = true;
                    keeping = true;
                }
            }
            return keeping;
        };

        std::optional<Reservation> reservation;
        if (!mQueue.empty())
        {
            const ChainNeed& head = mChains[mQueue.front()];

            // **On the idle thread it grows least**, which keeps the room the largest chains already took:
            // this thread's where none grows it less.
            const std::uint64_t mineGrows = growthOf(me, head);
            std::uint64_t least = mineGrows;
            for (std::size_t at = 0; at < mThreads.size(); ++at)
                if (at != thread && mThreads[at].mState == State::Idle)
                    least = std::min(least, growthOf(mThreads[at], head));
            const bool mine = mineGrows == least;

            // **Alone where it fits in no room the others leave**: past the whole budget, or where nothing
            // runs to free what the idle threads and the queue hold. Everything else waits for it, and
            // every other thread lets go of its scratch first.
            const bool pastBudget = mBudget.has_value() && head.mScratch.total() > *mBudget;
            if (pastBudget || (!running && !fits(least)))
            {
                if (running)
                    return Dispatch{};
                if (othersLetGo())
                    return Dispatch{ .mWakes = true };
                if (!fits(growthOf(me, head)) && keptOf(me) > 0)
                    return Dispatch{ .mKind = Dispatch::Kind::LetGo };
                return startChain(thread, 0, now, pastBudget);
            }

            if (mine && fits(least))
                return startChain(thread, 0, now, false);

            // **The head the thread it grows least takes, once woken**, and this one backfills beside it.
            if (!mine && fits(least))
            {
                for (std::size_t at = 1; at < mQueue.size(); ++at)
                {
                    const ChainNeed& chain = mChains[mQueue[at]];
                    if (fits(least + growthOf(me, chain)))
                        return startChain(thread, at, now, false);
                }
                return Dispatch{ .mWakes = true };
            }

            // Room the idle threads' kept scratch holds, let go where the head then fits.
            if (mine && mBudget.has_value())
            {
                std::uint64_t idleKept = 0;
                for (std::size_t at = 0; at < mThreads.size(); ++at)
                    if (at != thread && mThreads[at].mState == State::Idle)
                        idleKept += keptOf(mThreads[at]);
                if (idleKept > 0 && getHeld() - idleKept + least <= *mBudget)
                {
                    othersLetGo();
                    return Dispatch{ .mWakes = true };
                }
            }

            // **The head's time reserved, and a smaller chain started before it** only where it ends by
            // then or fits in what the head leaves spare (EASY backfill).
            if (mBudget.has_value())
                reservation = reserve(head.mScratch.total());
            for (std::size_t at = 1; at < mQueue.size(); ++at)
            {
                const ChainNeed& chain = mChains[mQueue[at]];
                const std::uint64_t growth = growthOf(me, chain);
                if (!fits(growth))
                    continue;
                if (!reservation.has_value() || now + chain.mSeconds <= reservation->mAt
                    || growth + chain.mImageBytes <= reservation->mSpare)
                    return startChain(thread, at, now, false);
            }
        }

        // **A walk where the queue runs short of the threads**, counted as the largest walk's images met so
        // far until it says its own, and within what a reserved head leaves spare.
        if (mNextWalk < mWalks && mQueue.size() < mThreads.size()
            && (!running || (fits(mWalkImages) && (!reservation.has_value() || mWalkImages <= reservation->mSpare))))
        {
            me.mState = State::Walking;
            me.mAdds = mWalkImages;
            mHeldMost = std::max(mHeldMost, getHeld());
            return Dispatch{ .mKind = Dispatch::Kind::Walk, .mJob = mNextWalk++ };
        }

        return Dispatch{};
    }

    void PreparationDispatcher::ended(const std::size_t thread, const ChainScratch& kept, const std::uint64_t walkKept,
        const std::uint64_t walkImages)
    {
        Thread& me = mThreads[thread];
        assert(me.mState != State::Idle && "a thread ended a job it did not hold");
        if (me.mState == State::Walking)
            mWalkImages = std::max(mWalkImages, walkImages);
        else
        {
            // **A make grows no buffer past the larger of what it kept and what it was predicted to need.**
            [[maybe_unused]] ChainScratch bound = me.mKept;
            bound.keepLarger(mChains[me.mChain].mScratch);
            assert(kept.within(bound) && "a chain's make grew a buffer past its prediction");
        }

        me.mKept = kept;
        me.mWalkKept = walkKept;
        me.mAdds = 0;
        me.mState = State::Idle;
    }

    void PreparationDispatcher::letGo(const std::size_t thread)
    {
        Thread& me = mThreads[thread];
        assert(me.mState == State::Idle && "a thread let go of its scratch while it held a job");
        me.mKept = ChainScratch{};
        me.mWalkKept = 0;
    }
}
