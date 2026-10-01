#include "instance.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "requirements.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        constexpr const char* sValidationLayer = "VK_LAYER_KHRONOS_validation";

        bool hasLayer(const char* name)
        {
            const std::vector<VkLayerProperties> layers = enumerateVk<VkLayerProperties>(
                "vkEnumerateInstanceLayerProperties", [](std::uint32_t* count, VkLayerProperties* into) {
                    return vkEnumerateInstanceLayerProperties(count, into);
                });

            return std::any_of(layers.begin(), layers.end(),
                [&](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
        }

        bool loaderOffers(const char* name)
        {
            const std::vector<VkExtensionProperties> extensions = enumerateVk<VkExtensionProperties>(
                "vkEnumerateInstanceExtensionProperties", [](std::uint32_t* count, VkExtensionProperties* into) {
                    return vkEnumerateInstanceExtensionProperties(nullptr, count, into);
                });

            return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }
    }

    Instance::Instance(const ValidationOptions& options, const std::span<const char* const> surfaceExtensions)
    {
        const Crash::NoteScope noted("making the Vulkan instance");
        checkVk(vkEnumerateInstanceVersion(&mApiVersion), "vkEnumerateInstanceVersion");
        if (mApiVersion < sApiVersion)
            throw Unsupported("the Vulkan loader offers " + versionString(mApiVersion) + ", and this renderer is written "
                "against " + versionString(sApiVersion));

        std::vector<const char*> extensions(surfaceExtensions.begin(), surfaceExtensions.end());
        std::vector<const char*> layers;

        // What the device half of swapchain maintenance rests on: a present fence is the only
        // thing that says the presentation engine has finished with an image. Surface maintenance
        // rests in turn on the extended surface query, and both are taken where the loader has
        // both, so a driver without them presents as before.
        if (!surfaceExtensions.empty() && loaderOffers(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
            && loaderOffers(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME))
        {
            extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            extensions.push_back(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }

        // Asked for wherever the loader offers it, which is every build: command-buffer labels are
        // what make a profile readable, and the release build is the one a profiler reads. Object
        // names stay this build's own (`Device::setName`).
        const bool debugUtils = loaderOffers(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        // Validation reaches us only through the messenger, so without the extension it would run
        // and report nothing — worse than not running at all, because the clean output would read
        // as a pass.
        const bool wanted = options.mLevel != ValidationLevel::Off;
        const bool validation = wanted && debugUtils && hasLayer(sValidationLayer);
        if (wanted && !validation)
        {
            const std::string missing = std::string(sValidationLayer) + " or " + VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

            // A run that asked for validation and cannot have it fails rather than reporting
            // nothing, or a gate reads an empty `takeValidationErrors` as a clean pass. A build
            // that merely switched the layers on by default still warns.
            if (options.mDemanded)
                throw Unsupported("Vulkan validation was asked for and " + missing + " is missing");

            Log(Debug::Warning) << "Vulkan validation was requested but " << missing << " is missing.";
        }

        if (debugUtils)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        if (validation)
        {
            mValidationLog = std::make_unique<ValidationLog>(options.mAbortOnError);
            layers.push_back(sValidationLayer);
        }

        const VkApplicationInfo application{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
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

        const VkBool32 on = VK_TRUE;

        // What the layer will not turn on by itself, gathered as each validation below asks for it.
        std::vector<VkLayerSettingEXT> settings;

        const auto turnOn = [&](const char* name) {
            settings.push_back(VkLayerSettingEXT{
                .pLayerName = sValidationLayer,
                .pSettingName = name,
                .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
                .valueCount = 1,
                .pValues = &on,
            });
        };

        VkLayerSettingsCreateInfoEXT layerSettings{};

        const void* next = nullptr;

        if (validation)
        {
            messengerInfo = makeMessengerCreateInfo(*mValidationLog);
            next = &messengerInfo;

            // **Synchronization validation and GPU-assisted validation are never loaded together**:
            // the two took the device down in three runs of four. The first catches a missing
            // barrier and costs enough to be opt-in. The second instruments every shader and catches
            // what a ray query does with its own arguments, at about half the frame rate, and the
            // layer itself asks not to be run beside the core checks, so neither is a default.
            if (options.mLevel == ValidationLevel::Sync)
            {
                enabled.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);

                // Without this, synchronization validation cannot see a compute shader's accesses at
                // all: with the cascade's barriers taken out, five runs of a doll write five
                // different pictures and the layer reports nothing until this is set.
                turnOn("syncval_shader_accesses_heuristic");
            }

            if (options.mLevel == ValidationLevel::Gpu)
            {
                enabled.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);

                // Hands reads past the end of a buffer to the hardware's own robust buffer access
                // rather than GPU-AV's instrumentation, which the layer itself warns is very slow
                // with this many storage buffers — a window under it loses the device inside half a
                // minute. What is given up is the report; what is kept is everything else GPU-AV
                // checks, including a ray query's own arguments. The scene's tables are pointers,
                // which robustness does not reach, so a shot still takes twice as long under the
                // layers.
                turnOn("gpuav_force_on_robustness");
            }

            if (!enabled.empty())
            {
                validationFeatures = VkValidationFeaturesEXT{
                    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                    .pNext = &messengerInfo,
                    .enabledValidationFeatureCount = static_cast<std::uint32_t>(enabled.size()),
                    .pEnabledValidationFeatures = enabled.data(),
                    .disabledValidationFeatureCount = 0,
                    .pDisabledValidationFeatures = nullptr,
                };
                next = &validationFeatures;
            }

            if (!settings.empty())
            {
                layerSettings = VkLayerSettingsCreateInfoEXT{
                    .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
                    .pNext = next,
                    .settingCount = static_cast<std::uint32_t>(settings.size()),
                    .pSettings = settings.data(),
                };
                next = &layerSettings;
            }
        }

        const VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = next,
            .flags = 0,
            .pApplicationInfo = &application,
            .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };

        checkVkSupport(vkCreateInstance(&createInfo, nullptr, &mHandle), "vkCreateInstance");
        mExtensions.assign(extensions.begin(), extensions.end());

        // A constructor that throws runs no destructor, and this throw is caught and reported
        // rather than ending the process, so anything after a successful create cleans up before it
        // rethrows.
        try
        {
            if (validation)
            {
                const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(mHandle, "vkCreateDebugUtilsMessengerEXT"));
                mDestroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(mHandle, "vkDestroyDebugUtilsMessengerEXT"));
                if (create == nullptr || mDestroyMessenger == nullptr)
                    throw Unsupported(
                        "the validation layer is loaded but the debug messenger's entry points are missing");

                checkVk(create(mHandle, &messengerInfo, nullptr, &mMessenger), "vkCreateDebugUtilsMessengerEXT");
            }
        }
        catch (...)
        {
            vkDestroyInstance(mHandle, nullptr);
            mHandle = VK_NULL_HANDLE;
            throw;
        }
    }

    bool Instance::hasExtension(const std::string_view name) const
    {
        return std::find(mExtensions.begin(), mExtensions.end(), name) != mExtensions.end();
    }

    Instance::~Instance()
    {
        if (mMessenger != VK_NULL_HANDLE)
            mDestroyMessenger(mHandle, mMessenger, nullptr);

        if (mHandle != VK_NULL_HANDLE)
            vkDestroyInstance(mHandle, nullptr);
    }
}
