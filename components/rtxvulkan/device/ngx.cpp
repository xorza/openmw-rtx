#include "ngx.hpp"

#include <cassert>

#include "ngxlibrary.hpp"

namespace Rtx::Dlss::Ngx
{
    namespace
    {
        template <class Function>
        bool bind(const Platform::Library::ScopedHandle& library, Function& into, const char* name)
        {
            into = reinterpret_cast<Function>(Platform::Library::find(library.get(), name));
            return into != nullptr;
        }

        template <class Function>
        bool bind(Api& api, Function& into, const char* name)
        {
            return bind(api.mLibrary, into, name);
        }

        Api load()
        {
            Api api;

            // The driver's core: the DriverStore the registry names on Windows, the loader's own
            // search on Linux. A machine with neither has no DLSS and says so through `api()`
            // returning null.
            api.mLibrary = openCore();
            if (!api.mLibrary.isOpen())
                return api;

            // The driver exports the initialiser under names of its own: the SDK's
            // `Init_with_ProjectID` is a wrapper, and the `_Ext` entry point takes the arguments
            // this declares. The shorter `Init_ProjectID` is a different signature, not a fallback.
            const bool resolved = bind(api, api.GetFeatureInstanceExtensionRequirements,
                                      "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements")
                && bind(api, api.GetFeatureDeviceExtensionRequirements,
                    "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements")
                && bind(api, api.GetFeatureRequirements, "NVSDK_NGX_VULKAN_GetFeatureRequirements")
                && bind(api, api.InitWithProjectId, "NVSDK_NGX_VULKAN_Init_ProjectID_Ext")
                && bind(api, api.GetCapabilityParameters, "NVSDK_NGX_VULKAN_GetCapabilityParameters")
                && bind(api, api.DestroyParameters, "NVSDK_NGX_VULKAN_DestroyParameters")
                && bind(api, api.Shutdown, "NVSDK_NGX_VULKAN_Shutdown1")
                && bind(api, api.CreateFeature, "NVSDK_NGX_VULKAN_CreateFeature")
                && bind(api, api.EvaluateFeature, "NVSDK_NGX_VULKAN_EvaluateFeature")
                && bind(api, api.ReleaseFeature, "NVSDK_NGX_VULKAN_ReleaseFeature");

            if (!resolved)
                api.mLibrary.reset();

            return api;
        }

        void set(Parameter* parameters, const char* name, const unsigned int value)
        {
            parameters->Set(name, value);
        }

        void set(Parameter* parameters, const char* name, const int value)
        {
            parameters->Set(name, value);
        }

        void set(Parameter* parameters, const char* name, const float value)
        {
            parameters->Set(name, value);
        }

        void set(Parameter* parameters, const char* name, Resource* value)
        {
            parameters->Set(name, static_cast<void*>(value));
        }
    }

    bool Api::isOpen() const
    {
        return mLibrary.isOpen() && GetFeatureInstanceExtensionRequirements != nullptr
            && GetFeatureDeviceExtensionRequirements != nullptr && GetFeatureRequirements != nullptr
            && InitWithProjectId != nullptr && GetCapabilityParameters != nullptr && DestroyParameters != nullptr
            && Shutdown != nullptr && CreateFeature != nullptr && EvaluateFeature != nullptr
            && ReleaseFeature != nullptr;
    }

    const Api* api()
    {
        // Loaded once and kept for the process: NGX's feature calls outlive every renderer, and
        // unloading the core under a driver that still has a thread in it is not a risk worth
        // taking at exit. A machine without the core pays one failed open.
        static const Api sApi = load();
        return sApi.isOpen() ? &sApi : nullptr;
    }

    Result createDlss(
        const VkCommandBuffer commands, Parameter* parameters, const DlssCreateParams& create, Handle** feature)
    {
        const Api* ngx = api();
        assert(ngx != nullptr && "a DLSS feature created without the driver's NGX core");

        set(parameters, sCreationNodeMask, 1u);
        set(parameters, sVisibilityNodeMask, 1u);
        set(parameters, sWidth, create.Feature.Width);
        set(parameters, sHeight, create.Feature.Height);
        set(parameters, sOutWidth, create.Feature.TargetWidth);
        set(parameters, sOutHeight, create.Feature.TargetHeight);
        set(parameters, sPerfQualityValue, static_cast<int>(create.Feature.PerfQuality));
        set(parameters, sFeatureCreateFlags, create.Flags);
        set(parameters, sEnableOutputSubrects, create.OutputSubrects ? 1 : 0);

        return ngx->CreateFeature(commands, Feature::SuperSampling, parameters, feature);
    }

    Result evaluateDlss(
        const VkCommandBuffer commands, const Handle* feature, Parameter* parameters, const DlssEvalParams& evaluation)
    {
        const Api* ngx = api();
        assert(ngx != nullptr && "a DLSS frame evaluated without the driver's NGX core");

        set(parameters, sColor, evaluation.Feature.Color);
        set(parameters, sOutput, evaluation.Feature.Output);
        set(parameters, sDepth, evaluation.Depth);
        set(parameters, sMotionVectors, evaluation.MotionVectors);
        set(parameters, sJitterX, evaluation.JitterX);
        set(parameters, sJitterY, evaluation.JitterY);
        set(parameters, sSharpness, evaluation.Feature.Sharpness);
        set(parameters, sReset, evaluation.Reset);
        set(parameters, sMotionVectorScaleX,
            evaluation.MotionVectorScaleX == 0.0f ? 1.0f : evaluation.MotionVectorScaleX);
        set(parameters, sMotionVectorScaleY,
            evaluation.MotionVectorScaleY == 0.0f ? 1.0f : evaluation.MotionVectorScaleY);
        set(parameters, sPreExposure, evaluation.PreExposure == 0.0f ? 1.0f : evaluation.PreExposure);
        set(parameters, sExposureScale, evaluation.ExposureScale == 0.0f ? 1.0f : evaluation.ExposureScale);
        set(parameters, sFrameTimeDelta, evaluation.FrameTimeDeltaInMsec);
        set(parameters, sRenderSubrectWidth, evaluation.RenderSubrectDimensions.Width);
        set(parameters, sRenderSubrectHeight, evaluation.RenderSubrectDimensions.Height);

        // The SDK's helper leaves these three unset by supplying null, so a map carried from one
        // frame to the next never reads the last frame's mask or exposure as this frame's.
        parameters->Set(sTransparencyMask, static_cast<void*>(nullptr));
        parameters->Set(sExposureTexture, static_cast<void*>(nullptr));
        parameters->Set(sBiasCurrentColorMask, static_cast<void*>(nullptr));

        return ngx->EvaluateFeature(commands, feature, parameters, nullptr);
    }
}
