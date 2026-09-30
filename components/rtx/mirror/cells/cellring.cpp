#include "cellring.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>

#include "cellgrid.hpp"
#include "prepared.hpp"

namespace Rtx
{
    namespace
    {
        /// The order the prepared disc's missing cells are read in: nearest first, then a fixed
        /// order among equals, so two runs from one eye ask for one list.
        struct Nearer
        {
            const CellGrid& mGrid;
            osg::Vec3f mEye;

            bool operator()(const osg::Vec2i& left, const osg::Vec2i& right) const
            {
                const float leftAway = mGrid.distanceSquaredTo(left, mEye);
                const float rightAway = mGrid.distanceSquaredTo(right, mEye);
                if (leftAway != rightAway)
                    return leftAway < rightAway;

                return left < right;
            }
        };
    }

    CellRing::CellRing(SceneAdopter& adopter)
        : mAdopter(adopter)
        , mPlacer(adopter.getScene())
    {
    }

    CellRing::~CellRing() = default;

    void CellRing::follow(const WorldAround& around)
    {
        // From either: a world told twice before a walk is the last word, and a detach tells the
        // ring of no world whether or not a walk came between.
        mTurn.step(Turn::Followed, Turn::Collected, Turn::Followed);
        mAround = around;
        mPlacer.setNightDay(around.mNightDay);

        if (mSupply.isReading(around.mWorld))
            return;

        // Everything held names the reader that is about to go, so it is let go of before the
        // supply is pointed anywhere else. Nothing is given back: what the frame held dies with the
        // reader that lent it.
        forget();
        mSupply.follow(around.mWorld);
        mAskStale = true;
    }

    void CellRing::forget()
    {
        // Nothing is given back — what the reader lent dies with it — but the rows are the frame's
        // own, and a worldspace change is the one time this runs while the game plays.
        mPlacer.dropUnless([](const HeldCell&) { return false; }, [](const HeldCell&) {});

        mHolds.forget();
        mHanded.clear();
    }

    void CellRing::setStaticsEnabled(const bool enabled)
    {
        mAskStale = mAskStale || enabled != mStatics;
        mStatics = enabled;
    }

    void CellRing::setFrame(const std::size_t frame)
    {
        mFrame = frame;
    }

    void CellRing::setSettled(const bool settled)
    {
        mSettled = settled;
    }

    bool CellRing::handed(const osg::Vec2i& cell) const
    {
        return std::any_of(
            mHanded.begin(), mHanded.end(), [&](const PreparedCell* held) { return held->mCell == cell; });
    }

    void CellRing::giveBackHolds(const HeldCell& cell)
    {
        CellReturns& back = mSupply.giveBack();
        back.mModels.insert(back.mModels.end(), cell.mModels.begin(), cell.mModels.end());
        back.mTextures.insert(back.mTextures.end(), cell.mGround.mTextures.begin(), cell.mGround.mTextures.end());
    }

    void CellRing::giveBackHolds(const PreparedCell& cell)
    {
        CellReturns& back = mSupply.giveBack();
        back.mModels.insert(back.mModels.end(), cell.mModels.begin(), cell.mModels.end());
        cell.mGround.collectTextures(back.mTextures);
    }

    void CellRing::takeDone()
    {
        mDoneScratch.clear();
        mSupply.take(mDoneScratch);

        for (PreparedCell* cell : mDoneScratch)
        {
            // Counted as it arrives, so the frame knows of every model a cell it may adopt names.
            for (PreparedModel* model : cell->mModels)
                ++mHolds.know(*model).mNamed;

            // The switch is a setting the game can move while it runs, and a cell read under the
            // other answer is read again. A cell the reader is part-way through is neither held nor
            // handed, so a replacing list names it again and the reader hands over two copies;
            // `sift` turns the second away.
            if (cell->mStatics != mStatics || handed(cell->mCell))
                discard(*cell);
            else
                mHanded.push_back(cell);
            mAskStale = true;
        }

        mDoneScratch.clear();
    }

    void CellRing::ask(const osg::Vec3f& eye, const float band)
    {
        // Rebuilt only when what it depends on moved: the eye, what is held, what is handed, or the
        // statics switch. Otherwise it is the list the supply already has.
        if (!mAskStale)
            return;
        mAskStale = false;

        mAsking.mCells.clear();
        mAsking.mStatics = mStatics;

        const CellGrid& grid = mAround.mWorld.mGrid;
        grid.forEachCellWithin(eye, band, [&](const osg::Vec2i& cell) {
            if (!mPlacer.holds(cell) && !handed(cell))
                mAsking.mCells.push_back(cell);
        });

        std::sort(mAsking.mCells.begin(), mAsking.mCells.end(), Nearer{ .mGrid = grid, .mEye = eye });

        mSupply.ask(mAsking);
    }

    void CellRing::sift(const osg::Vec3f& eye, const float band)
    {
        const std::size_t before = mHanded.size();
        std::erase_if(mHanded, [&](PreparedCell* cell) {
            if (mAround.mWorld.mGrid.withinReach(cell->mCell, eye, band) && !mPlacer.holds(cell->mCell))
                return false;

            discard(*cell);
            return true;
        });
        mAskStale = mAskStale || mHanded.size() != before;
    }

    void CellRing::waitForNext(const osg::Vec3f& eye, const float band)
    {
        // A cell read under the other answer to the statics switch, or one of a band that left, is
        // not what the wait waited for: the first thing the reader hands over after a move is
        // usually a cell nothing wants any more.
        while (mHanded.empty())
        {
            mSupply.waitForOne();
            takeDone();
            sift(eye, band);
        }
    }

