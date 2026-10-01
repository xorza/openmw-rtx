#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

struct SDL_Window;

namespace Rtx
{
    class Instance;

    /// A window's `VkSurfaceKHR`, and the questions a swapchain asks of it. The instance must
    /// outlive it.
    class Surface
    {
    public:
        /// What SDL says an instance needs before a window can have a surface: the instance has to
        /// be made with these before a surface can be made on it.
        static std::vector<const char*> getInstanceExtensions();

        /// Throws `Unsupported` where SDL will not make one for `window`.
        Surface(const Instance& instance, SDL_Window* window);
        ~Surface();

        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;

        VkSurfaceKHR getHandle() const { return mHandle; }

        /// Whether `queueFamily` of `device` can present to this surface.
        bool supports(VkPhysicalDevice device, std::uint32_t queueFamily) const;

        VkSurfaceCapabilitiesKHR getCapabilities(VkPhysicalDevice device) const;
        std::vector<VkSurfaceFormatKHR> getFormats(VkPhysicalDevice device) const;
        std::vector<VkPresentModeKHR> getPresentModes(VkPhysicalDevice device) const;

    private:
        VkInstance mInstance = VK_NULL_HANDLE;
        VkSurfaceKHR mHandle = VK_NULL_HANDLE;

        /// `VK_KHR_surface`'s functions, asked of the instance and not taken from volk: the loader
        /// answers them only to an instance that enabled the extension, where a core function is
        /// the same for every instance (`Instance`'s constructor says why).
        PFN_vkDestroySurfaceKHR mDestroySurface = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR mGetSupport = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR mGetCapabilities = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR mGetFormats = nullptr;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR mGetPresentModes = nullptr;
    };
}
