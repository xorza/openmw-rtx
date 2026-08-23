#include "stagedworld.hpp"

#include <span>
#include <string>

#include <components/esm3/loadcell.hpp>

#include "cellscene.hpp"
#include "world.hpp"

namespace RtxTool
{
    StagedWorld::StagedWorld(
        World& world, const ESM::Cell& cell, const StagingRequest& request, const ActorRequest& actors)
        : mExtractor(mScene)
        , mWorld(&world)
        , mActors(actors)
    {
        const RegionLoad arrived = loadRegion(
            world, cell, *mRoot, mScene, mExtractor, mLoaded, request.mWeather, request.mHour, actors.mProps);

        mLighting = arrived.mLighting;
        mReport = std::move(arrived.mReport);

        // **Before the first walk, because the walk runs the animators.** The graph's own
        // controllers — a brazier's flipbook, a lava flow — read the clock off the traversal, and a
        // shot is only repeatable if it is told which second it is showing rather than measuring
        // one of its own.
        setSeconds(actors.mSeconds);

        // Absent for an interior, and that is what `moveTo` reads as "this never streams".
        if (cell.isExterior())
            mStanding = CellSquare{ .mX = cell.getGridX(), .mY = cell.getGridY() };

        // The first walk. Everything after this is the same walk again, once a frame.
        mStaged = mirror(0);

        // **Before anyone goes in.** A row of actors stands relative to where the camera ends up, so
        // the camera cannot be derived from bounds that already contain them.
        mPlacement = placeCamera(mScene.getBounds(), request.mFieldOfView, request.mOrigin, request.mTarget);

        const std::span<const CellPerson> residents
            = actors.mResidents ? std::span<const CellPerson>(mReport.mPeople) : std::span<const CellPerson>();

        const std::span<const CellProp> props
            = actors.mProps ? std::span<const CellProp>(mReport.mProps) : std::span<const CellProp>();

        if (actors.empty() && residents.empty() && props.empty())
            return;

        mPosed = std::make_unique<PosedActors>(world, mScene, mExtractor, *mRoot, actors);
        mPosed->addResidents(residents);
        mPosed->addProps(props);
        mPosed->addRow(actors, mPlacement);
        mSettled = mPosed->settle();
    }

    StagedWorld::~StagedWorld() = default;

    RtxBridge::ExtractionStats StagedWorld::mirror(std::size_t frame)
    {
        return mExtractor.extract(*mRoot, osg::Matrixf::identity(), 0, frame);
    }

    Crossing StagedWorld::moveTo(const osg::Vec3f& where)
    {
        if (!mStanding.has_value())
            return {};

        // **Two integers compared, and nothing spelled out.** This runs every frame of a streaming
        // run and answers no on all but a handful of them; naming the square to find that out would
        // be two allocations a frame for the privilege.
        const CellSquare square = squareAt(where);
        if (square == *mStanding)
            return {};

        mStanding = square;

        // **Open sea, and the answer is to keep what is already loaded.** Every point has a square;
        // not every square has a cell record, and a camera over the water is standing in one of the
        // ones that does not. The game holds its last grid there too.
        const ESM::Cell* cell = mWorld->findCell(cellAt(square));
        if (cell == nullptr)
            return {};

        // **The actors come out first.** The new cells are walked into whatever the scene holds, so
        // a snapshot retaken with everyone still in it would place a second copy of them on the very
        // next frame.
        if (mPosed != nullptr)
            mPosed->unplace();

        const CellReport arrived = readRegion(*mWorld, *cell, *mRoot, mLoaded, mActors.mProps);

        // **The ring that arrived and the ones that left.** The working set is a square that follows
        // the camera, not everything ever visited; without the second half this grows for as long as
        // the run lasts and stops resembling the game after the first crossing.
        const Crossing crossed{ .mArrived = arrived.mCells,
            .mDeparted = dropCellsOutside(*mWorld, *cell, *mRoot, mLoaded) };

        for (const Rtx::Light& light : arrived.mLights)
            mScene.addLight(light);

        // Built, then walked, which is the split the game has too. The walk is also what tells the
        // sweep below that the departed cells are no longer met.
        mirror(0);
        mExtractor.advance();

        if (crossed.mDeparted > 0)
            mExtractor.retire();

        if (mPosed == nullptr)
            return crossed;

        // The snapshot first, then the people who arrived with the ring: the snapshot is the still
        // world, and a resident belongs to the half of the scene that is walked in again per frame.
        mPosed->restanding();
        if (mActors.mResidents)
            mPosed->addResidents(arrived.mPeople);
        if (mActors.mProps)
            mPosed->addProps(arrived.mProps);

        mSettled = mPosed->settle();
        return crossed;
    }

    bool StagedWorld::advanceTo(float seconds)
    {
        setSeconds(seconds);
        return mPosed != nullptr && mPosed->advanceTo(seconds);
    }

    void StagedWorld::setSeconds(float seconds)
    {
        mSeconds = seconds;
        mExtractor.setSimulationTime(mSeconds);
    }

    std::size_t StagedWorld::getActorCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getCount() - mPosed->getPropCount();
    }

    std::size_t StagedWorld::getPropCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getPropCount();
    }
}
