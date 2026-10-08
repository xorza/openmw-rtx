#include "createrenderer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "vulkanrenderer.hpp"

namespace Rtx
{
    namespace
    {
        /// Whether an environment variable is set to anything other than nothing or `0`.
        bool askedFor(const char* name)
        {
            const char* const value = std::getenv(name);
            return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
        }

        /// `level` raised to what the environment asks of the layers.
        ///
        /// **The two finer levels, asked for by name and never on by themselves.** The build decides
        /// whether the layers load; these decide what they check, and each costs far more than the
        /// core checks do — synchronization validation tracks every access of every resource, and
        /// the GPU-assisted layer instruments every shader. They are here because the harness names
        /// a level on its command line and the game has none, and `Rtx::sValidationByDefault` says
        /// why two hosts of one renderer must not disagree about the layers. What they answer is
        /// the fault a core-clean run still ends in: a device lost with an address and nothing else.
        ///
        /// **The GPU-assisted layer takes the process down on its own**, which is why it is a level
        /// of its own and never paired with the other: over a window `vkWaitForFences` comes back
        /// `VK_ERROR_DEVICE_LOST` on three runs of four, somewhere inside a minute, with nothing
        /// wrong in the frame, and headless it has aborted inside the layer's own thread. So
        /// `OPENMW_RTX_SYNC_VALIDATION` is the one to reach for in the game, and
        /// `OPENMW_RTX_GPU_VALIDATION` is there for a session willing to tell the losses apart.
        /// Either raises the level whatever the build said, which is what lets a Release build be
        /// asked one question without being rebuilt.
        ValidationLevel askedLevel(ValidationLevel level)
        {
            if (askedFor("OPENMW_RTX_SYNC_VALIDATION"))
                level = std::max(level, ValidationLevel::Sync);
            if (askedFor("OPENMW_RTX_GPU_VALIDATION"))
                level = ValidationLevel::Gpu;
            return level;
        }
    }

    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options)
    {
        RendererOptions asked = options;
        asked.mRun.mValidation.mLevel = askedLevel(options.mRun.mValidation.mLevel);
        return std::make_unique<VulkanRenderer>(asked);
    }

    SDL_WindowFlags vulkanWindowFlags()
    {
        return SDL_WINDOW_VULKAN;
    }
}
