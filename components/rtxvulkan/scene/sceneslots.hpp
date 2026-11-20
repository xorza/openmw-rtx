#pragma once

#include <memory>

#include <components/rtx/common/slots.hpp>
#include <components/rtx/renderer/slot.hpp>

namespace Rtx
{
    class DeviceScene;

    /// The scenes a renderer holds, by slot: the world's, and one for each picture inside the
    /// interface — null until each is given a scene — with the slots nothing holds.
    class SceneSlots
    {
    public:
        SceneSlots();

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

        /// `visit` over every scene a slot holds and its slot, the world's first.
        template <class Visit>
        void forEach(Visit&& visit)
        {
            if (mWorld != nullptr)
                visit(SceneSlot::world(), *mWorld);
            const auto views = mViews.getRows();
            for (std::uint32_t at = 0; at < views.size(); ++at)
                if (views[ViewRow(at)] != nullptr)
                    visit(SceneSlot::view(at), *views[ViewRow(at)]);
        }

    private:
        const std::unique_ptr<DeviceScene>& slotAt(SceneSlot slot) const;
        std::unique_ptr<DeviceScene>& slotAt(SceneSlot slot);

        std::unique_ptr<DeviceScene> mWorld;
        /// A freed slot's scene is let go at its drop, so a null row is a slot nothing holds whether or not it is out.
        using ViewRow = RowIndex<struct ViewSceneRows>;
        SlotRows<std::unique_ptr<DeviceScene>, ViewRow> mViews;
    };
}
