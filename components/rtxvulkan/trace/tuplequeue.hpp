#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace Rtx
{
    /// One tuple's run of a `TupleQueue`: which tuple, and how many items make it.
    struct TupleRun
    {
        std::uint32_t mTuple = 0;
        std::uint32_t mItems = 0;
    };

    /// Work that makes tuples out of items, which hands sharing it claim an item at a time: a run of
    /// items a tuple, numbered in the runs' order, and a tuple made when its last item lands. **A
    /// tuple a frame waits on is claimed first** (`claim`), as Dolphin and Godot compile the shader a
    /// frame stands on ahead of the rest; every other in the runs' order.
    class TupleQueue
    {
    public:
        /// @param runs each tuple at most once.
        /// @param tuples how many tuple indices there are, each run's under it.
        TupleQueue(std::span<const TupleRun> runs, std::uint32_t tuples);

        TupleQueue(const TupleQueue&) = delete;
        TupleQueue& operator=(const TupleQueue&) = delete;

        std::uint32_t getItemCount() const { return mItems; }

        /// The next item to make: `wanted`'s while its run has one, the first run's with one
        /// otherwise; `wanted` past the tuples, or one with no run, asks for none. **Asked once an
        /// item**, by as many claims as there are items, so no claim finds none: a claim that does
        /// is a contract broken, and ends the process.
        std::uint32_t claim(std::uint32_t wanted);

        /// Says an item of `tuple` was made, and answers whether it was the tuple's last.
        bool land(std::uint32_t tuple);

    private:
        /// The claim of `tuple`'s next item, or `sNone` where its run is claimed whole.
        std::uint32_t take(std::uint32_t tuple);

        static constexpr std::uint32_t sNone = ~std::uint32_t{ 0 };

        std::vector<std::uint32_t> mOrder;
        std::vector<std::uint32_t> mStarts;
        std::vector<std::uint32_t> mEnds;
        std::vector<std::atomic<std::uint32_t>> mClaimed;
        std::vector<std::atomic<std::uint32_t>> mLeft;
        std::uint32_t mItems = 0;
    };
}
