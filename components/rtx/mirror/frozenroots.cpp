#include "frozenroots.hpp"

#include <cassert>
#include <span>

#include <osg/Group>

namespace Rtx
{
    FrozenRoots::Face FrozenRoots::Face::of(const osg::Node& root, const osg::Matrix& world)
    {
        const osg::Group* group = root.asGroup();
        const unsigned int children = group != nullptr ? group->getNumChildren() : 0;
        return Face{
            .mWorld = world,
            .mStateSet = root.getStateSet(),
            .mFirstChild = children > 0 ? group->getChild(0) : nullptr,
            .mChildren = children,
        };
    }

    void FrozenRoots::meet(Frozen& frozen, const unsigned int walk)
    {
        if (mMetBy != walk)
        {
            mMetBy = walk;
            mMet = 0;
        }
        if (frozen.mMet != walk)
        {
            frozen.mMet = walk;
            ++mMet;
        }
    }

    bool FrozenRoots::pass(const osg::Node& root, const osg::Matrix& world, const unsigned int walk,
        const Holders holders, ExtractionStats& stats)
    {
        const auto known = mFrozen.find(&root);
        if (known == mFrozen.end())
            return false;

        // **What can move a frozen subtree from outside it**: the game placing the root elsewhere,
        // giving it a child or taking one, or hanging a state set on it — an enchantment's glow is
        // the one the game hangs. A change deeper down is a controller's, and a controller never
        // let it freeze.
        Frozen& frozen = known->second;
        meet(frozen, walk);
        const Face face = Face::of(root, world);
        if (frozen.mFace != face)
        {
            if (frozen.mHolding)
                thaw(frozen, holders);
            frozen.mFace = face;
            frozen.mMoved = walk;
            return false;
        }

        if (!frozen.mHolding)
            return false;

        stats.mInstances += frozen.mInstances;
        stats.mPassedFrozen += frozen.mKeys.mCount;
        return true;
    }

    void FrozenRoots::record(const std::uint32_t instances)
    {
        assert(!mRecording && "a reference root recorded inside another");
        mRecording = true;
        mRecordedFrom = instances;
        mRecorded.clear();
    }

    void FrozenRoots::end(const osg::Node& root, const osg::Matrix& world, const bool changeable,
        const unsigned int walk, const Holders holders, const std::uint32_t instances)
    {
        mRecording = false;
        if (changeable || mRecorded.empty())
            return;

        // A root that moved on this walk is not frozen where it stands yet: the next walk that
        // finds its face where this one left it freezes it.
        const auto known = mFrozen.find(&root);
        assert((known == mFrozen.end() || !known->second.mHolding) && "a frozen root walked and not passed");
        if (known != mFrozen.end() && known->second.mMoved == walk)
            return;

        // Held from here on: the walk resolved every one of these this frame, and stamped it.
        for (const Key& key : mRecorded)
        {
            if (key.mPlaced)
                holders.mPlacements.hold(holders.mPlacements.find(key.mPlacement));
            holders.mMeshes.hold(*key.mDrawable);
            holders.mMaterials.hold(key.mMaterial);
        }

        const Frozen frozen{
            .mFace = Face::of(root, world),
            .mKeys = mKeys.allocate(std::span<const Key>(mRecorded)),
            .mInstances = instances - mRecordedFrom,
            .mMet = known != mFrozen.end() ? known->second.mMet : 0,
            .mMoved = known != mFrozen.end() ? known->second.mMoved : 0,
            .mHolding = true,
        };
        Frozen& held = known != mFrozen.end()
            ? known->second
            : mFrozen.emplace(osg::ref_ptr<const osg::Node>(&root), Frozen{}).first->second;
        held = frozen;
        meet(held, walk);
    }

    void FrozenRoots::thaw(Frozen& frozen, const Holders holders)
    {
        frozen.mHolding = false;
        for (const Key& key : mKeys.in(frozen.mKeys))
        {
            if (key.mPlaced)
                holders.mPlacements.drop(holders.mPlacements.find(key.mPlacement));
            holders.mMeshes.release(*key.mDrawable);
            holders.mMaterials.release(key.mMaterial);
        }

        mKeys.release(frozen.mKeys);
        frozen.mKeys = Run{};
    }

    void FrozenRoots::thawUnmet(const unsigned int walk, const Holders holders, Released& released)
    {
        if (mMetBy == walk && mMet == mFrozen.size())
            return;

        boost::unordered::erase_if(mFrozen, [&](auto& frozen) {
            if (frozen.second.mMet == walk)
                return false;

            thaw(frozen.second, holders);
            released.keep(frozen.first);
            return true;
        });
    }

    void FrozenRoots::thawAll(const Holders holders, Released& released)
    {
        for (auto& frozen : mFrozen)
        {
            thaw(frozen.second, holders);
            released.keep(frozen.first);
        }
        mFrozen.clear();
        mMet = 0;
    }
}
