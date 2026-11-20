#include "celltable.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include <components/crashcatcher/crash.hpp>

#include "cellsupply.hpp"
#include "prepared.hpp"

namespace Rtx
{
    namespace
    {
        /// The order a band's cells are read and adopted in: nearest first, then a fixed order among equals, so two
        /// runs from one eye take one order.
        struct Nearer
        {
            const CellGrid& mGrid;
            osg::Vec3f mEye;

            bool operator()(const CellRecord& left, const CellRecord& right) const
            {
                const float leftAway = mGrid.distanceSquaredTo(left.mCell, mEye);
                const float rightAway = mGrid.distanceSquaredTo(right.mCell, mEye);
                if (leftAway != rightAway)
                    return leftAway < rightAway;

                return left.mCell < right.mCell;
            }
        };

        template <class Prepared>
        bool isPending(const CellStage<Prepared>& stage)
        {
            return std::holds_alternative<Wanted>(stage) || std::holds_alternative<Reading>(stage)
                || std::holds_alternative<Read<Prepared>>(stage);
        }

        /// A read the frame never counted the models of goes back with every hold the reader lent it.
        void giveBackUnknown(PreparedCell& cell, CellReturns& back)
        {
            back.mModels.insert(back.mModels.end(), cell.mModels.begin(), cell.mModels.end());
            cell.mGround.collectTextures(back.mTextures);
            back.mCells.push_back(&cell);
        }

        void giveBackUnknown(PreparedGrass& grass, CellReturns& back)
        {
            back.mModels.insert(back.mModels.end(), grass.mModels.begin(), grass.mModels.end());
            back.mGrass.push_back(&grass);
        }

        /// The stage after the bands moved: a read not begun outside its band is withdrawn, one in hand is kept while
        /// it is within what the band keeps, and what was read or stood past that is let go.
        template <class Prepared>
        void relax(CellStage<Prepared>& stage, const bool inBand, const bool inKept, CellReturns& back,
            std::vector<Prepared*>& dropped)
        {
            if (std::holds_alternative<Wanted>(stage))
            {
                if (!inBand)
                    stage = Unwanted{};
                return;
            }
            if (auto* const reading = std::get_if<Reading>(&stage))
            {
                reading->mWanted = inKept;
                return;
            }
            if (auto* const read = std::get_if<Read<Prepared>>(&stage))
            {
                if (inKept)
                    return;
                if (read->mKnown)
                    dropped.push_back(read->mPrepared);
                else
                    giveBackUnknown(*read->mPrepared, back);
                stage = Unwanted{};
                return;
            }
            if (std::holds_alternative<Adopted>(stage) && !inKept)
                stage = Unwanted{};
        }

        template <class Prepared>
        void want(CellStage<Prepared>& stage)
        {
            if (std::holds_alternative<Unwanted>(stage))
                stage = Wanted{};
        }

        /// The nearest wanted read of the kind `stageOf` names that the reader may begin: before every read of its
        /// kind still waiting, or anywhere while fewer than the bound wait.
        template <class Prepared>
        std::optional<std::size_t> findAdmissible(const std::vector<CellRecord>& records,
            CellStage<Prepared> CellRecord::* stageOf, const std::size_t waiting)
        {
            for (std::size_t at = 0; at < records.size(); ++at)
            {
                const CellStage<Prepared>& stage = records[at].*stageOf;
                if (std::holds_alternative<Read<Prepared>>(stage))
                {
                    if (waiting >= CellTable::sReadAheadMost)
                        return std::nullopt;
                    continue;
                }
                if (std::holds_alternative<Wanted>(stage))
                    return at;
            }
            return std::nullopt;
        }

        std::size_t countWaiting(const std::vector<CellRecord>& records)
        {
            return static_cast<std::size_t>(std::ranges::count_if(records, [](const CellRecord& record) {
                return std::holds_alternative<Read<PreparedCell>>(record.mCellStage);
            }) + std::ranges::count_if(records, [](const CellRecord& record) {
                return std::holds_alternative<Read<PreparedGrass>>(record.mGrassStage);
            }));
        }

