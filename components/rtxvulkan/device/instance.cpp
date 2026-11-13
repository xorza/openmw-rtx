#include "instance.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "requirements.hpp"
#include "result.hpp"

// The one translation unit that holds volk's body: the loader is loaded here and nowhere else.
#define VOLK_IMPLEMENTATION
#include <volk.h>

namespace Rtx
{
    namespace
    {
        constexpr const char* sValidationLayer = "VK_LAYER_KHRONOS_validation";

        /// Loads the loader into the process, once, and says whether there was one to load.
        bool loadLoader()
        {
            static const bool loaded = volkInitialize() == VK_SUCCESS;
            return loaded;
        }

        bool hasLayer(const char* name)
        {
            const std::vector<VkLayerProperties> layers = enumerateVk<VkLayerProperties>(
                "vkEnumerateInstanceLayerProperties", [](std::uint32_t* count, VkLayerProperties* into) {
                    return vkEnumerateInstanceLayerProperties(count, into);
                });

            return std::any_of(layers.begin(), layers.end(),
                [&](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
        }

        std::vector<VkExtensionProperties> loaderExtensions()
        {
            return enumerateVk<VkExtensionProperties>(
                "vkEnumerateInstanceExtensionProperties", [](std::uint32_t* count, VkExtensionProperties* into) {
                    return vkEnumerateInstanceExtensionProperties(nullptr, count, into);
                });
        }

        bool offers(std::span<const VkExtensionProperties> extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }
    }

    LayerAsk askOfLayer(const ValidationOptions& options)
    {
        LayerAsk ask;

        // **Synchronization validation and GPU-assisted validation are never loaded together**: the two
        // took the device down in three runs of four. The first catches a missing barrier and is opt-in
        // for its cost; the second instruments every shader and catches what a ray query does with its
        // own arguments at about half the frame rate, and the layer asks not to be run beside the core
        // checks, so neither is a default.
        switch (options.mLevel)
        {
            case ValidationLevel::Off:
            case ValidationLevel::On:
                break;
            case ValidationLevel::Sync:
                ask.mEnabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
                // Without this, synchronization validation cannot see a compute shader's accesses at
                // all: with the cascade's barriers taken out, five runs of a doll write five
                // different pictures and the layer reports nothing until this is set.
                ask.mSetting = "syncval_shader_accesses_heuristic";
                break;
            case ValidationLevel::Gpu:
                ask.mEnabled = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
                // Hands reads past the end of a buffer to the hardware's robust buffer access rather than
                // GPU-AV's instrumentation, which the layer itself warns is very slow with this many storage
                // buffers — a window under it loses the device inside half a minute. The report is given up;
                // everything else GPU-AV checks, a ray query's own arguments included, is kept. The scene's
                // tables are pointers, which robustness does not reach, so a shot still takes twice as long.
                ask.mSetting = "gpuav_force_on_robustness";
                break;
        }

        // **The layer's shader checks**: `spirv-val` over each module with a pipeline's specialization constants put
        // in, and the module against the pipeline. The layer keeps what it checked by a module's code alone, so every
        // tuple of a kernel is checked again at every start, which is what a caller that leaves them out is spared.
        if (options.mShaders == ShaderValidation::Skipped)
            ask.mDisabled = VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT;

        return ask;
    }

    Instance::Instance(const ValidationOptions& options, const std::span<const char* const> surfaceExtensions,
        const std::span<const std::string> extraExtensions)
        : mWatchesSubmits(options.mWatchSubmits)
    {
        const Crash::NoteScope noted("making the Vulkan instance");
        mApiVersion = requireLoader();

        std::vector<const char*> extensions(surfaceExtensions.begin(), surfaceExtensions.end());
        std::vector<const char*> layers;
        const std::vector<VkExtensionProperties> offered = loaderExtensions();

        // A feature's extension is enabled where the loader offers it. One it does not is left
        // out, so `auto` can fall back to the other upscaler instead of failing the instance; an
        // explicit choice then fails where the feature is created, naming its own reason.
        for (const std::string& name : extraExtensions)
            if (offers(offered, name.c_str())
                && std::none_of(extensions.begin(), extensions.end(),
                    [&](const char* held) { return std::strcmp(held, name.c_str()) == 0; }))
                extensions.push_back(name.c_str());

        // What the device half of swapchain maintenance rests on: a present fence is the only
        // thing that says the presentation engine has finished with an image. Surface maintenance
        // rests in turn on the extended surface query, and both are taken where the loader has
        // both, so a driver without them presents as before: **in the promoted spelling where the
        // loader has it, and in the one before its promotion where it has that alone**, as DXVK
        // takes either.
        if (!surfaceExtensions.empty() && offers(offered, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
        {
            const char* const maintenance = offers(offered, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME)
                ? VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME
                : offers(offered, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)
                ? VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME
                : nullptr;
            if (maintenance != nullptr)
            {
                extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
                extensions.push_back(maintenance);
            }
        }

        // Asked for wherever the loader offers it, which is every build: command-buffer labels are
        // what make a profile readable, and the release build is the one a profiler reads. Object
        // names stay this build's own (`Device::setName`).
        const bool debugUtils = offers(offered, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        // Validation reaches us only through the messenger, so without the extension it would run
        // and report nothing — worse than not running at all, because the clean output would read
        // as a pass.
        const bool wanted = options.mLevel != ValidationLevel::Off;
        const bool validation = wanted && debugUtils && hasLayer(sValidationLayer);
        if (wanted && !validation)
        {
            const std::string missing = std::string(sValidationLayer) + " or " + VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

            // A run that asked for validation and cannot have it fails rather than reporting
            // nothing, or a gate reads an empty validation log as a clean pass. A build
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
        VkValidationFeaturesEXT validationFeatures{};

        const LayerAsk ask = askOfLayer(options);
        const VkBool32 on = VK_TRUE;
        const VkLayerSettingEXT setting{
            .pLayerName = sValidationLayer,
            .pSettingName = ask.mSetting,
            .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
            .valueCount = 1,
            .pValues = &on,
        };
        VkLayerSettingsCreateInfoEXT layerSettings{};

        const void* next = nullptr;

        if (validation)
        {
            messengerInfo = makeMessengerCreateInfo(*mValidationLog);
            next = &messengerInfo;

            if (ask.mEnabled.has_value() || ask.mDisabled.has_value())
            {
                validationFeatures = VkValidationFeaturesEXT{
                    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                    .pNext = &messengerInfo,
                    .enabledValidationFeatureCount = ask.mEnabled.has_value() ? 1u : 0u,
                    .pEnabledValidationFeatures = ask.mEnabled.has_value() ? &*ask.mEnabled : nullptr,
                    .disabledValidationFeatureCount = ask.mDisabled.has_value() ? 1u : 0u,
                    .pDisabledValidationFeatures = ask.mDisabled.has_value() ? &*ask.mDisabled : nullptr,
                };
                next = &validationFeatures;
            }

            if (ask.mSetting != nullptr)
            {
                layerSettings = VkLayerSettingsCreateInfoEXT{
                    .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
                    .pNext = next,
                    .settingCount = 1,
                    .pSettings = &setting,
                };
                next = &layerSettings;
            }
        }

        // At the head of the chain, ahead of the validation's messenger: the loader calls every
        // messenger the chain holds.
        VkDebugUtilsMessengerCreateInfoEXT layersInfo{};
        if (debugUtils)
        {
            layersInfo = mLayers->makeMessengerCreateInfo();
            layersInfo.pNext = next;
            next = &layersInfo;
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

        // Into a local and adopted on success, as `Owned::make` does. From the adoption on, a throw
        // ends the instance with the member that holds it.
        VkInstance created = VK_NULL_HANDLE;
        checkVkSupport(vkCreateInstance(&createInfo, nullptr, &created), "vkCreateInstance");
        mHandle = Root<VkInstance, vkDestroyInstance>(created);
        mExtensions.assign(extensions.begin(), extensions.end());
        mLayers->annotate();

        // **One load serves every instance**, so a second one made beside the first, as the tests make
        // them, rewrites nothing another thread is calling through. For a core function and a swapchain
        // one the loader answers with its own exported trampoline whatever the instance
        // (`trampoline_get_proc_addr`, `wsi_swapchain_instance_gpa`); the surface's are not, and `Surface`
        // asks this instance for its own.
        static std::once_flag loaded;
        std::call_once(loaded, [&] { volkLoadInstance(mHandle.get()); });

        // What the one load stands on, held of every instance rather than trusted: a loader
        // that answered this one differently would be calling another's functions through it.
        const std::pair<const char*, PFN_vkVoidFunction> sameForEvery[] = {
            { "vkEnumeratePhysicalDevices", reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDevices) },
            { "vkQueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit2) },
            { "vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR) },
        };
        for (const auto& [name, held] : sameForEvery)
            if (vkGetInstanceProcAddr(mHandle.get(), name) != held)
                throw Unsupported(std::string("the Vulkan loader answers ") + name
                    + " differently for each instance, which this renderer does not support");

        if (validation)
        {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(mHandle.get(), "vkCreateDebugUtilsMessengerEXT"));
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(mHandle.get(), "vkDestroyDebugUtilsMessengerEXT"));
            if (create == nullptr || destroy == nullptr)
                throw Unsupported("the validation layer is loaded but the debug messenger's entry points are missing");

            VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
            checkVkWithoutDevice(
                create(mHandle.get(), &messengerInfo, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
            mMessenger = decltype(mMessenger)({ mHandle.get(), destroy }, messenger);
        }
    }

    std::uint32_t Instance::getLoaderVersion()
    {
        return loadLoader() ? volkGetInstanceVersion() : 0;
    }

    std::uint32_t Instance::requireLoader()
    {
        const std::uint32_t version = getLoaderVersion();
        if (version == 0)
            throw Unsupported("there is no Vulkan loader on this system: the graphics driver installs it");
        if (version < sApiVersion)
            throw Unsupported("the Vulkan loader offers " + versionString(version) + ", and this renderer is written "
                "against " + versionString(sApiVersion));
        return version;
    }

    void Instance::requireSurface()
    {
        requireLoader();
        if (!offers(loaderExtensions(), VK_KHR_SURFACE_EXTENSION_NAME))
            throw Unsupported("no Vulkan driver is installed that presents to a window");
    }

    bool Instance::hasExtension(const std::string_view name) const
    {
        return std::find(mExtensions.begin(), mExtensions.end(), name) != mExtensions.end();
    }
}
