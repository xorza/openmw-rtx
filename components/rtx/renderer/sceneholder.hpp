#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <components/rtx/common/index.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/structurerevision.hpp>

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
        std::uint64_t mIdentity = 0;

        /// `SceneDesc::getStructureRevision` as it stood at the last `setScene` or `extendScene`.
        StructureRevision mStructureRevision;
    };

    /// What an arrival may take on the device, and what the budget leaves what rests (`SceneHolder::getArrivalRoom`).
    struct ArrivalRoom
    {
        /// What a texture, the content given room last, may still take before the device refuses it: what a structure,
        /// given room ahead of it, may take as well.
        std::uint64_t mFree = 0;

        /// What the device's budget allows the process, every slot's content, the frame and what rests together: what
        /// is free and what the process holds, as the driver states it.
        std::uint64_t mBudget = 0;

        /// What the process holds of that and keeps, what rests included and what it has let go of not: less what
        /// rests, what is in use, and the content that rests keeps a share of the rest (`SceneUploader::restLimitOf`).
        std::uint64_t mHeld = 0;
    };

    /// What holds scenes in slots, builds and places them: the part of `Renderer` a scene's owner
    /// hands its scene to (`SceneUploader`, `ViewScene`), as an interface of its own, so it depends
    /// on neither the frame nor the interface.
    class SceneHolder
    {
    public:
        virtual ~SceneHolder() = default;

        SceneHolder(const SceneHolder&) = delete;
        SceneHolder& operator=(const SceneHolder&) = delete;

        /// Builds everything a scene needs, replacing whatever was there, and places it. `textures`
        /// are decoded already, each naming the slot it stands in (`TextureData::mSlot`), and must
        /// outlive the call.
        virtual void setScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures) = 0;

        /// The same scene with more in it: geometry and textures appended, nothing renumbered, at the
        /// cost of a cell and never of `setScene`. `arrived` is the textures the scene gained since
        /// the last call, each naming the slot it stands in.
        ///
        /// **Appends and does not place**: the caller places after it (`placeScene`), as on a frame
        /// where nothing arrived, because the top level names every instance and an arrival changed
        /// the list.
        virtual void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived) = 0;

        /// What this slot was last built from, and how far it has been extended since.
        virtual SceneHeld describeHeld(SceneSlot slot) const = 0;

        /// Appends what the device made of `slot` since the last take, and forgets it (`DeviceAnswer`): what it could
        /// not stand — a texture past the side it takes, a texture it had no room for, a mesh it had no room for and
        /// none coming — for the scene's owner to report with the rest of what content was refused
        /// (`SceneDesc::refusals`), and what each mesh's structure takes. A texture refused draws the stand-in, and a
        /// mesh refused is left out. Taken after every `setScene`, `extendScene` and `placeScene`, since a placement
        /// builds what waited for room and may find room is not coming.
        virtual void takeAnswer(SceneSlot slot, DeviceAnswer& into) = 0;

        /// What an arrival in `slot` may take on the device and what its budget leaves, or nothing from a renderer
        /// whose content takes no room it bounds: what the content that rests gives its room back to
        /// (`SceneDesc::evictResting`).
        virtual std::optional<ArrivalRoom> getArrivalRoom(SceneSlot slot) const = 0;

        /// The same scene with its instances and lights somewhere else and its actors in a new pose:
        /// rebuilds only what says where things are, plus the structure of each mesh `getDeformed`
        /// names. `scene` must be the scene `setScene` was given, because the placements index into
        /// structures this already holds. The world's ripples are read here too: the next
        /// `renderFrame` presses what the last placement or `setScene` read.
        virtual void placeScene(SceneSlot slot, const SceneDesc& scene) = 0;

        /// A scene of its own for a picture inside the interface — the inventory doll, the race
        /// preview — which a ray the frame sends must not find. Slots given back are taken over
        /// before the table grows.
        virtual SceneSlot addViewScene() = 0;

        virtual void dropViewScene(SceneSlot slot) = 0;

    protected:
        SceneHolder() = default;
    };
}
