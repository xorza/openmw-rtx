#pragma once

#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    /// What content the device has no room for does over `scene` now: waits where room is coming — what rests, which
    /// the scene's owner gives back for it, content the graveyard gives back once the frames in flight are done, or a
    /// block the moves give back (`BlocksComing`) — and is refused where none is. **A block the moves will empty is
    /// room coming**: the refusal asks for it, and the handheld's PBR drew the stand-in at Seyda Neen for good with a
    /// structure block's worth free, refused in the take that set the moves off.
    inline WhenRoomless whenRoomless(const Device& device, const SceneDesc& scene)
    {
        const Graveyard& graveyard = device.getGraveyard();
        const bool coming = scene.getRestingBytes() > 0 || graveyard.getBuried(MemoryUse::Structure) > 0
            || graveyard.getBuried(MemoryUse::Texture) > 0 || device.getMemory().getBlocksComing() > 0;
        return coming ? WhenRoomless::Waits : WhenRoomless::Refuses;
    }
}
