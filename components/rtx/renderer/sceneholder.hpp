#pragma once

#include <cstdint>
#include <span>

#include <components/rtx/common/index.hpp>
#include <components/rtx/image/texturedata.hpp>
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
        /// texture array are the first description's, and appending the second's arrivals onto
        /// them would begin past the end of its own table. Nought where nothing was built, which
        /// no description's identity is.
        std::uint64_t mIdentity = 0;

        /// `SceneDesc::getStructureRevision` as it stood at the last `setScene` or `extendScene`.
        StructureRevision mStructureRevision;
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

        /// The same scene with more in it: geometry and textures appended, nothing renumbered, at
        /// the cost of a cell and never of `setScene`. `arrived` is the textures the scene gained
        /// since the last call, each naming the slot it stands in — a slot a departure freed, or
        /// one past the count this already holds.
        ///
        /// **Appends and does not place**: the caller places after it (`placeScene`), as on a frame
        /// where nothing arrived, because the top level names every instance and an arrival changed
        /// the list.
        virtual void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived) = 0;

        /// What this slot was last built from, and how far it has been extended since.
        virtual SceneHeld describeHeld(SceneSlot slot) const = 0;

        /// What the last `setScene` or `extendScene` of `slot` could not stand for the device's
        /// sake — a texture past the side it takes, a texture or a mesh it had no room for — for
        /// the scene's owner to report with the rest of what content was refused
        /// (`SceneDesc::refusals`). A texture refused draws the stand-in, and a mesh refused is
        /// left out. Valid until the next call of either for `slot`.
        virtual std::span<const Refusal> getRefusals(SceneSlot slot) const = 0;

        /// Destroys the images of the texture slots a scene gave up. The slots keep their place,
        /// as they do in the scene's own table, and no live material names a freed one.
        virtual void dropTextures(SceneSlot slot, std::span<const Index> textures) = 0;

        /// The same scene with its instances and lights somewhere else and its actors in a new
        /// pose: rebuilds only what says where things are, plus the structure of each mesh
        /// `getDeformed` names. `scene` must be the scene `setScene` was given, because the
        /// placements index into structures this already holds. The world's ripples are read here
        /// too, beside its lights and sprites: what disturbed the water is a per-frame list of the
        /// scene like the other three, and the next `renderFrame` presses what the last placement
        /// or `setScene` read.
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
