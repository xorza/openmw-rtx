#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/dlss/dlss.hpp>
#include <components/rtxvulkan/device/driverprofile.hpp>

#include "ngx.hpp"

namespace Rtx::Dlss
{
    /// What NGX is told of the application at every call that takes it, from the folders the host names: the folder it
    /// searches for the feature library besides the game's own, the folder it keeps its own files in, and where its log
    /// goes. Pinned, and held by the renderer for its whole life, ahead of the instance: NGX keeps the pointers past
    /// the call that first hands them over. Made only where a run may use DLSS.
    class Application
    {
    public:
        explicit Application(const DlssLibrary::Folders& folders);

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        /// The search path and the log, as `Init` and every discovery call take them.
        const Ngx::FeatureCommonInfo* getCommonInfo() const { return &mCommon; }

        /// The writable folder NGX keeps its files in: the runtime folder, made if it does not
        /// stand, or the system's temporary folder where the host names none or it cannot be made.
        /// Never the working directory, which an AppImage or Program Files can make read-only.
        const wchar_t* getDataPath() const { return mDataPath.c_str(); }

        /// The instance extensions NGX needs for DLSS on this driver, which the instance must enable before it is made.
        /// Empty where NGX cannot answer, in a build without the backend, and where the system has no NVIDIA adapter,
        /// the one fact there is before a device is chosen: a Windows machine keeps the NGX core in its DriverStore
        /// after its NVIDIA card is gone, and it is not to be loaded beside another vendor's driver.
        std::vector<std::string> requiredInstanceExtensions() const;

        /// The device extensions NGX needs for DLSS on `physicalDevice`, which a device must enable before it is made,
        /// where `driver` runs NGX. Asked of every such device: the query NGX answers before a device exists can be
        /// inconclusive, and a false negative must not silently cost an RTX card its upscaler; feature creation is the
        /// real answer (`DlssUpscaler`). Empty where NGX cannot answer, and in a build without the backend.
        std::vector<std::string> requiredDeviceExtensions(
            VkInstance instance, VkPhysicalDevice physicalDevice, const DriverProfile& driver) const;

    private:
        Ngx::FeatureDiscoveryInfo discovery() const;

        std::wstring mSearchPath;
        std::array<const wchar_t*, 1> mSearchPaths{};
        std::wstring mDataPath;
        Ngx::FeatureCommonInfo mCommon{};
    };

    /// The project identifier NGX files DLSS's model and over-the-air updates under. It is a
    /// GUID-like custom-engine id, not an NVIDIA application id; any stable id of the fork's own
    /// serves, and changing it starts NGX's choices for this application over.
    inline constexpr const char* sProjectId = "4f3c1d2e-7a6b-4c58-9e0f-1a2b3c4d5e6f";
}