    void CellRing::adoptHanded()
    {
        // One cell a frame, and one frame walked twice adopts once. A cell's meshes are copied
        // into the scene and its structures built by the hand-over that follows; two on one frame
        // would be the batch behind a threshold this renderer never takes. A settled walk keeps the
        // rule and waits for its one cell, which is what `setSettled` says.
        if (mHanded.empty() || mAdoptedFrame == mFrame)
            return;

        mAdoptedFrame = mFrame;
        adopt(*mHanded.front());
        mHanded.erase(mHanded.begin());
        mAskStale = true;
    }

    void CellRing::adopt(PreparedCell& cell)
    {
        const Crash::NoteScope noted("adopting the cell {}, {}", cell.mCell.x(), cell.mCell.y());

        ExtractionStats& stats = mAdopter.getStats();
        HeldCell& held = mPlacer.hold(cell, mAround, stats);

        mAdopter.getScene().refusals().refuse(cell.mRefusals);
        stats.mPreprocessed.mOffFrame += cell.mPreprocessed;

        for (PreparedModel* model : cell.mModels)
        {
            CellHolds::HeldModel& known = mHolds.knownOf(*model);
            if (known.mParts.empty())
                mHolds.adoptParts(known, mAdopter);

            held.mModels.push_back(model);
        }

        mPlacer.adoptPlacements(cell, held, mHolds);

        mSupply.giveBack().mCells.push_back(&cell);
    }

    void CellRing::discard(PreparedCell& cell)
    {
        for (PreparedModel* model : cell.mModels)
            mHolds.release(*model);

        // Every hold the reader counted for the cell goes back with it: the models, and the
        // images its ground names.
        giveBackHolds(cell);
        mSupply.giveBack().mCells.push_back(&cell);
    }

    void CellRing::letGo(const HeldCell& cell)
    {
        for (PreparedModel* model : cell.mModels)
            mHolds.release(*model);

        giveBackHolds(cell);
    }

    void CellRing::setReferenceEnabled(const ESM::RefNum refnum, const bool enabled)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.setReferenceEnabled(refnum, enabled);
    }

    void CellRing::blacklistReference(const ESM::RefNum refnum)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.blacklistReference(refnum);
    }

    void CellRing::forgetReferences()
    {
        mTurn.expect(Turn::Collected);
        mPlacer.forgetReferences();
    }

    void CellRing::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.setGate(gate, state);
    }

    void CellRing::collectStanding(std::vector<ESM::RefNum>& into) const
    {
        mPlacer.collectStanding(into);
    }

    void CellRing::collectGateVerdicts(std::vector<GateVerdict>& into) const
    {
        mPlacer.collectGateVerdicts(mAround.mActiveGrid, into);
    }

    void CellRing::collect()
    {
        mTurn.step(Turn::Collected, Turn::Followed);
        ExtractionStats& stats = mAdopter.getStats();

        // What `forget` let go of since the last walk, and then what this walk lets go of.
        mHolds.releaseParts(mAdopter);
        walkRings(stats);
        mHolds.releaseParts(mAdopter);

        // Asserted after every walk, so a placement that outlived its cell, or a cell whose
        // placement went missing, is found on the frame it happened rather than seen at the horizon.
        assert(mPlacer.standsAsHeld(mAround)
            && "the ring stands something its cells do not hold, or holds what it does not stand");

        // What stands, counted off the slots and not off a tally: a world with no reader and an
        // interior have both dropped every slot by now, and stand nothing.
        stats.mInstances += mPlacer.getPlaced() + mPlacer.getGroundPlaced();
        stats.mDistantStatics += mPlacer.getPlaced();
        stats.mGroundCells += mPlacer.getGroundPlaced();
    }

    void CellRing::walkRings(ExtractionStats& stats)
    {
        if (!mSupply.hasReader())
            return;

        takeDone();

        // Indoors the eye's coordinates belong to another space, so the rings are not moved:
        // what is held stays held for the way back out, and nothing stands.
        if (!mAround.mExterior)
        {
            mPlacer.dropSlots();
            mSupply.publish();
            return;
        }

        const osg::Vec3f& eye = mAround.mEye;
        // One cell past the reach, which at the island route's speed is most of a second, so the
        // frame a cell crosses into the reach owes only its placements.
        const float band = mAround.mReach + mAround.mWorld.mGrid.getCellSize();

        // Any move, because the disc is measured from the eye itself and a cell at its rim can
        // enter or leave on a step. What that costs is a walk over the band's cells on the frames
        // the eye moves, and nothing on the frames it stands.
        if (mLastEye != eye)
        {
            mLastEye = eye;
            mAskStale = true;
        }

        // A cell held with the statics the other way is dropped whole and read again, for the
        // reason `takeDone` gives.
        const std::size_t dropped = mPlacer.dropUnless(
            [&](const HeldCell& cell) {
                return mAround.mWorld.mGrid.withinReach(cell.mCell, eye, band) && cell.mStatics == mStatics;
            },
            [&](const HeldCell& cell) { letGo(cell); });
        if (dropped > 0)
            mAskStale = true;

        sift(eye, band);

        ask(eye, band);

        // Waited for after the ask that names it and never before, because what the reader is
        // about to hand back is what that ask asked for. Nothing was asked for where the band is
        // whole, and then there is nothing to wait for.
        if (mSettled && mHanded.empty() && !mAsking.mCells.empty())
            waitForNext(eye, band);

        adoptHanded();

        stats.mLights += mPlacer.place(mAround);

        mSupply.publish();
    }
}
