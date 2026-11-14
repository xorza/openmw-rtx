#include "sceneslots.hpp"

#include <cassert>
#include <exception>
#include <utility>

#include "devicescene.hpp"

namespace Rtx
{
    SceneSlots::SceneSlots() = default;

    SceneSlots::~SceneSlots()
    {
        assert((std::uncaught_exceptions() > 0 || mViews.getLiveCount() == 0)
            && "a renderer taken apart with a picture's scene slot still out");
    }

    SceneSlot SceneSlots::add()
    {
        return SceneSlot::view(mViews.take(nullptr));
    }

    void SceneSlots::drop(const SceneSlot slot)
    {
        assert(!slot.isWorld() && "the world's slot given back");
        assert(slot.getViewIndex() < mViews.size() && "a scene slot nothing handed out");
        assert(mViews.isLive(slot.getViewIndex()) && "a scene given back twice");

        clear(slot);
        mViews.free(slot.getViewIndex());
    }

    void SceneSlots::clear(const SceneSlot slot)
    {
        slotAt(slot).reset();
    }

    DeviceScene& SceneSlots::hold(const SceneSlot slot, std::unique_ptr<DeviceScene> scene)
    {
        std::unique_ptr<DeviceScene>& held = slotAt(slot);
        assert(held == nullptr && "a scene put over another");
        held = std::move(scene);
        return *held;
    }

    const std::unique_ptr<DeviceScene>& SceneSlots::slotAt(const SceneSlot slot) const
    {
        if (slot.isWorld())
            return mWorld;

        assert(slot.getViewIndex() < mViews.size() && "a scene slot nothing handed out");
        return mViews.at(slot.getViewIndex());
    }

    std::unique_ptr<DeviceScene>& SceneSlots::slotAt(const SceneSlot slot)
    {
        // The const lookup's answer, on an object the caller holds mutable: one lookup for both overloads.
        return const_cast<std::unique_ptr<DeviceScene>&>(std::as_const(*this).slotAt(slot));
    }

    const DeviceScene* SceneSlots::find(const SceneSlot slot) const
    {
        return slotAt(slot).get();
    }

    DeviceScene* SceneSlots::find(const SceneSlot slot)
    {
        return slotAt(slot).get();
    }

    const DeviceScene& SceneSlots::at(const SceneSlot slot) const
    {
        const DeviceScene* held = find(slot);
        assert(held != nullptr && "a scene slot nothing holds");
        return *held;
    }

    DeviceScene& SceneSlots::at(const SceneSlot slot)
    {
        // The const lookup's answer, on an object the caller holds mutable: one lookup for both overloads.
        return const_cast<DeviceScene&>(std::as_const(*this).at(slot));
    }
}
