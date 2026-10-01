#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <osg/Vec2i>

#include <components/platform/thread.hpp>
#include <components/rtx/common/monitor.hpp>
#include <components/rtx/common/worker.hpp>

#include "cellworld.hpp"
#include "readermemory.hpp"

namespace Rtx
{
    class CellReader;
    struct PreparedCell;
    struct PreparedModel;
    struct PreparedTexture;

    /// What the reading thread is to read next: the cells, and what to read of each. One value,
    /// because a list that reaches the thread beside the other switch is a cell read under a rule
    /// nobody asked for.
    struct CellRequest
    {
        std::vector<osg::Vec2i> mCells;

        /// Whether to read each cell's references at all, which is the ring's statics switch as it
        /// stood when the ask was made.
        bool mStatics = true;

        bool operator==(const CellRequest& other) const = default;

        bool empty() const { return mCells.empty(); }

        /// Empties the list and keeps the room it grew. The switch is left as it was, because a
        /// request with no cells in it says nothing about either.
        void clear() { mCells.clear(); }

        /// Takes what `from` holds, leaving it empty and keeping the room both grew.
        void take(CellRequest& from);
    };

    /// What the frame has finished with, on its way back to the reader: a cell, the models it
    /// named and the images its ground named leave together.
    struct CellReturns
    {
        std::vector<PreparedCell*> mCells;
        std::vector<PreparedModel*> mModels;
        std::vector<PreparedTexture*> mTextures;

        bool empty() const { return mCells.empty() && mModels.empty() && mTextures.empty(); }

        /// Empties all three, keeping the room each grew.
        void clear();

        /// Appends everything `from` holds and empties it.
        void take(CellReturns& from);
    };

    /// Cells read ahead of the eye, on a thread of its own: the thread, the reader it drives, and
    /// the two channels between them — what to read next, and what the frame has finished with. A
    /// newer ask replaces an older one it has not finished, because the ones the ring lacked a
    /// moment ago are not an answer to what it lacks now. Everything read is lent and given back;
    /// `Spares` says why an address and not a shared count.
    ///
    /// **A cell is read once for the asks that name it while it is on its way.** An ask names
    /// what the ring lacks as of its last `take`, so the cell in hand, and every cell handed over
    /// since that take, are in the next ask as well: read again, they were 42 of the 143 reads a
    /// band of 101 cells took, and every copy was turned away. A take counts, so the thread knows which of what it
    /// handed over an ask could not have known of.
    class CellSupply
    {
    public:
        CellSupply();

        /// Stops the thread. A cell in flight is finished and dropped rather than waited out.
        ~CellSupply();

        /// Whether this is already reading `world`, so a caller need not let go of what it holds.
        bool isReading(const CellWorld& world) const { return mWorld == world; }

        /// Whether there is a reader at all, which a world with no content answers no to.
        bool hasReader() const { return mReader != nullptr; }

        /// Points it at `world`, stopping and joining whatever it was reading and forgetting both
        /// channels. The caller lets go of what it held first, or a hold handed back would reach a
        /// reader that never lent it.
        void follow(const CellWorld& world);

        /// Hands the thread `request`. Costs nothing where it equals the last one handed over.
        void ask(const CellRequest& request);

        /// Moves what the thread has read into `into`, appended. Empty where it has read nothing.
        void take(std::vector<PreparedCell*>& into);

        /// Blocks until the thread has read at least one more cell, for a settled run
        /// (`CellRing::setSettled`), and says whether it did: false where the thread has nothing
        /// left to read — its list finished or cancelled by a newer ask — so the wait ends rather
        /// than waiting for a cell nobody is reading.
        bool waitForOne();

        /// Where a caller puts what it has finished with. Handed over by `publish`.
        CellReturns& giveBack()
        {
            mOnFrame.check();
            return mReturning;
        }

        /// Hands the thread everything `giveBack` collected. Nothing where there is none.
        void publish();

        /// What the reader kept of its models when it last finished a cell. Nought before the
        /// first, and where there is no reader.
        ReaderMemory getReaderMemory();

    private:
        /// The reader's loop: a list at a time, until asked to stop. `Monitor::serve` is the loop,
        /// and this is what it takes and what it does.
        void work(const Platform::StopToken& stop);

        /// Reads the list `work` took, a cell at a time, and stops at the first sign of a newer
        /// one. On the thread, outside the lock but for what it hands over.
        void read(const Platform::StopToken& stop);

        /// Gives the reader what the frame gave back, taken out of `mReturned` under the lock into
        /// `mRecycling`. On the thread, outside the lock: putting a cell's memory away is the
        /// reader's work, and the frame takes the same lock for its own.
        void recycle();

        CellWorld mWorld;

        /// Which thread the two below belong to. The frame's, and `mOnFrame.check()` is what
        /// says so at each of the calls that touch them.
        OwnedBy mOnFrame;

        /// The frame's side: the last ask handed over, and what it is collecting to hand back.
        CellRequest mRequested;
        CellReturns mReturning;

        /// The lock between the frame and the reader, and the two waits across it. It guards the
        /// five below and nothing else.
        Monitor mMonitor;

        /// What the thread is to read next, written whole under the lock and taken whole by the
        /// thread. A newer request replaces an older one it has not finished.
        CellRequest mWanted;

        /// How many asks were handed over, which is what tells the thread its list is no longer
        /// wanted: an ask for nothing leaves `mWanted` as empty as a taken one, and has to cancel
        /// the list in flight all the same.
        std::uint64_t mAsked = 0;

        /// What the thread has read, under the lock.
        std::vector<PreparedCell*> mDone;

        /// How many times the frame has taken what was read, and how many it had when it made the
        /// ask in `mWanted`, under the lock.
        std::uint64_t mTakes = 0;
        std::uint64_t mWantedTakes = 0;

        /// What the frame has given back, under the lock, for the thread to refill.
        CellReturns mReturned;

        /// `CellReader::measure` as the thread last finished a cell, under the lock.
        ReaderMemory mMeasured;

        /// A cell the thread handed over, read with which statics, and how many takes the frame had
        /// made by then: it reaches the ring with the next take, so an ask made before that take
        /// names it still.
        struct Handed
        {
            osg::Vec2i mCell;
            bool mStatics = true;
            std::uint64_t mTakes = 0;
        };

        /// The thread's own: the request it is working through, the ask it came from and the takes
        /// that ask knew of, what the frame gave back that it is putting away, and what it handed
        /// over that the request cannot have known of.
        CellRequest mReading;
        std::uint64_t mReadingAsked = 0;
        std::uint64_t mReadingTakes = 0;
        CellReturns mRecycling;
        std::vector<Handed> mOnTheWay;

        /// Owned here and used by the thread alone while it runs.
        std::unique_ptr<CellReader> mReader;

        /// Last, for the reason `Worker` gives.
        Worker mWorker;
    };
}
