#include "cellring.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "cellgrid.hpp"
#include "prepared.hpp"

namespace Rtx
{
    // **Every cell the ring stands is inside the trace's reach**: the most a reach takes, and the
    // one cell past it the ring stands as the band, at the widest cells any worldspace has.
    static_assert(sFarPlane > (LandReach::sMostCells + 1.0f) * Constants::CellSizeInUnits,
        "the ring stands cells past where every ray ends");

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
        , mPlacer(adopter.getScene(), adopter.getSpecularLayout())
    {
    }

    CellRing::~CellRing()
    {
        // What the frame holds goes back to the adopter, which outlives the ring, whether or not a
        // detach came first. Not while an exception unwinds, where the scene is whatever the throw
        // left, and each hold's own assert stands aside for it.
        if (std::uncaught_exceptions() == 0)
        {
            forget();
            releaseHolds();
        }
    }

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
        ++mFollowed;
    }

    void CellRing::forget()
    {
        // Nothing is given back — what the reader lent dies with it — but the rows are the frame's
        // own, and a worldspace change is the one time this runs while the game plays.
        mPlacer.dropUnless([](const HeldCell&) { return false; }, [](const HeldCell&) {});
        mPlacer.dropGrassUnless([](const HeldGrass&) { return false; }, [](const HeldGrass&) {});

        mHolds.forget();
        mHanded.clear();
        mHandedGrass.clear();
    }

    void CellRing::setStaticsEnabled(const bool enabled)
    {
        mStatics = enabled;
    }

    void CellRing::setSettled(const bool settled)
    {
        mSettled = settled;
    }

    std::uint32_t CellRing::getCellsToStand() const
    {
        // **Counted against the band and not against the ask**, which names what was missing when
        // it was made: a walk that waited for its cell adopted one of them since. A walk that stood
        // nothing has nothing to stand, though it keeps what it held for the way back out.
        if (mBandCells == 0)
            return 0;

        assert(getHeldCellCount() <= mBandCells && "a cell held outside the band the last walk asked over");
        assert(getHeldGrassCount() <= mGrassBandCells && "grass held outside the band the last walk asked over");
        return mBandCells - static_cast<std::uint32_t>(getHeldCellCount()) + mGrassBandCells
            - static_cast<std::uint32_t>(getHeldGrassCount());
    }

    bool CellRing::waitsUnder(const osg::Vec2f& low, const osg::Vec2f& high) const
    {
        if (mBandCells == 0 || !mAsked.has_value())
            return false;

        // The cells the square overlaps by some area, so one that only touches its edge is out: a
        // map tile's box is its cell's square exactly.
        const CellGrid& grid = mAround.mWorld.mGrid;
        const float side = grid.getCellSize();
        const auto first = [&](float at) { return static_cast<int>(std::floor(at / side)); };
        const auto last = [&](float at) { return static_cast<int>(std::ceil(at / side)) - 1; };
        for (int x = first(low.x()); x <= last(high.x()); ++x)
            for (int y = first(low.y()); y <= last(high.y()); ++y)
            {
                const osg::Vec2i cell(x, y);
                if (grid.withinReach(cell, mAsked->mEye, mAsked->mBand) && !mPlacer.holds(cell))
                    return true;
            }

        return false;
    }

    void CellRing::know(const std::span<PreparedModel* const> models)
    {
        for (PreparedModel* model : models)
            ++mHolds.know(*model).mNamed;
    }

    template <class Prepared>
    void CellRing::adoptModels(Prepared& prepared)
    {
        ExtractionStats& stats = mAdopter.getStats();
        mAdopter.getScene().refusals().refuse(prepared.mRefusals);
        stats.mPreprocessed.mOffFrame += prepared.mPreprocessed;

        for (PreparedModel* model : prepared.mModels)
        {
            CellHolds::HeldModel& known = mHolds.knownOf(*model);
            if (known.mParts.empty())
                mHolds.adoptParts(known, mAdopter);
        }
    }

    float CellRing::grassBand() const
    {
        if (mAround.mWorld.mGroundcover == nullptr || !(mAround.mGroundcoverReach > 0.0f))
            return 0.0f;

        return mAround.mGroundcoverReach + mAround.mWorld.mGrid.getCellSize();
    }

    void CellRing::takeDone()
    {
        mDoneScratch.clear();
        mDoneGrassScratch.clear();
        mSupply.take(mDoneScratch, mDoneGrassScratch);

        for (PreparedCell* cell : mDoneScratch)
        {
            // Counted as it arrives, so the frame knows of every model a cell it may adopt names.
            know(cell->mModels);

            // The switch is a setting the game can move while it runs, and a cell read under the
            // other answer is read again. **Never a second copy under the same answer**: the supply
            // reads a cell once while it is on its way (`CellSupply`), and a copy adopted twice
            // would stand every reference twice.
            Crash::contract(
                cell->mStatics != mStatics || (!handed(mHanded, cell->mCell) && !mPlacer.holds(cell->mCell)),
                "the supply handed over a cell the ring already had");
            if (cell->mStatics != mStatics)
                discard(*cell);
            else
                mHanded.push_back(cell);
            ++mTaken;
        }

        for (PreparedGrass* grass : mDoneGrassScratch)
        {
            know(grass->mModels);

            Crash::contract(!handed(mHandedGrass, grass->mCell) && !mPlacer.holdsGrass(grass->mCell),
                "the supply handed over a cell's grass the ring already had");
            mHandedGrass.push_back(grass);
            ++mTaken;
        }

        mDoneScratch.clear();
        mDoneGrassScratch.clear();
    }

    void CellRing::ask(const osg::Vec3f& eye, const float band, const float grassBand)
    {
        // Rebuilt only when what it is made from moved. Otherwise it is the list the supply already
        // has. Any move of the eye, because the disc is measured from the eye itself and a cell at
        // its rim can enter or leave on a step: what that costs is a walk over the band's cells on
        // the frames the eye moves, and nothing on the frames it stands.
        const AskInputs inputs{
            .mEye = eye,
            .mBand = band,
            .mGrassBand = grassBand,
            .mStatics = mStatics,
            .mHeld = getHeldCellCount(),
            .mHanded = mHanded.size(),
            .mGrassHeld = getHeldGrassCount(),
            .mGrassHanded = mHandedGrass.size(),
            .mTaken = mTaken,
            .mFollowed = mFollowed,
        };
        if (mAsked == inputs)
            return;
        mAsked = inputs;

        mAsking.clear();
        mAsking.mStatics = mStatics;

        const CellGrid& grid = mAround.mWorld.mGrid;
        mBandCells = 0;
        grid.forEachCellWithin(eye, band, [&](const osg::Vec2i& cell) {
            ++mBandCells;
            if (!mPlacer.holds(cell) && !handed(mHanded, cell))
                mAsking.mCells.push_back(cell);
        });

        std::sort(mAsking.mCells.begin(), mAsking.mCells.end(), Nearer{ .mGrid = grid, .mEye = eye });

        mGrassBandCells = 0;
        if (grassBand > 0.0f)
            grid.forEachCellWithin(eye, grassBand, [&](const osg::Vec2i& cell) {
                ++mGrassBandCells;
                if (!mPlacer.holdsGrass(cell) && !handed(mHandedGrass, cell))
                    mAsking.mGrass.push_back(cell);
            });

        std::sort(mAsking.mGrass.begin(), mAsking.mGrass.end(), Nearer{ .mGrid = grid, .mEye = eye });

        mSupply.ask(mAsking);
    }

    void CellRing::sift(const osg::Vec3f& eye, const float band, const float grassBand)
    {
        std::erase_if(mHanded, [&](PreparedCell* cell) {
            if (mAround.mWorld.mGrid.withinReach(cell->mCell, eye, band))
                return false;

            discard(*cell);
            return true;
        });

        std::erase_if(mHandedGrass, [&](PreparedGrass* grass) {
            if (grassBand > 0.0f && mAround.mWorld.mGrid.withinReach(grass->mCell, eye, grassBand))
                return false;

            discard(*grass);
            return true;
        });
    }

    void CellRing::waitForNext(const osg::Vec3f& eye, const float band, const float grassBand)
    {
        // A cell read under the other answer to the statics switch, or one of a band that left, is
        // not what the wait waited for: the first thing the reader hands over after a move is
        // usually a cell nothing wants any more. The grass the same: a settled walk adopts its one
        // cell's grass on the frame two runs agree on, or their pictures part.
        const bool cells = !mAsking.mCells.empty();
        const bool grass = !mAsking.mGrass.empty();
        while ((cells && mHanded.empty()) || (grass && mHandedGrass.empty()))
        {
            const bool read = mSupply.waitForOne();
            takeDone();
            sift(eye, band, grassBand);

            // The reader has nothing left to read: what it read was sifted away, or the ask that
            // would have brought more equalled the one before. The walk adopts nothing this time.
            if (!read)
                return;
        }
    }

    void CellRing::adoptHanded(const std::size_t frame)
    {
        // One cell a frame, and one frame walked twice adopts once. A cell's meshes are copied
        // into the scene and its structures built by the hand-over that follows; two on one frame
        // would be the batch behind a threshold this renderer never takes. A settled walk keeps the
        // rule and waits for its one cell, which is what `setSettled` says.
        if (!mHanded.empty() && mAdoptedFrame != frame)
        {
            mAdoptedFrame = frame;
            adopt(*mHanded.front());
            mHanded.erase(mHanded.begin());
        }

        if (!mHandedGrass.empty() && mGrassAdoptedFrame != frame)
        {
            mGrassAdoptedFrame = frame;
            adopt(*mHandedGrass.front());
            mHandedGrass.erase(mHandedGrass.begin());
        }
    }

    void CellRing::adopt(PreparedCell& cell)
    {
        const Crash::NoteScope noted("adopting the cell {}, {}", cell.mCell.x(), cell.mCell.y());
        adoptModels(cell);

        // The ground after the models, as the groundcover's: where the texture table has no room
        // left, the ground's layers take the neutral texel and the models keep theirs, as the
        // ground gives way where the device's room runs out (`TextureArray::write`).
        mPlacer.holdCell(cell, mAround, mAdopter.getStats(), mHolds);

        mSupply.giveBack().mCells.push_back(&cell);
    }

    void CellRing::adopt(PreparedGrass& grass)
    {
        const Crash::NoteScope noted("adopting the groundcover of the cell {}, {}", grass.mCell.x(), grass.mCell.y());
        adoptModels(grass);

        mPlacer.holdGrass(grass, mHolds);

        mSupply.giveBack().mGrass.push_back(&grass);
    }

    void CellRing::discard(PreparedGrass& grass)
    {
        letGo(grass.mModels);
        mSupply.giveBack().mGrass.push_back(&grass);
    }

    void CellRing::letGo(const std::span<PreparedModel* const> models)
    {
        for (PreparedModel* model : models)
            mHolds.release(*model);

        CellReturns& back = mSupply.giveBack();
        back.mModels.insert(back.mModels.end(), models.begin(), models.end());
    }

    void CellRing::discard(PreparedCell& cell)
    {
        // Every hold the reader counted for the cell goes back with it: the models, and the
        // images its ground names.
        letGo(cell.mModels);
        CellReturns& back = mSupply.giveBack();
        cell.mGround.collectTextures(back.mTextures);
        back.mCells.push_back(&cell);
    }

    void CellRing::letGo(const HeldCell& cell)
    {
        letGo(cell.mModels);
        CellReturns& back = mSupply.giveBack();
        back.mTextures.insert(back.mTextures.end(), cell.mGround.mTextures.begin(), cell.mGround.mTextures.end());
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

    void CellRing::collect(const std::size_t frame)
    {
        mTurn.step(Turn::Collected, Turn::Followed);
        ExtractionStats& stats = mAdopter.getStats();

        // What `forget` let go of since the last walk, and then what this walk lets go of.
        mHolds.releaseParts(mAdopter);
        walkRings(stats, frame);
        mHolds.releaseParts(mAdopter);

        // Asserted after every walk, so a placement that outlived its cell, or a cell whose
        // placement went missing, is found on the frame it happened rather than seen at the horizon.
        assert(mPlacer.standsAsHeld(mAround)
            && "the ring stands something its cells do not hold, or holds what it does not stand");

        // What stands, counted off the slots and not off a tally: a world with no reader and an
        // interior have both dropped every slot by now, and stand nothing.
        stats.mInstances += mPlacer.getPlaced() + mPlacer.getGroundPlaced() + mPlacer.getGrassPlaced();
        stats.mDistantStatics += mPlacer.getPlaced();
        stats.mGroundCells += mPlacer.getGroundPlaced();
        stats.mGroundcover += mPlacer.getGrassPlaced();
    }

    void CellRing::walkRings(ExtractionStats& stats, const std::size_t frame)
    {
        // Nothing stands, so nothing is left to stand: an exterior band short of cells must not
        // outlive the walk that asked over it.
        if (!mSupply.hasReader())
        {
            mBandCells = 0;
            mGrassBandCells = 0;
            return;
        }

        takeDone();

        // Indoors the eye's coordinates belong to another space, so the rings are not moved:
        // what is held stays held for the way back out, and nothing stands.
        if (!mAround.mExterior)
        {
            mBandCells = 0;
            mGrassBandCells = 0;
            mPlacer.dropSlots();
            mSupply.publish();
            return;
        }

        const osg::Vec3f& eye = mAround.mEye;
        // One cell past the reach, which at the island route's speed is most of a second, so the
        // frame a cell crosses into the reach owes only its placements.
        const float band = mAround.mReach + mAround.mWorld.mGrid.getCellSize();

        // A cell held with the statics the other way is dropped whole and read again, for the
        // reason `takeDone` gives.
        mPlacer.dropUnless(
            [&](const HeldCell& cell) {
                return mAround.mWorld.mGrid.withinReach(cell.mCell, eye, band) && cell.mStatics == mStatics;
            },
            [&](const HeldCell& cell) { letGo(cell); });

        // A cell's grass past the grass band goes whole, and all of it where the world has none.
        const float grass = grassBand();
        mPlacer.dropGrassUnless(
            [&](const HeldGrass& held) {
                return grass > 0.0f && mAround.mWorld.mGrid.withinReach(held.mCell, eye, grass);
            },
            [&](const HeldGrass& held) { letGo(held.mModels); });

        sift(eye, band, grass);

        ask(eye, band, grass);

        // Waited for after the ask that names it and never before, because what the reader is
        // about to hand back is what that ask asked for. Nothing was asked for where the band is
        // whole, and then there is nothing to wait for.
        if (mSettled
            && ((mHanded.empty() && !mAsking.mCells.empty()) || (mHandedGrass.empty() && !mAsking.mGrass.empty())))
            waitForNext(eye, band, grass);

        adoptHanded(frame);

        stats.mLights += mPlacer.place(mAround);

        mSupply.publish();
    }
}
