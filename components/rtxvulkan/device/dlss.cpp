#include "dlss.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include <components/platform/dlss.hpp>
#include <components/version/version.hpp>

namespace Rtx::Dlss
{
    FeatureInfo::FeatureInfo()
    {
        const std::filesystem::path directory = Platform::Dlss::runtimeDirectory();
        if (directory.empty())
            return;

        mPath = directory.wstring();
        mPointers[0] = mPath.c_str();
        mInfo.PathList.Path = mPointers.data();
        mInfo.PathList.Length = 1;
    }

    const Ngx::FeatureCommonInfo* FeatureInfo::get() const
    {
        return mInfo.PathList.Length == 0 ? nullptr : &mInfo;
    }

    const Ngx::FeatureCommonInfo* featureInfo()
    {
        static const FeatureInfo sFeatureInfo;
        return sFeatureInfo.get();
    }

    namespace
    {
        /// The writable folder NGX logs to, the same one initialisation is given. A function-local
        /// static because the discovery record only borrows the pointer for the call.
        const std::wstring& applicationDataPath()
        {
            static const std::wstring sPath = Platform::Dlss::applicationDataDirectory().wstring();
            return sPath;
        }

        Ngx::FeatureDiscoveryInfo discovery(const Ngx::FeatureCommonInfo* featureInfo)
        {
            Ngx::FeatureDiscoveryInfo info{};
            info.SDKVersion = Ngx::sApiVersion;
            info.FeatureId = Ngx::Feature::SuperSampling;
            info.Identifier.IdentifierType = Ngx::ApplicationKind::Project;
            info.Identifier.Value.Project.ProjectId = sProjectId;
            info.Identifier.Value.Project.EngineType = Ngx::Engine::Custom;
            info.Identifier.Value.Project.EngineVersion = Version::getVersion().data();
            info.ApplicationDataPath = applicationDataPath().c_str();
            info.FeatureInfo = featureInfo;
            return info;
        }
    }

    std::vector<std::string> requiredInstanceExtensions()
    {
        const Ngx::Api* ngx = Ngx::api();
        if (ngx == nullptr)
            return {};

        Ngx::FeatureDiscoveryInfo info = discovery(featureInfo());
        std::uint32_t count = 0;
        VkExtensionProperties* properties = nullptr;
        if (Ngx::failed(ngx->GetFeatureInstanceExtensionRequirements(&info, &count, &properties))
            || properties == nullptr)
            return {};

        std::vector<std::string> names;
        names.reserve(count);
        for (std::uint32_t at = 0; at < count; ++at)
            names.emplace_back(properties[at].extensionName);
        return names;
    }

    std::vector<std::string> requiredDeviceExtensions(const VkInstance instance, const VkPhysicalDevice physicalDevice)
    {
        const Ngx::Api* ngx = Ngx::api();
        if (ngx == nullptr)
            return {};

        Ngx::FeatureDiscoveryInfo info = discovery(featureInfo());
        std::uint32_t count = 0;
        VkExtensionProperties* properties = nullptr;
        if (Ngx::failed(
                ngx->GetFeatureDeviceExtensionRequirements(instance, physicalDevice, &info, &count, &properties))
            || properties == nullptr)
            return {};

        std::vector<std::string> names;
        names.reserve(count);
        for (std::uint32_t at = 0; at < count; ++at)
            names.emplace_back(properties[at].extensionName);
        return names;
    }

    bool isSupported(const VkInstance instance, const VkPhysicalDevice physicalDevice)
    {
        const Ngx::Api* ngx = Ngx::api();
        if (ngx == nullptr)
            return false;

        Ngx::FeatureDiscoveryInfo info = discovery(featureInfo());
        Ngx::FeatureRequirement requirement{};
        const Ngx::Result result = ngx->GetFeatureRequirements(instance, physicalDevice, &info, &requirement);
        return Ngx::succeeded(result) && requirement.FeatureSupported == Ngx::Support::Supported;
    }
}
