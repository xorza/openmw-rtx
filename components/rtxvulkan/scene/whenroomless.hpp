#pragma once

#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    /// What content the device has no room for does over `scene` now: waits where room is coming — what rests, which
    /// the scene's owner gives back for it, or content the graveyard gives back once the frames in flight are done —
    /// and is refused where none is.
    inline WhenRoomless whenRoomless(const Device& device, const SceneDesc& scene)
    {
        const Graveyard& graveyard = device.getGraveyard();
        const bool coming = scene.getRestingBytes() > 0 || graveyard.getBuried(MemoryUse::Structure) > 0
            || graveyard.getBuried(MemoryUse::Texture) > 0;
        return coming ? WhenRoomless::Waits : WhenRoomless::Refuses;
    }
}
