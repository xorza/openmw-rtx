#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtx/scene/structurerevision.hpp>

#include "residencydevice.hpp"
#include "slot.hpp"

namespace Rtx
{
    class SceneDesc;

    /// What a backend holds in one of its slots, as it says so itself. A slot and a scene are one
    /// to one, so nothing here has to name which scene.
    struct SceneHeld
    {
        /// `SceneDesc::getIdentity` of the description the slot was built from: an uploader
        /// handing another description to a slot has to build, because the structures and the
        /// texture array are the first description's. Nought where nothing was built, which no
        /// description's identity is.
        SceneIdentity mIdentity{};

        /// `SceneDesc::getStructureRevision` as it stood at the last `setScene` or `extendScene`.
        StructureRevision mStructureRevision;
    };

    /// What a structure's arrival may take on the device, and what the budget leaves what rests: the meshes' room until
    /// their structures stand in the content arena (`SceneHolder::getArrivalRoom`).
    struct ArrivalRoom
    {
        /// What a structure may still take: what the structure storages' blocks have free and the whole blocks the
        /// structures' ceiling leaves room for.
        std::uint64_t mStructureFree = 0;

        /// What the device's budget allows the process, every slot's content, the frame and what rests together.
        std::uint64_t mBudget = 0;

        /// What the process holds of that and keeps, what rests included and what it has let go of not.
        std::uint64_t mHeld = 0;
    };

    /// What holds scenes in slots, builds and places them: the part of `Renderer` a scene's owner hands its scene to
    /// (`SceneUploader`, `ViewScene`), as an interface of its own, so it depends on neither the frame nor the
    /// interface.
    /// **What content stands is residency's** (`ResidencyDevice`): a hand-over appends what arrived, does residency's
    /// operations and places, one submit.
    class SceneHolder : public ResidencyDevice
    {
    public:
        virtual ~SceneHolder() = default;

        SceneHolder(const SceneHolder&) = delete;
        SceneHolder& operator=(const SceneHolder&) = delete;

        /// **A slot's first build, and nothing else**: `scene`'s geometry and tables, replacing whatever the slot held,
        /// every texture drawing the stand-in until residency stands it. What residency had standing of the slot's last
        /// scene is its to forget (`Residency::restartScene`).
        virtual void standScene(SceneSlot slot, const SceneDesc& scene) = 0;

        /// What this slot was last built from, and how far it has been extended since.
        virtual SceneHeld describeHeld(SceneSlot slot) const = 0;

        /// Appends what the device made of `slot`'s meshes since the last take, and forgets it (`DeviceAnswer`): a mesh
        /// it had no room for and none coming, and what each mesh's structure takes, until structures stand in the
        /// content arena.
        virtual void takeAnswer(SceneSlot slot, DeviceAnswer& into) = 0;

        /// What a structure's arrival in `slot` may take on the device and what its budget leaves, or nothing from a
        /// renderer whose content takes no room it bounds.
        virtual std::optional<ArrivalRoom> getArrivalRoom(SceneSlot slot) const = 0;

        /// A scene of its own for a picture inside the interface — the inventory doll, the race preview — which a ray
        /// the frame sends must not find. Slots given back are taken over before the table grows.
        virtual SceneSlot addViewScene() = 0;

        virtual void dropViewScene(SceneSlot slot) = 0;

    protected:
        SceneHolder() = default;
    };
}
