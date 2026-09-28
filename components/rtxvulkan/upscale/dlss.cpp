#include "dlss.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <nvsdk_ngx_vk.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/files/fixedpath.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "ngx.hpp"
#include "ngxdispatch.hpp"

namespace Rtx
{
    namespace
    {
        /// Somewhere NGX may write its own files — not where its feature libraries live, which
        /// are found through `NVSDK_NGX_FeatureCommonInfo`. Handing it the library directory fails
        /// at `Init` with `FAIL_InvalidParameter`, which names no parameter.
        ///
        /// **Wide through `std::filesystem::path`, on both platforms.** NGX takes `wchar_t`, and
        /// what that holds is the system's: UTF-16 on Windows, where the path already is one and
        /// a byte-by-byte widening handed NGX the wrong characters for any user name outside
        /// ASCII, and UTF-32 on Linux.
        const wchar_t* dataPath()
        {
            static const std::wstring path = [] {
                const std::filesystem::path where = std::filesystem::temp_directory_path() / "openmw-rtx-ngx";
                std::error_code ignored;
                std::filesystem::create_directories(where, ignored);

                return where.wstring();
            }();

            return path.c_str();
        }

        /// Where NGX finds the feature library it loads at runtime: the directory of the running
        /// executable, which the build and an install put the library in. Named rather than left
        /// to NGX's default, because the SDK calls that default "the application folder" and says
        /// no more, and a folder that turned out to be the working directory would find nothing in
        /// a game started from anywhere else.
        const wchar_t* featurePath()
        {
            static const std::wstring path = Files::TargetPathType("openmw").getLocalPath().wstring();

            return path.c_str();
        }

        /// Off unless asked for, and that is not timidity. NGX's feature libraries write around
        /// a thousand lines to the console on one successful run — enough to bury the message of
        /// whatever failure sent someone looking for them. The reference implementation found the
        /// one error that mattered only in this log, and it appears nowhere in the API surface.
        NVSDK_NGX_Logging_Level loggingLevel()
        {
            return std::getenv("OPENMW_RTX_NGX_LOG") != nullptr ? NVSDK_NGX_LOGGING_LEVEL_ON
                                                                : NVSDK_NGX_LOGGING_LEVEL_OFF;
        }

        /// Ends NGX on `device`, once nothing of NGX's is in flight there.
        ///
        /// **The programming guide makes that the caller's** (5.6: no work associated with the
        /// runtime still in flight), and a runtime that built no feature still leaves work of its
        /// own on the device it was started on. Shut down under that work, the device is lost the
        /// next time the process destroys a device, any device — `vkDeviceWaitIdle` answers
        /// `VK_ERROR_DEVICE_LOST` with an invalid write, even for a runtime only asked a render size.
        void shutDown(const Device& device)
        {
            tearDown("the device would not finish before NGX was shut down", [&] { device.waitIdle(); });
            NVSDK_NGX_VULKAN_Shutdown1(device.getHandle());
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
            throw Unsupported("NGX would not say which instance extensions it needs");

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
            throw Unsupported("NGX would not say which device extensions it needs");

        return std::span<const char* const>(device, deviceCount);
    }

    DlssSupport Dlss::probe(const Device& device, VkInstance instance)
    {
        if (const Dlss* const live = liveOn(device.getHandle()))
            return DlssSupport{ live->mAvailable, live->mObstacle };

        // Stood up and taken down inside this call, which is what makes it safe to ask from
        // anywhere: nothing outside holds a runtime on this device that this could be ending,
        // because the branch above is what happens when something does.
        const Dlss asked(device, instance);
        return DlssSupport{ asked.mAvailable, asked.mObstacle };
    }

