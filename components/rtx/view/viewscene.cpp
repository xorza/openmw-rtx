#include "viewscene.hpp"

#include <cassert>
#include <utility>

#include <components/rtx/renderer/residency.hpp>
#include <components/rtx/renderer/sceneholder.hpp>

namespace Rtx
{
    ViewScene::ViewScene(SceneHolder& scenes, Residency& residency, TextureFeed& feed)
        : mScenes(&scenes)
        , mResidency(&residency)
        , mFeed(&feed)
        , mSlot(scenes.addViewScene())
    {
    }

    ViewScene::~ViewScene()
    {
        drop();
    }

    ViewScene::ViewScene(ViewScene&& other) noexcept
        : mScenes(std::exchange(other.mScenes, nullptr))
        , mResidency(other.mResidency)
        , mFeed(other.mFeed)
        , mSlot(other.mSlot)
    {
    }

    ViewScene& ViewScene::operator=(ViewScene&& other) noexcept
    {
        if (this != &other)
        {
            drop();
            mScenes = std::exchange(other.mScenes, nullptr);
            mResidency = other.mResidency;
            mFeed = other.mFeed;
            mSlot = other.mSlot;
        }

        return *this;
    }

    SceneSlot ViewScene::get() const
    {
        assert(holds() && "the slot of a view scene that holds none");
        return mSlot;
    }

    void ViewScene::drop()
    {
        if (mScenes == nullptr)
            return;
        if (mResidency->holds(mSlot))
            mResidency->forgetScene(mSlot, *mFeed);
        mScenes->dropViewScene(mSlot);
        mScenes = nullptr;
    }
}
