#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <components/misc/monitor.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/common/ownedby.hpp>

#include "celltable.hpp"
#include "cellworld.hpp"
#include "readermemory.hpp"

namespace Rtx
{
    class CellReader;
    struct PreparedCell;
    struct PreparedGrass;
    struct PreparedModel;
    struct PreparedTexture;

    /// What the frame has finished with, on its way back to the reader: a cell, the models it
    /// named and the images its ground named leave together.
    struct CellReturns
    {
        std::vector<PreparedCell*> mCells;
        std::vector<PreparedGrass*> mGrass;
        std::vector<PreparedModel*> mModels;
        std::vector<PreparedTexture*> mTextures;

        bool empty() const { return mCells.empty() && mGrass.empty() && mModels.empty() && mTextures.empty(); }

        /// Empties all four, keeping the room each grew.
        void clear();

        /// Appends everything `from` holds and empties it.
        void take(CellReturns& from);
    };

    /// Cells read ahead of the eye, on a thread of its own: the thread, the reader it drives, and between them the
    /// table of what is wanted, in hand and read (`CellTable`) and what the frame has finished with. Everything read is
    /// lent and given back; `Spares` says why an address and not a shared count.
    class CellSupply
    {
    public:
        CellSupply();

        /// Stops the thread, which stops between a cell's references.
        ~CellSupply();

        CellSupply(const CellSupply&) = delete;
        CellSupply& operator=(const CellSupply&) = delete;

        /// Whether this is already reading `world`, so a caller need not let go of what it holds.
        bool isReading(const CellWorld& world) const { return mWorld == world; }

        /// The world it reads.
        const CellWorld& getWorld() const { return mWorld; }

        /// Whether there is a reader at all, which a world with no content answers no to.
        bool hasReader() const { return mReader != nullptr; }

        /// Points it at `world`, stopping and joining whatever it was reading and forgetting the table. The caller lets
        /// go of what it held first, or a hold handed back would reach a reader that never lent it.
        void follow(const CellWorld& world);

        /// `CellTable::retarget`, waking the reader: what was read and never known goes to `giveBack`.
        void retarget(const CellTarget& target, CellsDropped& dropped);

        /// `CellTable::withdraw`.
        void withdraw();

        /// For a settled walk: blocks until the read of `kind` the band lacks nearest the eye is read
        /// (`CellTable::findOwed`), or the reader has nothing it can read. Nothing to wait for where none is owed.
        void awaitOwed(ReadKind kind);

        /// `CellTable::collectFiled`.
        void collectFiled(std::vector<PreparedCell*>& cells, std::vector<PreparedGrass*>& grass);

        /// `CellTable::adoptCell` and `adoptGrass`, waking the reader where the bound held it.
        PreparedCell* adoptCell();
        PreparedGrass* adoptGrass();

        /// `CellTable::countStood`.
        std::size_t countStood(ReadKind kind);

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
        /// The reader's loop: a read at a time, until asked to stop.
        void work(const std::stop_token& stop);

        /// Reads what `pick` names and files it, or gives it back where no band wants it as the read ends. On the
        /// thread, outside the lock but for the filing.
        void read(const CellPick& pick, const std::stop_token& stop);

        /// Gives the reader what the frame gave back, taken out of `mReturned` under the lock into
        /// `mRecycling`. On the thread, outside the lock: putting a cell's memory away is the
        /// reader's work, and the frame takes the same lock for its own.
        void recycle();

        CellWorld mWorld;

        /// Which thread `mReturning` belongs to. The frame's, and `mOnFrame.check()` is what says so at each of the
        /// calls that touch it.
        OwnedBy mOnFrame;

        /// What the frame is collecting to hand back.
        CellReturns mReturning;

        /// The lock between the frame and the reader, and the two waits across it. It guards the
        /// three below and nothing else.
        Misc::Monitor mMonitor;

        CellTable mTable;

        /// What the frame has given back, for the thread to refill.
        CellReturns mReturned;

        /// `CellReader::measure` as the thread last finished a cell.
        ReaderMemory mMeasured;

        /// The thread's own: the read it took, and what the frame gave back that it is putting away.
        std::optional<CellPick> mPicked;
        CellReturns mRecycling;

        /// Owned here and used by the thread alone while it runs.
        std::unique_ptr<CellReader> mReader;

        /// Last, so the thread is joined before the members it reads go.
        std::jthread mWorker;
    };
}
