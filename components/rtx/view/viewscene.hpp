#pragma once

#include <components/rtx/renderer/slot.hpp>

namespace Rtx
{
    class Residency;
    class SceneHolder;
    class TextureFeed;

    /// A scene slot of a renderer's own — `SceneHolder::addViewScene` — given back when this goes.
    /// What a picture inside the interface holds instead of an index it has to remember to drop:
    /// a constructor that throws after the slot was taken unwinds it, and nothing can drop it
    /// twice. Move-only, because the slot is one thing; one holding nothing is what a moved-from
    /// one and a default one are, and `get` is not asked of either. **Residency forgets the scene as its slot goes**
    /// (`Residency::forgetScene`), so what stood of it retires with it.
    class ViewScene
    {
    public:
        ViewScene() = default;
        ViewScene(SceneHolder& scenes, Residency& residency, TextureFeed& feed);
        ~ViewScene();

        ViewScene(const ViewScene&) = delete;
        ViewScene& operator=(const ViewScene&) = delete;

        ViewScene(ViewScene&& other) noexcept;
        ViewScene& operator=(ViewScene&& other) noexcept;

        bool holds() const { return mScenes != nullptr; }

        SceneSlot get() const;

    private:
        void drop();

        /// Null where this holds no slot.
        SceneHolder* mScenes = nullptr;
        Residency* mResidency = nullptr;
        TextureFeed* mFeed = nullptr;
        SceneSlot mSlot;
    };
}
