#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include "cellgrid.hpp"

namespace Rtx
{
    struct CellReturns;
    struct PreparedCell;
    struct PreparedGrass;

    /// What of a cell the reader reads: its ground, lamps and references, or its groundcover.
    enum class ReadKind : std::uint8_t
    {
        Cell,
        Grass,
    };

    /// No band wants the read, and nothing of it is held.
    struct Unwanted
    {
    };

    /// A band wants it, and the reader has not begun it.
    struct Wanted
    {
    };

    /// In the reader's hands, and whether a band still wants it as the read ends: one no band keeps is given back by
    /// the reader and never handed.
    struct Reading
    {
        bool mWanted = true;
    };

    /// Read, and waiting for the frame to adopt it. Known once the frame has counted the models it names
    /// (`CellTable::collectFiled`), so a model a cell let go of and a read cell names again keeps its holds.
    template <class Prepared>
    struct Read
    {
        Prepared* mPrepared = nullptr;
        bool mKnown = false;
    };

    /// The frame holds it (`CellPlacer`), and lets go of it there.
    struct Adopted
    {
    };

    template <class Prepared>
    using CellStage = std::variant<Unwanted, Wanted, Reading, Read<Prepared>, Adopted>;

    /// One cell's two reads, each a stage the reader and the frame move it through in turn.
    struct CellRecord
    {
        osg::Vec2i mCell;
        CellStage<PreparedCell> mCellStage;
        CellStage<PreparedGrass> mGrassStage;
    };

    /// A read the reader took.
    struct CellPick
    {
        osg::Vec2i mCell;
        ReadKind mKind = ReadKind::Cell;
    };

    /// What the bands want: a cell within `mBand` of the eye is read, and one past `mKept` let go, a cell further out
    /// so that an eye walking to and fro across the band's edge reads nothing again; the groundcover's likewise, nought
    /// where the world has none.
    struct CellTarget
    {
        CellGrid mGrid;
        osg::Vec3f mEye;
        float mBand = 0.0f;
        float mKept = 0.0f;
        float mGrassBand = 0.0f;
        float mGrassKept = 0.0f;

        bool operator==(const CellTarget& other) const = default;
    };

    /// What a retarget let go of that the frame knew: each still read and never adopted, the frame's holds on its
    /// models to give back with it.
    struct CellsDropped
    {
        std::vector<PreparedCell*> mCells;
        std::vector<PreparedGrass*> mGrass;

        void clear()
        {
            mCells.clear();
            mGrass.clear();
        }
    };

    /// **Every cell the bands want or something of it is held, one record a cell**, nearest the eye first: what the
    /// reader takes next, what is on its way, and what the frame adopts next are each a record's stage, where a
    /// request list, the lists handed over, the cells on their way and the counts of takes between them were seven
    /// containers kept in step by hand. No lock of its own: `CellSupply` holds it under the monitor between the frame
    /// and the reader. Bounded by the band's cells and the ring kept past it.
    class CellTable
    {
    public:
        /// **The most reads waiting for the frame**, cells and groundcover together: the frame adopts one of each a
        /// frame, and a reader let run read every cell of a band ahead of it, 429 at the Repack's busiest crossing. A
        /// read nearer than every one of its kind waiting is taken past the bound, so a settled walk's wait for the
        /// nearest cell it lacks is never a wait for room.
        static constexpr std::size_t sReadAheadMost = 8;

        /// Forgets every record, for a reader that is gone with everything it lent.
        void clear() { mRecords.clear(); }

        /// Moves the bands to `target`: a read past what its band keeps is let go, a cell of the band not yet held is
        /// wanted, and the records are ordered nearest the new eye first. A read never adopted is let go into `back`
        /// where the frame never counted its models, and into `dropped` where it did.
        void retarget(const CellTarget& target, CellReturns& back, CellsDropped& dropped);

        /// Withdraws every read not begun, for an eye with no band: a room.
        void withdraw();

        /// Takes the nearest read the reader may begin, groundcover first, and marks it in hand. Nothing where every
        /// wanted read waits behind `sReadAheadMost`.
        std::optional<CellPick> pick();

        /// Whether `pick` would take a read.
        bool canPick() const;

        /// Files what the reader read for `pick`, and answers whether a band still wants it; where none does, its
        /// record no longer holds it and the reader gives it back.
        bool file(const CellPick& pick, PreparedCell& read);
        bool file(const CellPick& pick, PreparedGrass& read);

        /// Marks every read filed since the last call known and appends it, for the frame to count its models.
        void collectFiled(std::vector<PreparedCell*>& cells, std::vector<PreparedGrass*>& grass);

        /// The nearest known read of a cell, or of a cell's groundcover, marked stood. Null where none is.
        PreparedCell* adoptCell();
        PreparedGrass* adoptGrass();

        /// Where the nearest read of `kind` the band lacks stands, which a settled walk waits for: wanted, in hand or
        /// read, within its band. Nothing where the band holds all of its cells.
        std::optional<std::size_t> findOwed(ReadKind kind) const;

        /// Whether the read of `kind` at `at` is read and waiting.
        bool isRead(std::size_t at, ReadKind kind) const;

        /// How many reads of `kind` stand, for the frame's check that its cells are the table's.
        std::size_t countStood(ReadKind kind) const;

    private:
        std::vector<CellRecord> mRecords;
        CellTarget mTarget;

        /// The cells of the band no record holds, refilled per retarget.
        std::vector<CellRecord> mFresh;
    };
}
