#include "frozenroots.hpp"

#include <cassert>
#include <span>
#include <utility>

#include <osg/Group>

#include <components/rtx/common/maproom.hpp>

namespace Rtx
{
    FrozenRoots::Face FrozenRoots::Face::of(const osg::Node& root, const osg::Matrix& world, const std::size_t identity)
    {
        const osg::Group* group = root.asGroup();
        const unsigned int children = group != nullptr ? group->getNumChildren() : 0;
        return Face{
            .mWorld = world,
            .mIdentity = identity,
            .mStateSet = root.getStateSet(),
            .mFirstChild = children > 0 ? group->getChild(0) : nullptr,
            .mChildren = children,
            .mUpdate = root.getUpdateCallback(),
            .mUpdating = root.getNumChildrenRequiringUpdateTraversal(),
        };
    }

    namespace
    {
        template <class T>
        void replaceHeld(osg::ref_ptr<const T>& held, const T* const now, Released& released)
        {
            if (held.get() == now)
                return;
            released.keep(held);
            held = now;
        }
    }

    void FrozenRoots::HeldFace::become(const Face& face, Released& released)
    {
        mWorld = face.mWorld;
        mIdentity = face.mIdentity;
        mChildren = face.mChildren;
        mUpdating = face.mUpdating;
        replaceHeld(mStateSet, face.mStateSet, released);
        replaceHeld(mFirstChild, face.mFirstChild, released);
        replaceHeld(mUpdate, face.mUpdate, released);
    }

    bool FrozenRoots::HeldFace::shows(const Face& face) const
    {
        return mWorld == face.mWorld && mIdentity == face.mIdentity && mChildren == face.mChildren
            && mUpdating == face.mUpdating && mStateSet.get() == face.mStateSet && mFirstChild.get() == face.mFirstChild
            && mUpdate.get() == face.mUpdate;
    }

    void FrozenRoots::HeldFace::release(Released& released)
    {
        released.keep(mStateSet);
        released.keep(mFirstChild);
        released.keep(mUpdate);
        mStateSet = nullptr;
        mFirstChild = nullptr;
        mUpdate = nullptr;
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

    bool FrozenRoots::pass(
        const osg::Node& root, const Face& face, const unsigned int walk, const Holders holders, ExtractionStats& stats)
    {
        const auto known = mFrozen.find(&root);
        if (known == mFrozen.end())
            return false;

        // **What can move a frozen subtree from outside it**: the game placing the root elsewhere,
        // giving it a child or taking one, hanging a state set on it — an enchantment's glow is
        // the one the game hangs — or a controller anywhere in it, which `Face` says how it sees.
        Frozen& frozen = known->second;
        meet(frozen, walk);
        if (!frozen.mFace.shows(face))
        {
            if (frozen.mHolding)
                thaw(frozen, holders);
            frozen.mFace.become(face, holders.mReleased);
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

    void FrozenRoots::end(const osg::Node& root, const Face& face, const bool changeable, const unsigned int walk,
        const Holders holders, const std::uint32_t instances)
    {
        mRecording = false;
        if (changeable || face.mUpdating > 0 || mRecorded.empty())
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

        // A root no walk froze before waits apart until the walk ends, and the roots it left are
        // thawed first (`thawUnmet`).
        if (known == mFrozen.end())
        {
            mPastRoom += mArrived.size() == mArrived.capacity() ? 1u : 0u;
            mArrived.emplace_back(osg::ref_ptr<const osg::Node>(&root), Frozen{});
        }
        Frozen& held = known != mFrozen.end() ? known->second : mArrived.back().second;
        held.mFace.become(face, holders.mReleased);
        held.mKeys = mKeys.allocate(std::span<const Key>(mRecorded));
        held.mInstances = instances - mRecordedFrom;
        held.mHolding = true;
        if (known != mFrozen.end())
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

    void FrozenRoots::letGo(const osg::ref_ptr<const osg::Node>& root, Frozen& frozen, const Holders holders)
    {
        thaw(frozen, holders);
        frozen.mFace.release(holders.mReleased);
        holders.mReleased.keep(root);
    }

    void FrozenRoots::thawUnmet(const unsigned int walk, const Holders holders)
    {
        if (mMetBy != walk || mMet != mFrozen.size())
            boost::unordered::erase_if(mFrozen, [&](auto& frozen) {
                if (frozen.second.mMet == walk)
                    return false;

                letGo(frozen.first, frozen.second, holders);
                return true;
            });

        // Now that what the walk left is gone, what it froze first. What a walk that threw left here files with
        // them, and the next walk thaws any of it that this one did not meet. `try_emplace` moves nothing where
        // the root is filed already.
        for (auto& [root, arrived] : mArrived)
        {
            const bool full = fillsRoom(mFrozen);
            const auto [filed, fresh] = mFrozen.try_emplace(std::move(root), std::move(arrived));
            // One root met twice, as a walk that threw left it and the walk after met it again, files once,
            // and the other's holds go back: no new key, which is what takes room.
            if (!fresh)
            {
                letGo(root, arrived, holders);
                continue;
            }
            mPastRoom += full ? 1u : 0u;
            meet(filed->second, walk);
        }
        mArrived.clear();
    }

    void FrozenRoots::thawAll(const Holders holders)
    {
        for (auto& [root, frozen] : mFrozen)
            letGo(root, frozen, holders);
        for (auto& [root, frozen] : mArrived)
            letGo(root, frozen, holders);
        mFrozen.clear();
        mArrived.clear();
        mMet = 0;
    }
}
