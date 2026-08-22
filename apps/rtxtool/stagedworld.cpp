#include "stagedworld.hpp"

#include <span>

#include "cellscene.hpp"
#include "world.hpp"

namespace RtxTool
{
    StagedWorld::StagedWorld(
        World& world, const ESM::Cell& cell, const StagingRequest& request, const ActorRequest& actors)
        : mExtractor(mScene)
    {
        const RegionLoad arrived
            = loadRegion(world, cell, *mRoot, mScene, mExtractor, mLoaded, request.mWeather, request.mHour, actors.mProps);

        mLighting = arrived.mLighting;

        // The first walk. Everything after this is the same walk again, once a frame.
        mirror(0);

        // **Before anyone goes in.** A row of actors stands relative to where the camera ends up, so
        // the camera cannot be derived from bounds that already contain them.
        mPlacement = placeCamera(mScene.getBounds(), request.mFieldOfView, request.mOrigin, request.mTarget);

        const std::span<const CellPerson> residents
            = actors.mResidents ? std::span<const CellPerson>(arrived.mPeople) : std::span<const CellPerson>();

        const std::span<const CellProp> props
            = actors.mProps ? std::span<const CellProp>(arrived.mProps) : std::span<const CellProp>();

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

    std::size_t StagedWorld::getActorCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getCount() - mPosed->getPropCount();
    }

    std::size_t StagedWorld::getPropCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getPropCount();
    }
}
