#include "dlssapplication.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <components/debug/debuglog.hpp>
#include <components/version/version.hpp>

namespace Rtx::Dlss
{
    namespace
    {
        /// NGX's messages, into the game's log at the debug level, where a player who asks for
        /// them finds them: why a feature was refused, which library loaded, what the driver lacks.
        void logNgx(const char* message, Ngx::LoggingLevel, Ngx::Feature)
        {
            std::string_view text(message);
            while (text.ends_with('\n'))
                text.remove_suffix(1);
            Log(Debug::Debug) << "NGX: " << text;
        }

        /// The names of the extensions NGX listed, or none where it failed or listed none.
        std::vector<std::string> namesOf(
            const Ngx::Result result, const std::uint32_t count, const VkExtensionProperties* properties)
        {
            if (Ngx::failed(result) || properties == nullptr)
                return {};

            std::vector<std::string> names;
            names.reserve(count);
            for (const VkExtensionProperties& extension : std::span(properties, count))
                names.emplace_back(extension.extensionName);
            return names;
        }

        /// The extensions every NGX feature needs, as the core lists them for a caller that names no
        /// feature (`NVSDK_NGX_VULKAN_RequiredExtensions`).
        struct CoreExtensions
        {
            std::vector<std::string> mInstance;
            std::vector<std::string> mDevice;
        };

        CoreExtensions coreExtensions(const Ngx::Api& ngx)
        {
            unsigned int instanceCount = 0;
            const char** instance = nullptr;
            unsigned int deviceCount = 0;
            const char** device = nullptr;
            if (Ngx::failed(ngx.RequiredExtensions(&instanceCount, &instance, &deviceCount, &device)))
                return {};

            CoreExtensions extensions;
            if (instance != nullptr)
                extensions.mInstance.assign(instance, instance + instanceCount);
            if (device != nullptr)
                extensions.mDevice.assign(device, device + deviceCount);
            return extensions;
        }
    }

    Application::Application(const DlssLibrary::Folders& folders)
    {
        // NGX's own level `On`, its errors and its steps, and not `Verbose`, which repeats every
        // configuration file it looks for at every feature. The game's log is the one sink: no
        // `nvngx.log` beside the library and nothing on standard output.
        mCommon.Logging = Ngx::LoggingInfo{
            .LoggingCallback = logNgx,
            .MinimumLoggingLevel = Ngx::LoggingLevel::On,
            .DisableOtherLoggingSinks = true,
        };

        std::error_code error;
        if (!folders.mRuntime.empty())
        {
            mSearchPath = folders.mRuntime.wstring();
            mSearchPaths[0] = mSearchPath.c_str();
            mCommon.PathList.Path = mSearchPaths.data();
            mCommon.PathList.Length = 1;

            std::filesystem::create_directories(folders.mRuntime, error);
            if (!error)
                mDataPath = mSearchPath;
        }
        if (mDataPath.empty())
            mDataPath = std::filesystem::temp_directory_path(error).wstring();
    }

    Ngx::FeatureDiscoveryInfo Application::discovery() const
    {
        Ngx::FeatureDiscoveryInfo info{};
        info.SDKVersion = Ngx::sApiVersion;
        info.FeatureId = Ngx::Feature::SuperSampling;
        info.Identifier.IdentifierType = Ngx::ApplicationKind::Project;
        info.Identifier.Value.Project.ProjectId = sProjectId;
        info.Identifier.Value.Project.EngineType = Ngx::Engine::Custom;
        info.Identifier.Value.Project.EngineVersion = Version::getVersion().data();
        info.ApplicationDataPath = getDataPath();
        info.FeatureInfo = getCommonInfo();
        return info;
    }

    // **A feature's own list where the core has one, and the core's list where it answers
    // `NotImplemented`**: the Linux core 615.78.8 answers so for DLSS before a device exists, and the
    // device it was not asked for then lacks `VK_NVX_binary_import`, which feature creation needs.

    std::vector<std::string> Application::requiredInstanceExtensions() const
    {
        const Ngx::Api* ngx = Ngx::api();
        if (ngx == nullptr)
            return {};

        const Ngx::FeatureDiscoveryInfo info = discovery();
        std::uint32_t count = 0;
        VkExtensionProperties* properties = nullptr;
        const Ngx::Result result = ngx->GetFeatureInstanceExtensionRequirements(&info, &count, &properties);
        if (result == Ngx::Result::NotImplemented)
            return coreExtensions(*ngx).mInstance;
        return namesOf(result, count, properties);
    }

    std::vector<std::string> Application::requiredDeviceExtensions(
        const VkInstance instance, const VkPhysicalDevice physicalDevice) const
    {
        const Ngx::Api* ngx = Ngx::api();
        if (ngx == nullptr)
            return {};

        const Ngx::FeatureDiscoveryInfo info = discovery();
        std::uint32_t count = 0;
        VkExtensionProperties* properties = nullptr;
        const Ngx::Result result
            = ngx->GetFeatureDeviceExtensionRequirements(instance, physicalDevice, &info, &count, &properties);
        if (result == Ngx::Result::NotImplemented)
            return coreExtensions(*ngx).mDevice;
        return namesOf(result, count, properties);
    }
}
