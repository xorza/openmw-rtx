#include "dlss.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>

#include <components/rtx/error.hpp>

#include "device.hpp"
#include "physicaldevice.hpp"

namespace Rtx
{
    namespace
    {
        /// Anything but success, as something a message can carry.
        std::string nameResult(NVSDK_NGX_Result result)
        {
            // NGX gives its own wide description, which is more use than the number.
            const wchar_t* wide = GetNGXResultAsString(result);
            std::string text;
            for (const wchar_t* at = wide; at != nullptr && *at != L'\0'; ++at)
                text += static_cast<char>(*at);

            return text;
        }

        /// Somewhere NGX may write its own files.
        ///
        /// **Not the same thing as where its feature libraries live**, which is the mistake to make:
        /// this is a writable directory of NGX's own, and the libraries are found through the search
        /// list in `NVSDK_NGX_FeatureCommonInfo`. Handing it the library directory instead fails at
        /// `Init` with `FAIL_InvalidParameter`, which names no parameter.
        const wchar_t* dataPath()
        {
            static const std::wstring path = [] {
                const std::filesystem::path where = std::filesystem::temp_directory_path() / "openmw-rtx-ngx";
                std::error_code ignored;
                std::filesystem::create_directories(where, ignored);

                const std::string given = where.string();
                return std::wstring(given.begin(), given.end());
            }();

            return path.c_str();
        }

        /// Where NGX finds the feature libraries it loads at runtime.
        ///
        /// Its own search is the application's folder alone, and these are nowhere near the binary.
        const wchar_t* featurePath()
        {
            static const std::wstring path = [] {
                const std::string_view given = OPENMW_RTX_NGX_FEATURES;
                return std::wstring(given.begin(), given.end());
            }();

            return path.c_str();
        }

        /// **Off unless asked for, and that is not timidity.** NGX's feature libraries write around
        /// a thousand lines to the console on one successful run — enough to bury the message of
        /// whatever failure sent someone looking for them. The reference implementation found the
        /// one error that mattered only in this log, and it appears nowhere in the API surface.
        NVSDK_NGX_Logging_Level loggingLevel()
        {
            return std::getenv("OPENMW_RTX_NGX_LOG") != nullptr ? NVSDK_NGX_LOGGING_LEVEL_ON
                                                                : NVSDK_NGX_LOGGING_LEVEL_OFF;
        }
    }

    std::span<const char* const> Dlss::getInstanceExtensions()
    {
        unsigned int instanceCount = 0;
        const char** instance = nullptr;
        unsigned int deviceCount = 0;
        const char** device = nullptr;

        if (NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instance, &deviceCount, &device)
            != NVSDK_NGX_Result_Success)
            throw Error("NGX would not say which instance extensions it needs");

        return std::span<const char* const>(instance, instanceCount);
    }

    std::span<const char* const> Dlss::getDeviceExtensions()
    {
        unsigned int instanceCount = 0;
        const char** instance = nullptr;
        unsigned int deviceCount = 0;
        const char** device = nullptr;

        if (NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instance, &deviceCount, &device)
            != NVSDK_NGX_Result_Success)
            throw Error("NGX would not say which device extensions it needs");

        return std::span<const char* const>(device, deviceCount);
    }

    Dlss::Dlss(const Device& device, VkInstance instance)
        : mDevice(device.getHandle())
    {
        const wchar_t* const searched[] = { featurePath() };

        NVSDK_NGX_FeatureCommonInfo common{};
        common.PathListInfo.Path = searched;
        common.PathListInfo.Length = 1;
        common.LoggingInfo.LoggingCallback = nullptr;
        common.LoggingInfo.MinimumLoggingLevel = loggingLevel();
        common.LoggingInfo.DisableOtherLoggingSinks = false;

        // NVIDIA's handle on an application, for their own telemetry and driver overrides.
        //
        // **A GUID, and parsed as one.** The driver checks the shape and nothing else: a readable
        // name here comes back from `Init` as `FAIL_InvalidParameter`, which names no parameter and
        // writes nothing to the log. This one is this fork's own rather than borrowed, and the
        // engine is `CUSTOM` because OpenMW is not one of the engines NVIDIA knows.
        const NVSDK_NGX_Result started = NVSDK_NGX_VULKAN_Init_with_ProjectID("c541dbdf-6e4f-4476-ad27-15d2b4a231f4",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.52", dataPath(), instance, device.getPhysicalDevice().getHandle(), mDevice,
            vkGetInstanceProcAddr, vkGetDeviceProcAddr, &common, NVSDK_NGX_Version_API);

        if (NVSDK_NGX_FAILED(started))
            throw Error("NGX would not start: " + nameResult(started));

        NVSDK_NGX_Parameter* capabilities = nullptr;
        const NVSDK_NGX_Result asked = NVSDK_NGX_VULKAN_GetCapabilityParameters(&capabilities);
        if (NVSDK_NGX_FAILED(asked) || capabilities == nullptr)
            throw Error("NGX started and would not say what it can do: " + nameResult(asked));

        int available = 0;
        capabilities->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
        mAvailable = available != 0;

        if (!mAvailable)
        {
            int needsDriver = 0;
            capabilities->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsDriver);
            mObstacle = needsDriver != 0 ? "this driver is older than the Ray Reconstruction it would have to load"
                                         : "this device does not offer Ray Reconstruction";
        }
    }

    Dlss::~Dlss()
    {
        if (mDevice != VK_NULL_HANDLE)
            NVSDK_NGX_VULKAN_Shutdown1(mDevice);
    }
}
