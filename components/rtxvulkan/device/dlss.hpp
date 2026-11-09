#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "ngx.hpp"

namespace Rtx::Dlss
{
    /// The folder NGX searches besides the game's own: the one the launcher downloads into. The
    /// path and the pointer array live here so both outlive the NGX call that reads them.
    struct FeatureInfo
    {
        FeatureInfo();
        FeatureInfo(const FeatureInfo&) = delete;
        FeatureInfo& operator=(const FeatureInfo&) = delete;

        const Ngx::FeatureCommonInfo* get() const;

        std::wstring mPath;
        std::array<const wchar_t*, 1> mPointers{};
        Ngx::FeatureCommonInfo mInfo{};
    };

    /// The path list for the process, made once: NGX may keep the pointers past the call that
    /// first hands it the list, so it cannot live on a frame's stack.
    const Ngx::FeatureCommonInfo* featureInfo();

    /// Whether this build has the DLSS backend. The one reading of the build flag, so the rest of
    /// the backend can ask a `constexpr bool` and not the preprocessor. The driver's NGX core is
    /// loaded at run time; nothing from NVIDIA's SDK is linked or shipped.
    inline constexpr bool sCompiled = OPENMW_RTX_DLSS;

    /// The project identifier NGX files DLSS's model and over-the-air updates under. It is a
    /// GUID-like custom-engine id, not an NVIDIA application id; any stable id of the fork's own
    /// serves, and changing it starts NGX's choices for this application over.
    inline constexpr const char* sProjectId = "4f3c1d2e-7a6b-4c58-9e0f-1a2b3c4d5e6f";

    /// The instance extensions NGX needs for DLSS on this driver, which the instance must enable
    /// before it is made. Asked before there is an instance, which is why it takes none.
    std::vector<std::string> requiredInstanceExtensions();

    /// The device extensions NGX needs for DLSS on this driver, which a device must enable before
    /// it is made. Empty where NGX cannot answer, which leaves feature creation to report why.
    std::vector<std::string> requiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice);

    /// Whether the driver and card accept DLSS's feature, as NGX answers before a device exists.
    bool isSupported(VkInstance instance, VkPhysicalDevice physicalDevice);
}
