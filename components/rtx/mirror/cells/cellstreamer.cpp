#include "cellstreamer.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
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

    CellStreamer::CellStreamer(SceneAdopter& adopter)
        : mAdopter(adopter)
        , mPlacer(adopter.getScene())
    {
    }

    CellStreamer::~CellStreamer()
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

    void CellStreamer::follow(const WorldAround& around)
    {
        // From either: a world told twice before a walk is the last word, and a detach tells the
        // ring of no world whether or not a walk came between.
        mTurn.step(Turn::Followed, Turn::Collected, Turn::Followed);
        mAround = around;
        mPlacer.setNightDay(around.mNightDay);

        if (mSupply.isReading(around.mWorld))
            return;

        // Said, with what moved: a reader made again drops every cell it held, which a field that changes when
        // nothing should, as a mask or the state above, would do every frame.
        if (mSupply.hasReader())
            Log(Debug::Info) << "Ray tracing reads the cells again, for "
                             << describeChange(mSupply.getWorld(), around.mWorld);

        // Everything held names the reader that is about to go, so it is let go of before the
        // supply is pointed anywhere else. Nothing is given back: what the frame held dies with the
        // reader that lent it.
        forget();
        mSupply.follow(around.mWorld);
    }

    void CellStreamer::forget()
    {
        // Nothing is given back — what the reader lent dies with it — but the rows are the frame's
        // own, and a worldspace change is the one time this runs while the game plays.
        mPlacer.dropUnless([](const HeldCell&) { return false; }, [](const HeldCell&) {});
        mPlacer.dropGrassUnless([](const HeldGrass&) { return false; }, [](const HeldGrass&) {});

        mHolds.forget();
        mAsked.reset();
        mBandCells = 0;
        mGrassBandCells = 0;
        mBandHeld = 0;
        mGrassBandHeld = 0;
    }

    void CellStreamer::setSettled(const bool settled)
    {
        mSettled = settled;
    }

    std::uint32_t CellStreamer::getCellsToStand() const
    {
        // **Counted against the band and not against the ask**, which names what was missing when
        // it was made: a walk that waited for its cell adopted one of them since. A walk that stood
        // nothing has nothing to stand, though it keeps what it held for the way back out.
        if (mBandCells == 0)
            return 0;

        assert(mBandHeld <= mBandCells && "more cells held inside the band than the band has");
        assert(mGrassBandHeld <= mGrassBandCells && "more grass held inside the band than the band has");
        return mBandCells - mBandHeld + mGrassBandCells - mGrassBandHeld;
    }

    bool CellStreamer::waitsUnder(const osg::Vec2f& low, const osg::Vec2f& high) const
    {
        if (mBandCells == 0 || !mAsked.has_value())
            return false;

        // The cells the square overlaps by some area, so one that only touches its edge is out: a
        // map tile's box is its cell's square exactly.
        const CellGrid& grid = mAround.mWorld.mGrid;
        const float side = grid.getCellSize();
        const auto first = [&](float at) { return static_cast<std::int32_t>(std::floor(at / side)); };
        const auto last = [&](float at) { return static_cast<std::int32_t>(std::ceil(at / side)) - 1; };
        for (std::int32_t x = first(low.x()); x <= last(high.x()); ++x)
            for (std::int32_t y = first(low.y()); y <= last(high.y()); ++y)
            {
                const osg::Vec2i cell(x, y);
                if (grid.withinReach(cell, mAsked->mEye, mAsked->mBand) && !mPlacer.holds(cell))
                    return true;
            }

        return false;
    }

    void CellStreamer::know(const std::span<PreparedModel* const> models)
    {
        for (PreparedModel* model : models)
            ++mHolds.know(*model).mNamed;
    }

    template <class Prepared>
    void CellStreamer::adoptModels(Prepared& prepared)
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

    std::uint32_t CellStreamer::countHeldInBand() const
    {
        std::uint32_t held = 0;
        if (mBandCells > 0 && mAsked.has_value())
            mAround.mWorld.mGrid.forEachCellWithin(
                mAsked->mEye, mAsked->mBand, [&](const osg::Vec2i& cell) { held += mPlacer.holds(cell) ? 1 : 0; });
        return held;
    }

    std::uint32_t CellStreamer::countGrassHeldInBand() const
    {
        std::uint32_t held = 0;
        if (mGrassBandCells > 0 && mAsked.has_value())
            mAround.mWorld.mGrid.forEachCellWithin(mAsked->mEye, mAsked->mGrassBand,
                [&](const osg::Vec2i& cell) { held += mPlacer.holdsGrass(cell) ? 1 : 0; });
        return held;
    }

    float CellStreamer::keptTo(const float band) const
    {
        return band > 0.0f ? band + mAround.mWorld.mGrid.getCellSize() : 0.0f;
    }

    float CellStreamer::grassBand() const
    {
        if (mAround.mWorld.mGroundcover == nullptr || !(mAround.mGroundcoverReach > 0.0f))
            return 0.0f;

        return mAround.mGroundcoverReach + mAround.mWorld.mGrid.getCellSize();
    }

    void CellStreamer::standNothing()
    {
        // The target with its counts: a target kept past them answers the next walk that asks the same as unchanged,
        // and leaves the counts at nought over a band that still lacks its cells.
        mAsked.reset();
        mBandCells = 0;
        mGrassBandCells = 0;
        mBandHeld = 0;
        mGrassBandHeld = 0;
    }

    void CellStreamer::retarget(const CellTarget& target)
    {
        mAsked = target;

        mDropped.clear();
        mSupply.retarget(target, mDropped);
        for (PreparedCell* cell : mDropped.mCells)
        {
            letGo(cell->mModels);
            CellReturns& back = mSupply.giveBack();
            cell->mGround.collectTextures(back.mTextures);
            back.mCells.push_back(cell);
        }
        for (PreparedGrass* grass : mDropped.mGrass)
        {
            letGo(grass->mModels);
            mSupply.giveBack().mGrass.push_back(grass);
        }

        // What the table let go of standing, by the same rule; what the band holds is what a load waits on.
        const CellGrid& grid = target.mGrid;
        mPlacer.dropUnless(
            [&](const HeldCell& cell) { return grid.withinReach(cell.mCell, target.mEye, target.mKept); },
            [&](const HeldCell& cell) { letGo(cell); });
        mPlacer.dropGrassUnless(
            [&](const HeldGrass& held) {
                return target.mGrassKept > 0.0f && grid.withinReach(held.mCell, target.mEye, target.mGrassKept);
            },
            [&](const HeldGrass& held) { letGo(held.mModels); });
        mPlacer.retierGrounds(
            [&](const osg::Vec2i& cell) { return grid.withinReach(cell, target.mEye, target.mBand); });

        mBandCells = 0;
        mBandHeld = 0;
        grid.forEachCellWithin(target.mEye, target.mBand, [&](const osg::Vec2i& cell) {
            ++mBandCells;
            mBandHeld += mPlacer.holds(cell) ? 1u : 0u;
        });
        mGrassBandCells = 0;
        mGrassBandHeld = 0;
        if (target.mGrassBand > 0.0f)
            grid.forEachCellWithin(target.mEye, target.mGrassBand, [&](const osg::Vec2i& cell) {
                ++mGrassBandCells;
                mGrassBandHeld += mPlacer.holdsGrass(cell) ? 1u : 0u;
            });
    }

    void CellStreamer::adoptRead(const std::size_t frame)
    {
        // Counted as each arrives, so the frame knows of every model a cell it may adopt names, and a model a cell
        // let go of and a read cell names again keeps its holds.
        mFiledScratch.clear();
        mFiledGrassScratch.clear();
        mSupply.collectFiled(mFiledScratch, mFiledGrassScratch);
        for (const PreparedCell* cell : mFiledScratch)
            know(cell->mModels);
        for (const PreparedGrass* grass : mFiledGrassScratch)
            know(grass->mModels);

        // One cell a frame, and one frame walked twice adopts once. A cell's meshes are copied into the scene and its
        // structures built by the hand-over that follows; two on one frame would be the batch behind a threshold this
        // renderer never takes. A settled walk keeps the rule and waits for its one cell, which is what `setSettled`
        // says.
        if (mAdoptedFrame != frame)
            if (PreparedCell* const cell = mSupply.adoptCell(); cell != nullptr)
            {
                mAdoptedFrame = frame;
                adopt(*cell);
            }

        if (mGrassAdoptedFrame != frame)
            if (PreparedGrass* const grass = mSupply.adoptGrass(); grass != nullptr)
            {
                mGrassAdoptedFrame = frame;
                adopt(*grass);
            }
    }

    void CellStreamer::adopt(PreparedCell& cell)
    {
        const Crash::NoteScope noted("adopting the cell {}, {}", cell.mCell.x(), cell.mCell.y());
        // **Never a second copy**: the table reads a cell once while a band holds it, and a copy adopted twice would
        // stand every reference twice.
        Crash::contract(!mPlacer.holds(cell.mCell), "the supply handed over a cell the ring already had");
        adoptModels(cell);

        // The ground after the models, as the groundcover's: where the texture table has no room
        // left, the ground's layers take the neutral texel and the models keep theirs, as the
        // ground gives way where the device's room runs out (`TextureArray::write`).
        mPlacer.holdCell(cell, mAround, mAdopter.getStats(), mHolds);
        mAdopter.getStats().mCellsAdoptedInReach
            += mAround.mWorld.mGrid.withinReach(cell.mCell, mAround.mEye, mAround.mReach) ? 1u : 0u;
        if (mAsked.has_value() && mAround.mWorld.mGrid.withinReach(cell.mCell, mAsked->mEye, mAsked->mBand))
        {
            ++mBandHeld;
            mPlacer.retierGround(cell.mCell, HoldTier::Shown);
        }

        mSupply.giveBack().mCells.push_back(&cell);
    }

    void CellStreamer::adopt(PreparedGrass& grass)
    {
        const Crash::NoteScope noted("adopting the groundcover of the cell {}, {}", grass.mCell.x(), grass.mCell.y());
        Crash::contract(!mPlacer.holdsGrass(grass.mCell), "the supply handed over a cell's grass the ring already had");
        adoptModels(grass);

        mPlacer.holdGrass(grass, mHolds);
        if (mAsked.has_value() && mAsked->mGrassBand > 0.0f
            && mAround.mWorld.mGrid.withinReach(grass.mCell, mAsked->mEye, mAsked->mGrassBand))
            ++mGrassBandHeld;

        mSupply.giveBack().mGrass.push_back(&grass);
    }

    void CellStreamer::letGo(const std::span<PreparedModel* const> models)
    {
        for (PreparedModel* model : models)
            mHolds.release(*model);

        CellReturns& back = mSupply.giveBack();
        back.mModels.insert(back.mModels.end(), models.begin(), models.end());
    }

    void CellStreamer::letGo(const HeldCell& cell)
    {
        letGo(cell.mModels);
        CellReturns& back = mSupply.giveBack();
        back.mTextures.insert(back.mTextures.end(), cell.mGround.mTextures.begin(), cell.mGround.mTextures.end());
    }

    void CellStreamer::setReferenceEnabled(const ESM::RefNum refnum, const bool enabled)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.setReferenceEnabled(refnum, enabled);
    }

    void CellStreamer::blacklistReference(const ESM::RefNum refnum)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.blacklistReference(refnum);
    }

    void CellStreamer::moveReference(
        const ESM::RefNum refnum, const ReferencePlace& place, const ReferenceStands stands)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.moveReference(refnum, place, stands);
    }

    void CellStreamer::forgetReferences()
    {
        mTurn.expect(Turn::Collected);
        mPlacer.forgetReferences();
    }

    void CellStreamer::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        mTurn.expect(Turn::Collected);
        mPlacer.setGate(gate, state);
    }

    void CellStreamer::collectStanding(std::vector<ESM::RefNum>& into) const
    {
        mPlacer.collectStanding(into);
    }

    void CellStreamer::collectGateVerdicts(std::vector<GateVerdict>& into) const
    {
        mPlacer.collectGateVerdicts(mAround.mActiveGrid, into);
    }

    void CellStreamer::collect(const std::size_t frame)
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
        assert(mBandHeld == countHeldInBand() && "the band's held cells were miscounted");
        assert(mGrassBandHeld == countGrassHeldInBand() && "the grass band's held cells were miscounted");
        assert(!mSupply.hasReader() || mSupply.countStood(ReadKind::Cell) == getHeldCellCount());
        assert(!mSupply.hasReader() || mSupply.countStood(ReadKind::Grass) == getHeldGrassCount());

        // What stands, counted off the slots and not off a tally: a world with no reader and an
        // interior have both dropped every slot by now, and stand nothing.
        stats.mInstances += mPlacer.getPlaced() + mPlacer.getGroundPlaced() + mPlacer.getGrassPlaced();
        stats.mDistantStatics += mPlacer.getPlaced();
        stats.mGroundCells += mPlacer.getGroundPlaced();
        stats.mGroundcover += mPlacer.getGrassPlaced();
    }

    void CellStreamer::walkRings(ExtractionStats& stats, const std::size_t frame)
    {
        // Nothing stands, so nothing is left to stand: an exterior band short of cells must not
        // outlive the walk that asked over it.
        if (!mSupply.hasReader())
        {
            standNothing();
            return;
        }

        // Indoors the eye's coordinates belong to another space, so the bands are not moved: what is held stays held
        // for the way back out, nothing stands, and nothing more is read.
        if (!mAround.mExterior)
        {
            // **Indoors the land rests**: its grounds held `Kept`, which the plan keeps while they fit and gives to the
            // room where they do not.
            if (mAsked.has_value())
            {
                mSupply.withdraw();
                mPlacer.retierGrounds([](const osg::Vec2i&) { return false; });
            }
            standNothing();
            mPlacer.dropSlots();
            mSupply.publish();
            return;
        }

        // One cell past the reach, which at the island route's speed is most of a second, so the
        // frame a cell crosses into the reach owes only its placements.
        const float band = mAround.mReach + mAround.mWorld.mGrid.getCellSize();
        const float grass = grassBand();
        const CellTarget target{
            .mGrid = mAround.mWorld.mGrid,
            .mEye = mAround.mEye,
            .mBand = band,
            .mKept = keptTo(band),
            .mGrassBand = grass,
            .mGrassKept = keptTo(grass),
        };
        if (mAsked != target)
            retarget(target);

        // Waited for after the retarget that names it and never before, because what the reader is about to file is
        // what that retarget wants. Nothing is owed where the band is whole, and then there is nothing to wait for.
        if (mSettled)
        {
            mSupply.awaitOwed(ReadKind::Cell);
            mSupply.awaitOwed(ReadKind::Grass);
        }

        adoptRead(frame);

        stats.mLights += mPlacer.place(mAround);

        mSupply.publish();
    }
}