    Dlss::Dlss(const Device& device, VkInstance instance)
        : mDevice(device)
    {
        // Before anything is started, so a refusal leaves the runtime that is up untouched. A
        // constructor that threw after `Init` would have shut the first one down on the way out.
        if (liveOn(device.getHandle()) != nullptr)
            throw Unsupported("NGX starts once per device and is already up on this one");

        const wchar_t* const searched[] = { featurePath() };

        NVSDK_NGX_FeatureCommonInfo common{};
        common.PathListInfo.Path = searched;
        common.PathListInfo.Length = 1;
        common.LoggingInfo.LoggingCallback = nullptr;
        common.LoggingInfo.MinimumLoggingLevel = loggingLevel();
        common.LoggingInfo.DisableOtherLoggingSinks = false;

        // NVIDIA's handle on an application, for their own telemetry and driver overrides. A GUID
        // and parsed as one: a readable name comes back from `Init` as `FAIL_InvalidParameter`.
        // This fork's own, and the engine is `CUSTOM` because OpenMW is not one NVIDIA knows.
        const Crash::NoteScope noted("starting NGX");
        const NVSDK_NGX_Result started = NVSDK_NGX_VULKAN_Init_with_ProjectID("c541dbdf-6e4f-4476-ad27-15d2b4a231f4",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.52", dataPath(), instance, device.getPhysicalDevice().getHandle(),
            device.getHandle(), ngxInstanceProcAddr, ngxDeviceProcAddr, &common, NVSDK_NGX_Version_API);

        if (NVSDK_NGX_FAILED(started))
            throw Unsupported("NGX would not start: " + describeNgxResult(started));

        const NVSDK_NGX_Result asked = NVSDK_NGX_VULKAN_GetCapabilityParameters(&mCapabilities);
        if (NVSDK_NGX_FAILED(asked) || mCapabilities == nullptr)
        {
            // A constructor that throws gets no destructor, and NGX is up: leaving it that way
            // would refuse every later attempt for a reason that is no longer true.
            NVSDK_NGX_VULKAN_DestroyParameters(mCapabilities);
            shutDown(device);
            throw Unsupported("NGX started and would not say what it can do: " + describeNgxResult(asked));
        }

        int available = 0;
        mCapabilities->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
        mAvailable = available != 0;

        if (!mAvailable)
        {
            int needsDriver = 0;
            mCapabilities->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsDriver);
            mObstacle = needsDriver != 0 ? "this driver is older than the Ray Reconstruction it would have to load"
                                         : "this device does not offer Ray Reconstruction";
        }

        // Last, so that only a runtime that came all the way up claims its device. Everything above
        // throws on failure, and a constructor that threw gets no destructor to clear this.
        sLive.push_back(this);
    }

    VkExtent2D Dlss::getRenderSize(VkExtent2D output, Upscale upscale) const
    {
        // Dynamic resolution is not something this renderer does — the trace's targets are made
        // once per size — so the range the query also fills in is read and discarded.
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int mostWide = 0;
        unsigned int mostTall = 0;
        unsigned int leastWide = 0;
        unsigned int leastTall = 0;
        float sharpness = 0.0f;

        // The query is a function pointer inside the capability map, not an exported symbol —
        // the driver's feature library puts it there. So it is absent exactly when that library was
        // not found, and the helper answers `FAIL_OutOfDate` rather than anything about paths.
        const NVSDK_NGX_Result asked = NGX_DLSSD_GET_OPTIMAL_SETTINGS(mCapabilities, output.width, output.height,
            ngxQualityOf(upscale), &width, &height, &mostWide, &mostTall, &leastWide, &leastTall, &sharpness);

        if (NVSDK_NGX_FAILED(asked))
            throw Unsupported("DLSS would not say what to render at: " + describeNgxResult(asked));

        if (width == 0 || height == 0)
            throw Unsupported("DLSS answered with an empty render size");

        return VkExtent2D{ width, height };
    }

    Dlss::~Dlss()
    {
        std::erase(sLive, this);

        // Destroyed, and it is not NGX's to reclaim. The SDK tells `GetCapabilityParameters`
        // apart from the deprecated `GetParameters` on exactly this: a capability map is the
        // caller's, and `DlssPass` releases the one it allocates for the same reason.
        NVSDK_NGX_VULKAN_DestroyParameters(mCapabilities);
        mCapabilities = nullptr;

        shutDown(mDevice);
    }

    VkDevice Dlss::getDevice() const
    {
        return mDevice.getHandle();
    }

    const Dlss* Dlss::liveOn(const VkDevice device)
    {
        const auto found = std::find_if(
            sLive.begin(), sLive.end(), [device](const Dlss* live) { return live->getDevice() == device; });
        return found != sLive.end() ? *found : nullptr;
    }
}
