#include "rtxbackend.hpp"

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/renderer/nullrenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtxvulkan/createrenderer.hpp>

namespace MWRender
{
    SDL_WindowFlags surfaceFlagsOf(const RtxBackend backend)
    {
        switch (backend)
        {
            case RtxBackend::Vulkan:
                return Rtx::vulkanWindowFlags();
            case RtxBackend::Null:
                return SDL_WindowFlags{ 0 };
        }
        Crash::fatal("a ray tracing backend past the two");
    }

    const Rtx::ShaderSet& shippedShadersOf(const RtxBackend backend)
    {
        static constexpr Rtx::ShaderSet sNone{};
        switch (backend)
        {
            case RtxBackend::Vulkan:
                return Rtx::shippedVulkanShaders();
            case RtxBackend::Null:
                return sNone;
        }
        Crash::fatal("a ray tracing backend past the two");
    }

    std::unique_ptr<Rtx::Renderer> createBackend(const RtxBackend backend, const Rtx::RendererOptions& options)
    {
        switch (backend)
        {
            case RtxBackend::Vulkan:
                return Rtx::createVulkanRenderer(options);
            case RtxBackend::Null:
                return std::make_unique<Rtx::NullRenderer>(options);
        }
        Crash::fatal("a ray tracing backend past the two");
    }
}
