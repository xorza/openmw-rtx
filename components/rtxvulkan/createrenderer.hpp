#pragma once

#include <memory>

#include <SDL3/SDL_video.h>

#include <components/rtx/renderer/renderer.hpp>

namespace Rtx
{
    /// The one thing a host needs from this backend: a `Renderer` over Vulkan. Throws `Unsupported`
    /// where this machine cannot run it and `DeviceError` where the device failed, so a host can
    /// tell a machine to skip from a fault to report. Behind this the backend's headers are its
    /// own, and a fact about Vulkan reaches a host through nothing but the seam.
    ///
    /// **The validation layers' finer levels, asked for by the environment**: `OPENMW_RTX_SYNC_VALIDATION`
    /// and `OPENMW_RTX_GPU_VALIDATION` raise `options`' level whatever the build or the host said,
    /// which `askedLevel` says the why of.
    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options);

    /// What a window this renderer presents to is made with: the backend's own surface, and no GL
    /// context anywhere near it.
    SDL_WindowFlags vulkanWindowFlags();
}
