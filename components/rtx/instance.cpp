#include "instance.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include <components/debug/debuglog.hpp>

#include "error.hpp"
#include "requirements.hpp"

namespace Rtx
{
    namespace
    {
        constexpr const char* sValidationLayer = "VK_LAYER_KHRONOS_validation";

        bool hasLayer(const char* name)
        {
            std::uint32_t count = 0;
            checkVk(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties");
            std::vector<VkLayerProperties> layers(count);
            checkVk(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");

            return std::any_of(layers.begin(), layers.end(),
                [&](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
        }

        std::string versionString(std::uint32_t version)
        {
            return std::to_string(VK_API_VERSION_MAJOR(version)) + '.' + std::to_string(VK_API_VERSION_MINOR(version))
                + '.' + std::to_string(VK_API_VERSION_PATCH(version));
        }
    }

    Instance::Instance(const InstanceOptions& options)
    {
        checkVk(vkEnumerateInstanceVersion(&mApiVersion), "vkEnumerateInstanceVersion");
        if (mApiVersion < sApiVersion)
            throw Error("the Vulkan loader offers " + versionString(mApiVersion) + ", and this renderer is written "
                "against " + versionString(sApiVersion));

        std::vector<const char*> extensions(options.mSurfaceExtensions);
        std::vector<const char*> layers;

        const bool validation = options.mValidation && hasLayer(sValidationLayer);
        if (options.mValidation && !validation)
            Log(Debug::Warning) << "Vulkan validation was requested but " << sValidationLayer << " is not installed.";

        if (validation)
        {
            mValidationLog = std::make_unique<ValidationLog>(options.mPolicy);
            layers.push_back(sValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        const VkApplicationInfo application{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "OpenMW",
            .applicationVersion = 0,
            .pEngineName = "OpenMW RTX",
            .engineVersion = 0,
            .apiVersion = sApiVersion,
        };

        // Chained into the create info so errors raised by vkCreateInstance and vkDestroyInstance
        // themselves are reported; the standalone messenger below covers everything in between.
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        std::vector<VkValidationFeatureEnableEXT> enabled;
        VkValidationFeaturesEXT validationFeatures{};
        const void* next = nullptr;

        if (validation)
        {
            messengerInfo = makeMessengerCreateInfo(*mValidationLog);
            next = &messengerInfo;

            if (options.mSynchronizationValidation)
                enabled.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
            if (options.mGpuAssistedValidation)
                enabled.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);

            if (!enabled.empty())
            {
                validationFeatures = VkValidationFeaturesEXT{
                    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                    .pNext = &messengerInfo,
                    .enabledValidationFeatureCount = static_cast<std::uint32_t>(enabled.size()),
                    .pEnabledValidationFeatures = enabled.data(),
                };
                next = &validationFeatures;
            }
        }

        const VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = next,
            .pApplicationInfo = &application,
            .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };

        checkVk(vkCreateInstance(&createInfo, nullptr, &mHandle), "vkCreateInstance");

        if (validation)
        {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(mHandle, "vkCreateDebugUtilsMessengerEXT"));
            if (create == nullptr)
                throw Error("the validation layer is loaded but vkCreateDebugUtilsMessengerEXT is missing");

            checkVk(create(mHandle, &messengerInfo, nullptr, &mMessenger), "vkCreateDebugUtilsMessengerEXT");
        }
    }

    Instance::~Instance()
    {
        if (mMessenger != VK_NULL_HANDLE)
        {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(mHandle, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy != nullptr)
                destroy(mHandle, mMessenger, nullptr);
        }

        if (mHandle != VK_NULL_HANDLE)
            vkDestroyInstance(mHandle, nullptr);
    }
}
