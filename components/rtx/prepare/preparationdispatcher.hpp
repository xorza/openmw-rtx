#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <components/rtx/preprocess/texture/chainscratch.hpp>

namespace Rtx
{
    /// What a free thread is handed (`PreparationDispatcher::next`).
    struct Dispatch
    {
        enum class Kind
        {
            /// Walk the next model or ground layer, `mJob`.
            Walk,
            /// Make the queued chain `mJob`.
            Chain,
            /// Let go of the scratch the thread keeps, which a waiting chain needs the room of.
            LetGo,
            /// Nothing fits now: wait for a thread to end what it does.
            Wait,
            /// Every job is done: the thread ends.
            Finished,
        };

        Kind mKind = Kind::Wait;
        std::size_t mJob = 0;

        /// Whether the chain is past the whole budget and runs alone, which the caller logs.
        bool mAlone = false;

        /// Whether other idle threads were asked to let go of their scratch, which the caller wakes.
        bool mWakes = false;
    };

    /// What a chain needs, as its walk queued it (`PreparationDispatcher::queue`).
    struct ChainNeed
    {
        /// Its make's scratch at most (`ChainPass::scratchOf`, `ConversionPass::scratchOf`).
        ChainScratch mScratch;

        /// The bytes of the images it holds from its walk to its end.
        std::uint64_t mImageBytes = 0;

        /// How long it is estimated to take, which orders a backfill and never admits one.
        double mSeconds = 0.0;
    };

    /// Which job a free thread takes next, within a budget of memory where the system states one
    /// (`docs/rtx/architecture.md`, "The preparation's memory is dispatched"): queued chains largest
    /// first, each on the thread whose kept scratch it grows least; a chain that fits nowhere reserves
    /// the time the running jobs free its room, and smaller ones start before it only where they end by
    /// then or fit beside it (EASY backfill); a chain past the whole budget runs alone.
    class PreparationDispatcher
    {
    public:
        /// @param threads how many threads ask.
        /// @param walks how many walks there are, models and ground layers.
        /// @param budget the bytes the jobs may hold together, or nothing for no bound: the threads alone.
        PreparationDispatcher(std::size_t threads, std::size_t walks, std::optional<std::uint64_t> budget);

        /// Queues a chain a walk claimed, its images counted from now, and answers its number.
        std::size_t queue(const ChainNeed& need);

        /// What `thread`, free, does next, at `now` seconds since the start.
        Dispatch next(std::size_t thread, double now);

        /// `thread` ended its job: the scratch it keeps now, as it measured it, and the other bytes a walk
        /// keeps beside it; for a walk, the bytes of the images it loaded.
        void ended(std::size_t thread, const ChainScratch& kept, std::uint64_t walkKept, std::uint64_t walkImages);

        /// `thread` let go of its scratch.
        void letGo(std::size_t thread);

        /// What the jobs hold together now, as counted: what a test and a report read.
        std::uint64_t getHeld() const;

        /// The most they held together, as counted, each time a job started.
        std::uint64_t getHeldMost() const { return mHeldMost; }

        /// Whether every walk and every chain ended.
        bool isFinished() const;

    private:
        enum class State
        {
            Idle,
            Walking,
            Making,
        };

        struct Thread
        {
            ChainScratch mKept;
            std::uint64_t mWalkKept = 0;
            State mState = State::Idle;

            /// What its job adds to what it keeps, and when it is estimated to end.
            std::uint64_t mAdds = 0;
            double mEnds = 0.0;
            std::size_t mChain = 0;
            bool mAskedToLetGo = false;
        };

        /// What making `chain` adds on `thread`: its scratch past what the thread keeps.
        std::uint64_t growthOf(const Thread& thread, const ChainNeed& chain) const;
        std::uint64_t keptOf(const Thread& thread) const;
        bool fits(std::uint64_t adds) const;

        /// The earliest time the running jobs free the room `needs`, idle threads' scratch let go, and the
        /// room left beside it then; nothing where nothing running frees it.
        struct Reservation
        {
            double mAt = 0.0;
            std::uint64_t mSpare = 0;
        };
        std::optional<Reservation> reserve(std::uint64_t needs) const;

        Dispatch startChain(std::size_t thread, std::size_t at, double now, bool alone);

        std::vector<Thread> mThreads;
        std::vector<ChainNeed> mChains;

        /// The queued chains' numbers, the largest scratch first.
        std::vector<std::size_t> mQueue;

        std::size_t mWalks;
        std::size_t mNextWalk = 0;

        /// The bytes of the images the largest walk met so far loaded: what a walk is counted as until it
        /// ends.
        std::uint64_t mWalkImages = 0;

        std::optional<std::uint64_t> mBudget;

        /// Every queued chain's images.
        std::uint64_t mQueuedImages = 0;

        std::uint64_t mHeldMost = 0;
    };
}
