#include "viewscene.hpp"

#include <cassert>
#include <utility>

#include <components/rtx/renderer/sceneholder.hpp>

namespace Rtx
{
    ViewScene::ViewScene(SceneHolder& scenes)
        : mScenes(&scenes)
        , mSlot(scenes.addViewScene())
    {
    }

    ViewScene::~ViewScene()
    {
        drop();
    }

    ViewScene::ViewScene(ViewScene&& other) noexcept
        : mScenes(std::exchange(other.mScenes, nullptr))
        , mSlot(other.mSlot)
    {
    }

    ViewScene& ViewScene::operator=(ViewScene&& other) noexcept
    {
        if (this != &other)
        {
            drop();
            mScenes = std::exchange(other.mScenes, nullptr);
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
        if (mScenes != nullptr)
            mScenes->dropViewScene(mSlot);
        mScenes = nullptr;
    }
}
