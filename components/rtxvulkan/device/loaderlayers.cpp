#include "loaderlayers.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include <components/crashcatcher/crash.hpp>

namespace Rtx
{
    namespace
    {
        /// What the loader names every message of its own with, where a layer names its own.
        constexpr std::string_view sLoaderMessage = "Loader Message";

        constexpr std::string_view sInsertion = "Insert instance layer ";

        /// The names of `layers`' first `shown`, and how many more there are.
        std::string join(std::span<const InsertedLayer> layers, const std::size_t shown)
        {
            std::string text;
            for (std::size_t index = 0; index < shown; ++index)
            {
                if (index > 0)
                    text += ", ";
                text += layers[index].mName;
            }

            const std::size_t more = layers.size() - shown;
            if (more > 0 && shown > 0)
                text += std::format(" and {} more", more);
            else if (more > 0)
                text += std::format("{} layer{}", more, more == 1 ? "" : "s");
            return text;
        }
    }

    VkDebugUtilsMessengerCreateInfoEXT LoaderLayers::makeMessengerCreateInfo()
    {
        return VkDebugUtilsMessengerCreateInfoEXT{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            // What the loader sends a message that is neither an error nor a warning at: an
            // insertion, and the manifest search that says the loader is heard.
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
            .pfnUserCallback = &onMessage,
            .pUserData = this,
        };
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL LoaderLayers::onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
    {
        if (data->pMessageIdName != nullptr && data->pMessage != nullptr)
            static_cast<LoaderLayers*>(userData)->hear(data->pMessageIdName, data->pMessage);

        // The spec reserves a true return for the layers' own use; applications must return false.
        return VK_FALSE;
    }

    void LoaderLayers::hear(const std::string_view idName, const std::string_view message)
    {
        if (idName != sLoaderMessage)
            return;
        mHeard = true;
        if (!message.starts_with(sInsertion))
            return;

        // `"VK_LAYER_RTSS" (C:\Program Files (x86)\...\RTSSVkLayer64.dll)`, as every 1.4 loader
        // spells it: the name ends at its closing quote, since a path has parentheses of its own. A
        // spelling a later loader changes is kept whole as the name, which still names the layer.
        const std::string_view rest = message.substr(sInsertion.size());
        constexpr std::string_view closing = "\" (";
        const std::size_t closed = rest.starts_with('"') ? rest.find(closing, 1) : std::string_view::npos;
        if (closed == std::string_view::npos || !rest.ends_with(')'))
            mInserted.push_back(InsertedLayer{ .mName = std::string(rest), .mLibrary = {} });
        else
        {
            const std::size_t library = closed + closing.size();
            mInserted.push_back(InsertedLayer{ .mName = std::string(rest.substr(1, closed - 1)),
                .mLibrary = std::string(rest.substr(library, rest.size() - library - 1)) });
        }
        annotate();
    }

    std::string LoaderLayers::describe() const
    {
        std::string text = "\nlayers the loader inserted\n";
        if (!mHeard)
            return text + "  (the loader reported nothing)\n";
        if (mInserted.empty())
            return text + "  (none)\n";

        std::size_t width = 0;
        for (const InsertedLayer& layer : mInserted)
            width = std::max(width, layer.mName.size());
        for (const InsertedLayer& layer : mInserted)
            text += std::format("  {:<{}}  {}\n", layer.mName, width, layer.mLibrary);
        return text;
    }

    std::string LoaderLayers::summarize() const
    {
        if (!mHeard)
            return "not reported by the loader";
        if (mInserted.empty())
            return "none";

        for (std::size_t shown = mInserted.size();; --shown)
            if (std::string text = join(mInserted, shown); text.size() <= Crash::sAnnotationLength || shown == 0)
                return text;
    }

    void LoaderLayers::annotate() const
    {
        Crash::annotate("layers", summarize());
    }
}
