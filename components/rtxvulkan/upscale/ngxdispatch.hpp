#pragma once

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// **The lookups NGX resolves every Vulkan function it calls through.** `NVSDK_NGX_VULKAN_Init`
    /// takes the two it is to use, which is the SDK's own place for an application to stand between
    /// NGX and the driver: `Dlss` hands it these, and they answer with the loader's functions but for
    /// the one this renderer corrects, `vkCmdPipelineBarrier`.
    ///
    /// **What is corrected is a dependency NGX leaves out of its own recordings, and a dependency is
    /// only ever widened here, never narrowed or dropped.** The second generation of Ray
    /// Reconstruction (preset F, from NGX 310.9.1) transitions an image of its own and clears it
    /// straight after, and the barrier it records for the transition names shader stages alone:
    /// nothing orders the clear after the layout the transition wrote. Synchronization validation
    /// reports it every frame as a write after a write, and whether a driver happens to finish the
    /// transition first is not something to lean on. NGX cannot be asked to record anything else,
    /// and a barrier this renderer records around the evaluation cannot reach between two of NGX's
    /// own commands.
    ///
    /// Only NGX is handed these, so nothing of this renderer's own passes through them.
    /// `RtxDlssTest.aFlatFrameResolvesToItself` evaluates every preset under synchronization
    /// validation and fails on any hazard, which is what says the rule is still enough.
    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngxInstanceProcAddr(VkInstance instance, const char* name);
    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngxDeviceProcAddr(VkDevice device, const char* name);

    /// The rule, for one image barrier: one that moves an image into a layout a transfer may write
    /// in makes the transfer stage's writes wait on it, which is the dependency a clear or a copy
    /// straight after the transition needs. A barrier that keeps its layout, or moves the image into
    /// one no transfer writes, is left as it was.
    ///
    /// @return whether the barrier was widened, which is whether the call it belongs to has to wait
    ///         the transfer stage too.
    bool orderTransferWrites(VkImageMemoryBarrier& barrier);
}
