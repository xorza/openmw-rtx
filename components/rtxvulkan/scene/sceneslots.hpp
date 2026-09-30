#pragma once

#include <memory>
#include <vector>

#include <components/rtx/common/slots.hpp>
#include <components/rtx/renderer/slot.hpp>

#include "devicescene.hpp"

namespace Rtx
{
    /// The scenes a renderer holds, by slot: the world's, and one for each picture inside the
    /// interface — null until each is given a scene — with the slots nothing holds.
    class SceneSlots
    {
    public:
        SceneSlots() = default;

        /// Asserts every picture's slot given back: a view that outlives its renderer is a scene
        /// held for nothing, named here rather than never. Not while an exception unwinds.
        ~SceneSlots();

        /// A slot for a picture's scene, empty until something is put into it.
        SceneSlot add();

        /// Lets the slot go, and what it holds with it, as `clear` does.
        void drop(SceneSlot slot);

        /// Destroys what `slot` holds. Everything a scene holds on the device buries itself under
        /// the next submit, so a picture of it recorded and not yet carried, and the last
        /// placement's refit, both run over what they recorded.
        void clear(SceneSlot slot);

        /// Puts `scene` into `slot`, which holds nothing.
        DeviceScene& hold(SceneSlot slot, std::unique_ptr<DeviceScene> scene);

        /// What `slot` holds, or null. A slot nothing handed out is a caller bug, and asserted.
        const DeviceScene* find(SceneSlot slot) const;
        DeviceScene* find(SceneSlot slot);

        /// What `slot` holds, which something must: asked of an empty slot is a caller bug, and
        /// asserted.
        const DeviceScene& at(SceneSlot slot) const;
        DeviceScene& at(SceneSlot slot);

        /// `visit` over every scene a slot holds, the world's first.
        template <class Visit>
        void forEach(Visit&& visit)
        {
            if (mWorld != nullptr)
                visit(*mWorld);
            for (const std::unique_ptr<DeviceScene>& view : mViews)
                if (view != nullptr)
                    visit(*view);
        }

    private:
        const std::unique_ptr<DeviceScene>& slotAt(SceneSlot slot) const;
        std::unique_ptr<DeviceScene>& slotAt(SceneSlot slot);

        std::unique_ptr<DeviceScene> mWorld;
        std::vector<std::unique_ptr<DeviceScene>> mViews;
        SlotPool mFreeViews;
    };
}
