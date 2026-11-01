#include "createrenderer.hpp"

#include <components/rtxvulkan/device/instance.hpp>

#include "vulkanrenderer.hpp"

namespace Rtx
{
    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options)
    {
        return std::make_unique<VulkanRenderer>(options);
    }

    SDL_WindowFlags vulkanWindowFlags()
    {
        // SDL makes such a window through the same loader and its drivers, and without them its
        // failure reads as any window's: a display that refused, which no host can tell from a
        // machine to skip.
        Instance::requireSurface();
        return SDL_WINDOW_VULKAN;
    }
}