        template <class Prepared>
        bool fileAs(std::vector<CellRecord>& records, CellStage<Prepared> CellRecord::* stageOf, const osg::Vec2i& cell,
            Prepared& read)
        {
            // A record in hand is never erased, since only one no read holds is.
            const auto record
                = std::ranges::find_if(records, [&](const CellRecord& each) { return each.mCell == cell; });
            Crash::contract(record != records.end(), "a read filed whose record went");
            CellStage<Prepared>& stage = (*record).*stageOf;
            const auto* const reading = std::get_if<Reading>(&stage);
            Crash::contract(reading != nullptr, "a read filed that was not in hand");

            if (!reading->mWanted)
            {
                stage = Unwanted{};
                return false;
            }
            stage = Read<Prepared>{ .mPrepared = &read, .mKnown = false };
            return true;
        }

        template <class Prepared>
        Prepared* adoptFirst(std::vector<CellRecord>& records, CellStage<Prepared> CellRecord::* stageOf)
        {
            for (CellRecord& record : records)
            {
                const auto* const read = std::get_if<Read<Prepared>>(&(record.*stageOf));
                if (read == nullptr || !read->mKnown)
                    continue;
                Prepared* const prepared = read->mPrepared;
                record.*stageOf = Adopted{};
                return prepared;
            }
            return nullptr;
        }

        template <class Prepared>
        void collectAs(
            std::vector<CellRecord>& records, CellStage<Prepared> CellRecord::* stageOf, std::vector<Prepared*>& into)
        {
            for (CellRecord& record : records)
                if (auto* const read = std::get_if<Read<Prepared>>(&(record.*stageOf));
                    read != nullptr && !read->mKnown)
                {
                    read->mKnown = true;
                    into.push_back(read->mPrepared);
                }
        }
    }

    void CellTable::retarget(const CellTarget& target, CellReturns& back, CellsDropped& dropped)
    {
        mTarget = target;
        const CellGrid& grid = target.mGrid;
        const auto within = [&](const osg::Vec2i& cell, const float radius) {
            return radius > 0.0f && grid.withinReach(cell, target.mEye, radius);
        };

        for (CellRecord& record : mRecords)
        {
            relax(record.mCellStage, within(record.mCell, target.mBand), within(record.mCell, target.mKept), back,
                dropped.mCells);
            relax(record.mGrassStage, within(record.mCell, target.mGrassBand), within(record.mCell, target.mGrassKept),
                back, dropped.mGrass);
        }
        std::erase_if(mRecords, [](const CellRecord& record) {
            return std::holds_alternative<Unwanted>(record.mCellStage)
                && std::holds_alternative<Unwanted>(record.mGrassStage);
        });

        // By the cell, so each of the band's cells finds its record in a search; then nearest first again.
        const auto byCell = [](const CellRecord& left, const CellRecord& right) { return left.mCell < right.mCell; };
        std::ranges::sort(mRecords, byCell);
        mFresh.clear();
        const float widest = std::max(target.mBand, target.mGrassBand);
        if (widest > 0.0f)
            grid.forEachCellWithin(target.mEye, widest, [&](const osg::Vec2i& cell) {
                const bool inBand = within(cell, target.mBand);
                const bool inGrass = within(cell, target.mGrassBand);
                const auto record = std::lower_bound(mRecords.begin(), mRecords.end(), cell,
                    [](const CellRecord& held, const osg::Vec2i& asked) { return held.mCell < asked; });
                if (record != mRecords.end() && record->mCell == cell)
                {
                    if (inBand)
                        want(record->mCellStage);
                    if (inGrass)
                        want(record->mGrassStage);
                    return;
                }
                mFresh.push_back(CellRecord{
                    .mCell = cell,
                    .mCellStage = inBand ? CellStage<PreparedCell>(Wanted{}) : CellStage<PreparedCell>(Unwanted{}),
                    .mGrassStage = inGrass ? CellStage<PreparedGrass>(Wanted{}) : CellStage<PreparedGrass>(Unwanted{}),
                });
            });
        mRecords.insert(mRecords.end(), mFresh.begin(), mFresh.end());
        std::ranges::sort(mRecords, Nearer{ .mGrid = grid, .mEye = target.mEye });
    }

