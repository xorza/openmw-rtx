#include <components/rtx/renderer.hpp>

#ifdef OPENMW_RTX_VULKAN
#include <components/rtxvulkan/vulkanrenderer.hpp>
#endif

namespace Rtx
{
    namespace
    {
        /// What `Backend::Default` means here.
        ///
        /// Whichever backend this build has, and where it has both, the one the platform runs
        /// natively — Metal is not a portability layer over Vulkan and choosing it on Apple hardware
        /// is not a fallback.
        constexpr Backend sDefault =
#if defined(OPENMW_RTX_METAL) && defined(__APPLE__)
            Backend::Metal;
#elif defined(OPENMW_RTX_VULKAN)
            Backend::Vulkan;
#else
            // Neither built: the request is answered below by saying so, rather than by naming a
            // backend nobody asked for.
            Backend::Default;
#endif

        const char* backendName(Backend backend)
        {
            switch (backend)
            {
                case Backend::Vulkan:
                    return "Vulkan";
                case Backend::Metal:
                    return "Metal";
                case Backend::Default:
                    break;
            }

            return "default";
        }
    }

    std::unique_ptr<Renderer> createRenderer(const RendererOptions& options, std::string& reason)
    {
        const Backend backend = options.mBackend == Backend::Default ? sDefault : options.mBackend;

#ifdef OPENMW_RTX_VULKAN
        if (backend == Backend::Vulkan)
            return createVulkanRenderer(options, reason);
#endif

        reason = backend == Backend::Default ? std::string("this build has no ray tracing backend")
                                             : std::string("this build has no ") + backendName(backend) + " backend";
        return nullptr;
    }
}
