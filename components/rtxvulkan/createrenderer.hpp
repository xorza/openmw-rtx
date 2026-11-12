#pragma once

#include <memory>

#include <SDL3/SDL_video.h>

#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/shaderset.hpp>

namespace Rtx
{
    /// What a host makes of this backend: a `Renderer` over Vulkan. Throws `Unsupported`
    /// where this machine cannot run it and `DeviceError` where the device failed, so a host can
    /// tell a machine to skip from a fault to report. Behind this the backend's headers are its
    /// own, and a fact about Vulkan reaches a host through nothing but the seam. A function of
    /// `options` alone: what the environment asks of the layers the host reads where it makes them.
    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options);

    /// This backend's shaders as the game ships them, compiled into the program by the build
    /// (`openmw-rtx-spirv-embed`): without their source, and counting nothing into the census. What a host
    /// hands `RendererOptions::mShaders` where it brings no set of its own.
    const ShaderSet& shippedVulkanShaders();

    /// What a window this renderer presents to is made with: the backend's own surface, and no GL
    /// context anywhere near it. Throws `Unsupported` where this machine has no Vulkan driver to
    /// present to it with.
    SDL_WindowFlags vulkanWindowFlags();
}
