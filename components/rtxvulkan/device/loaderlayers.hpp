#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>
#include <vulkan/vk_platform.h>

namespace Rtx
{
    /// A layer the loader put between the instance and the driver, and the library it loaded it from.
    struct InsertedLayer
    {
        std::string mName;
        std::string mLibrary;
    };

    /// **The layers the loader inserted into an instance, as the loader itself says.** An overlay's layer
    /// runs inside every call the renderer makes, so a crash report names them beside the driver. No query
    /// answers this: `vkEnumerateInstanceLayerProperties` lists every installed layer, an explicit one
    /// nobody enabled among them, and not which implicit ones loaded. The loader tells a messenger chained
    /// into the instance's create info, at info severity, one "Insert instance layer" message for each
    /// layer it loads (`loader_create_instance_chain`). Pinned, because the loader calls the messenger
    /// again inside `vkDestroyInstance` with the address it was handed at the create.
    class LoaderLayers
    {
    public:
        LoaderLayers() = default;
        LoaderLayers(const LoaderLayers&) = delete;
        LoaderLayers& operator=(const LoaderLayers&) = delete;

        /// What chains into `VkInstanceCreateInfo::pNext`, which `VK_EXT_debug_utils` must be
        /// enabled for.
        VkDebugUtilsMessengerCreateInfoEXT makeMessengerCreateInfo();

        /// Whether the loader said anything at all: its own manifest search reports at the same
        /// severity, so a loader that inserted nothing still reported, and one that never spoke
        /// answers nothing about its layers rather than "none".
        bool isHeard() const { return mHeard; }

        std::span<const InsertedLayer> getInserted() const { return mInserted; }

        /// The log's section: each layer and its library, one a line.
        std::string describe() const;

        /// The names alone, cut at a name to what a crash report's annotation holds, with how
        /// many more the log names.
        std::string summarize() const;

        /// Hands `summarize` to every later crash report. Called at each insertion as well, so a
        /// crash inside a layer's own `vkCreateInstance` names the layers loaded by then.
        void annotate() const;

    private:
        static VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData);

        void hear(std::string_view idName, std::string_view message);

        std::vector<InsertedLayer> mInserted;
        bool mHeard = false;
    };
}
