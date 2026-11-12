#include "surface.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_vulkan.h>
#include <volk.h>

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/result.hpp>

namespace Rtx
{
    namespace
    {
        template <class Function>
        void load(const VkInstance instance, Function& out, const char* name)
        {
            out = reinterpret_cast<Function>(vkGetInstanceProcAddr(instance, name));
            if (out == nullptr)
                throw Unsupported(std::string("the Vulkan loader enabled a surface and has no ") + name);
        }
    }

    std::vector<const char*> Surface::getInstanceExtensions()
    {
        Uint32 count = 0;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr)
            throw Unsupported(
                std::string("SDL would not name the instance extensions a surface needs: ") + SDL_GetError());

        return std::vector<const char*>(names, names + count);
    }

    Surface::Surface(const Instance& instance, SDL_Window* const window)
        : mInstance(instance.getHandle())
        , mWindow(window)
    {
        assert(instance.hasExtension(VK_KHR_SURFACE_EXTENSION_NAME) && "a surface on an instance made headless");

        load(mInstance, mDestroySurface, "vkDestroySurfaceKHR");
        load(mInstance, mGetSupport, "vkGetPhysicalDeviceSurfaceSupportKHR");
        load(mInstance, mGetCapabilities, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        load(mInstance, mGetFormats, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        load(mInstance, mGetPresentModes, "vkGetPhysicalDeviceSurfacePresentModesKHR");

        if (!SDL_Vulkan_CreateSurface(window, mInstance, nullptr, &mHandle))
            throw Unsupported(std::string("SDL would not make a Vulkan surface: ") + SDL_GetError());
    }

    Surface::~Surface()
    {
        mDestroySurface(mInstance, mHandle, nullptr);
    }

    void Surface::remake()
    {
        mDestroySurface(mInstance, std::exchange(mHandle, VK_NULL_HANDLE), nullptr);
        if (!SDL_Vulkan_CreateSurface(mWindow, mInstance, nullptr, &mHandle))
            deviceFailed(std::string("SDL would not make the window's surface again: ") + SDL_GetError());
    }

    namespace
    {
        /// `checkVkWithoutDevice`, but for a lost surface, which it notes in `lost` and does not raise.
        struct SurfaceCheck
        {
            bool& mLost;

            void operator()(const VkResult result, const char* const call) const
            {
                if (result == VK_ERROR_SURFACE_LOST_KHR)
                    mLost = true;
                else
                    checkVkWithoutDevice(result, call);
            }
        };
    }

    std::optional<bool> Surface::supports(const VkPhysicalDevice device, const std::uint32_t queueFamily) const
    {
        VkBool32 supported = VK_FALSE;
        bool lost = false;
        SurfaceCheck{ lost }(
            mGetSupport(device, queueFamily, mHandle, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (lost)
            return std::nullopt;
        return supported == VK_TRUE;
    }

    std::optional<VkSurfaceCapabilitiesKHR> Surface::getCapabilities(const VkPhysicalDevice device) const
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        const VkResult result = mGetCapabilities(device, mHandle, &capabilities);
        if (result == VK_ERROR_SURFACE_LOST_KHR)
            return std::nullopt;
        checkVkWithoutDevice(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        return capabilities;
    }

    std::optional<std::vector<VkSurfaceFormatKHR>> Surface::getFormats(const VkPhysicalDevice device) const
    {
        bool lost = false;
        std::vector<VkSurfaceFormatKHR> formats = enumerateChecked(
            SurfaceCheck{ lost }, "vkGetPhysicalDeviceSurfaceFormatsKHR",
            [&](std::uint32_t* count, VkSurfaceFormatKHR* into) { return mGetFormats(device, mHandle, count, into); },
            VkSurfaceFormatKHR{});
        if (lost)
            return std::nullopt;
        return formats;
    }

    std::optional<std::vector<VkPresentModeKHR>> Surface::getPresentModes(const VkPhysicalDevice device) const
    {
        bool lost = false;
        std::vector<VkPresentModeKHR> modes = enumerateChecked(
            SurfaceCheck{ lost }, "vkGetPhysicalDeviceSurfacePresentModesKHR",
            [&](std::uint32_t* count, VkPresentModeKHR* into) {
                return mGetPresentModes(device, mHandle, count, into);
            },
            VkPresentModeKHR{});
        if (lost)
            return std::nullopt;
        return modes;
    }
}
