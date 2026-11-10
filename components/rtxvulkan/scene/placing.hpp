#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/frameslots.hpp>

namespace Rtx
{
    class GpuTimer;
    class Uploads;

    /// Where a placement records, which copy it writes, what writes the host's rows and what times
    /// it — the placement's own context and not the frame's, handed to both halves of one placement
    /// so the two cannot be told two things. What a placement buries goes to the device's graveyard.
    struct Placing
    {
        VkCommandBuffer mCommands = VK_NULL_HANDLE;

        /// Which copy of the per-slot tables this writes. The caller has made sure no frame in
        /// flight is still reading it.
        FrameSlot mSlot;

        /// Null for a picture inside the interface, which is not timed —
        /// `VulkanRenderer::placeScene` says why.
        GpuTimer* mTimer = nullptr;

        /// What writes `mSlot`'s copy of every host-written table, into a batch the caller defers
        /// ahead of `mCommands`' submit: what `mCommands` records after an `order` reads them.
        Uploads& mUploads;
    };
}