    void CellTable::withdraw()
    {
        for (CellRecord& record : mRecords)
        {
            if (std::holds_alternative<Wanted>(record.mCellStage))
                record.mCellStage = Unwanted{};
            if (std::holds_alternative<Wanted>(record.mGrassStage))
                record.mGrassStage = Unwanted{};
        }
    }

    std::optional<CellPick> CellTable::pick()
    {
        // The groundcover first: the cells around the eye, where a plant missing is seen at once.
        const std::size_t waiting = countWaiting(mRecords);
        if (const std::optional<std::size_t> grass = findAdmissible(mRecords, &CellRecord::mGrassStage, waiting))
        {
            mRecords[*grass].mGrassStage = Reading{};
            return CellPick{ .mCell = mRecords[*grass].mCell, .mKind = ReadKind::Grass };
        }
        if (const std::optional<std::size_t> cell = findAdmissible(mRecords, &CellRecord::mCellStage, waiting))
        {
            mRecords[*cell].mCellStage = Reading{};
            return CellPick{ .mCell = mRecords[*cell].mCell, .mKind = ReadKind::Cell };
        }
        return std::nullopt;
    }

    bool CellTable::canPick() const
    {
        const std::size_t waiting = countWaiting(mRecords);
        return findAdmissible(mRecords, &CellRecord::mGrassStage, waiting).has_value()
            || findAdmissible(mRecords, &CellRecord::mCellStage, waiting).has_value();
    }

    bool CellTable::file(const CellPick& pick, PreparedCell& read)
    {
        assert(pick.mKind == ReadKind::Cell && "a cell's read filed as its groundcover's");
        return fileAs(mRecords, &CellRecord::mCellStage, pick.mCell, read);
    }

    bool CellTable::file(const CellPick& pick, PreparedGrass& read)
    {
        assert(pick.mKind == ReadKind::Grass && "a cell's groundcover filed as its read");
        return fileAs(mRecords, &CellRecord::mGrassStage, pick.mCell, read);
    }

    void CellTable::collectFiled(std::vector<PreparedCell*>& cells, std::vector<PreparedGrass*>& grass)
    {
        collectAs(mRecords, &CellRecord::mCellStage, cells);
        collectAs(mRecords, &CellRecord::mGrassStage, grass);
    }

    PreparedCell* CellTable::adoptCell()
    {
        return adoptFirst(mRecords, &CellRecord::mCellStage);
    }

    PreparedGrass* CellTable::adoptGrass()
    {
        return adoptFirst(mRecords, &CellRecord::mGrassStage);
    }

    std::optional<std::size_t> CellTable::findOwed(const ReadKind kind) const
    {
        const float band = kind == ReadKind::Cell ? mTarget.mBand : mTarget.mGrassBand;
        if (!(band > 0.0f))
            return std::nullopt;

        for (std::size_t at = 0; at < mRecords.size(); ++at)
        {
            const CellRecord& record = mRecords[at];
            const bool pending = kind == ReadKind::Cell ? isPending(record.mCellStage) : isPending(record.mGrassStage);
            if (pending && mTarget.mGrid.withinReach(record.mCell, mTarget.mEye, band))
                return at;
        }
        return std::nullopt;
    }

    bool CellTable::isRead(const std::size_t at, const ReadKind kind) const
    {
        const CellRecord& record = mRecords[at];
        return kind == ReadKind::Cell ? std::holds_alternative<Read<PreparedCell>>(record.mCellStage)
                                      : std::holds_alternative<Read<PreparedGrass>>(record.mGrassStage);
    }

    std::size_t CellTable::countStood(const ReadKind kind) const
    {
        return static_cast<std::size_t>(std::ranges::count_if(mRecords, [&](const CellRecord& record) {
            return kind == ReadKind::Cell ? std::holds_alternative<Adopted>(record.mCellStage)
                                          : std::holds_alternative<Adopted>(record.mGrassStage);
        }));
    }
}
