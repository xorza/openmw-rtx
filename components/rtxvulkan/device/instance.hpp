#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volk.h>

#include "owned.hpp"
#include "validation.hpp"

namespace Rtx
{
    struct ValidationOptions;

    /// A `VkInstance` and, when validation is on, the messenger and the log behind it.
    class Instance
    {
    public:
        /// @param validation the layers and what an error does, as the renderer was asked. A
        ///        developer feature: nobody enables it in a run they care about the frame rate of.
        /// @param surfaceExtensions what the window's surface needs, or empty for the headless path,
        ///        which is why `openmw-rtxtool` works over ssh.
        Instance(const ValidationOptions& validation, std::span<const char* const> surfaceExtensions);

        /// The version the Vulkan loader offers, loading it into the process the first time it is
        /// asked, or nought where the system has none. Nothing loads it before: no program imports it,
        /// so a machine without one runs the OpenGL renderer.
        static std::uint32_t getLoaderVersion();

        VkInstance getHandle() const { return mHandle.get(); }

        /// Null unless validation was requested and the layer was present. Mutable through a
        /// const instance, because the debug callback writes it from whichever thread made the
        /// offending call.
        ValidationLog* getValidationLog() const { return mValidationLog.get(); }

        /// Whether `name` was loaded: what a device made on this instance reads an option's needs
        /// against, and a surface's `VK_KHR_surface` among them — loaded with a window and never
        /// headless, and what the device's swapchain rests on.
        bool hasExtension(std::string_view name) const;

        /// Whether `VK_EXT_debug_utils` was enabled, which is what object names and command-buffer
        /// labels need. True whenever this build names objects, not only under validation — a
        /// capture is worth having without paying for the layers.
        bool hasDebugUtils() const { return hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

        /// The version the loader reported, which is at least `sApiVersion`.
        std::uint32_t getApiVersion() const { return mApiVersion; }

    private:
        /// Every extension loaded, as names of its own: a surface's are the window library's, which
        /// does not promise its strings outlive the call.
        std::vector<std::string> mExtensions;

        // Held by pointer so the address handed to the debug callback survives everything.
        std::unique_ptr<ValidationLog> mValidationLog;

        /// Before the messenger made on it, which `Root` says why.
        Root<VkInstance, vkDestroyInstance> mHandle;

        /// The debug messenger, ended before the instance it was made on.
        class Messenger
        {
        public:
            Messenger() = default;

            /// Takes `handle`, made on `instance` and checked, to end with `destroy`: resolved with
            /// the create half, once, so the end does not ask the loader again.
            Messenger(VkInstance instance, PFN_vkDestroyDebugUtilsMessengerEXT destroy, VkDebugUtilsMessengerEXT handle)
                : mInstance(instance)
                , mDestroyMessenger(destroy)
                , mHandle(handle)
            {
            }

            ~Messenger() { end(); }

            Messenger(const Messenger&) = delete;
            Messenger& operator=(const Messenger&) = delete;

            Messenger& operator=(Messenger&& other) noexcept
            {
                if (this != &other)
                {
                    end();
                    mInstance = other.mInstance;
                    mDestroyMessenger = other.mDestroyMessenger;
                    mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
                }
                return *this;
            }

        private:
            void end()
            {
                if (mHandle != VK_NULL_HANDLE)
                    mDestroyMessenger(mInstance, mHandle, nullptr);
                mHandle = VK_NULL_HANDLE;
            }

            VkInstance mInstance = VK_NULL_HANDLE;
            PFN_vkDestroyDebugUtilsMessengerEXT mDestroyMessenger = nullptr;
            VkDebugUtilsMessengerEXT mHandle = VK_NULL_HANDLE;
        };

        Messenger mMessenger;
        std::uint32_t mApiVersion = 0;
    };
}
