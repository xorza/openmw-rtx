#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include "loaderlayers.hpp"
#include "owned.hpp"
#include "validation.hpp"

namespace Rtx
{
    struct ValidationOptions;

    /// What a run's validation asks of the layer once it is loaded: at most one finer check with the one setting of
    /// the layer's own it needs, the two finer checks never together, and at most one part of the core checks left
    /// out.
    struct LayerAsk
    {
        std::optional<VkValidationFeatureEnableEXT> mEnabled;

        /// The layer's setting turned on beside `mEnabled`, or null where the level needs none.
        const char* mSetting = nullptr;

        std::optional<VkValidationFeatureDisableEXT> mDisabled;
    };

    /// What `options` asks of the layer: its level's finer check, and its shader checks off where
    /// `ValidationOptions::mShaders` leaves them to another run. Read by an instance that loaded the layer and by
    /// no other.
    LayerAsk askOfLayer(const ValidationOptions& options);

    /// A `VkInstance` and, when validation is on, the messenger and the log behind it.
    class Instance
    {
    public:
        /// @param validation the layers and what an error does, as the renderer was asked. A
        ///        developer feature: nobody enables it in a run they care about the frame rate of.
        /// @param surfaceExtensions what the window's surface needs, or empty for the headless path,
        ///        which is why `openmw-rtxtool` works over ssh.
        /// @param extraExtensions what a feature needs besides, such as NGX's: each enabled where
        ///        the loader offers it and left out where it does not, so `auto` can fall back to
        ///        the other upscaler; the feature then refuses where it is created, naming why.
        Instance(const ValidationOptions& validation, std::span<const char* const> surfaceExtensions,
            std::span<const std::string> extraExtensions = {});

        /// The version the Vulkan loader offers, loading it into the process the first time it is
        /// asked, or nought where the system has none. Nothing loads it before: no program imports it,
        /// so a machine without one starts, and its error box says what is missing.
        static std::uint32_t getLoaderVersion();

        /// The loader's version, where the system has one this renderer runs on, and `Unsupported`
        /// where it has none or an older one.
        static std::uint32_t requireLoader();

        /// Nothing where a driver behind the loader presents to a window, and `Unsupported` where
        /// none does, as a loader with no driver at all.
        static void requireSurface();

        VkInstance getHandle() const { return mHandle.get(); }

        /// Null unless validation was requested and the layer was present. Mutable through a
        /// const instance, because the debug callback writes it from whichever thread made the
        /// offending call.
        ValidationLog* getValidationLog() const { return mValidationLog.get(); }

        /// Whether `name` was loaded: what a device made on this instance reads an option's needs
        /// against, and a surface's `VK_KHR_surface` among them — loaded with a window and never
        /// headless, and what the device's swapchain rests on.
        bool hasExtension(std::string_view name) const;

        /// Whether `VK_EXT_debug_utils` was enabled, which object names and command-buffer labels need:
        /// wherever the loader offers it, in every build and not only under validation.
        bool hasDebugUtils() const { return hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }

        /// The version the loader reported, which is at least `sApiVersion`.
        std::uint32_t getApiVersion() const { return mApiVersion; }

        /// The layers the loader inserted, as it reported them while the instance was made.
        const LoaderLayers& getLayers() const { return *mLayers; }

        /// `ValidationOptions::mWatchSubmits`, which a device made on this instance follows.
        bool watchesSubmits() const { return mWatchesSubmits; }

    private:
        /// Every extension loaded, as names of its own: a surface's are the window library's, which
        /// does not promise its strings outlive the call.
        std::vector<std::string> mExtensions;

        // Held by pointer so the address handed to the debug callback survives everything.
        std::unique_ptr<ValidationLog> mValidationLog;

        /// Held by pointer for the same reason, and before the instance, whose destruction the
        /// loader reports to it as well.
        std::unique_ptr<LoaderLayers> mLayers = std::make_unique<LoaderLayers>();

        bool mWatchesSubmits = false;

        /// Before the messenger made on it, which `Root` says why.
        Root<VkInstance, vkDestroyInstance> mHandle;

        /// The debug messenger, ended before the instance it was made on, through the function
        /// the instance answered for it: a layer's entry points are the instance's own, not the
        /// one load's.
        Handled<VkDebugUtilsMessengerEXT,
            EndedWith<VkInstance, VkDebugUtilsMessengerEXT, PFN_vkDestroyDebugUtilsMessengerEXT>>
            mMessenger;
        std::uint32_t mApiVersion = 0;
    };
}
