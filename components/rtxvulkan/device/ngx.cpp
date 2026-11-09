#include "ngx.hpp"

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
            const Platform::Library::ScopedHandle& core = api.mLibrary;
            const bool resolved = bind(core, api.GetFeatureInstanceExtensionRequirements,
                                      "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements")
                && bind(core, api.GetFeatureDeviceExtensionRequirements,
                    "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements")
                && bind(core, api.InitWithProjectId, "NVSDK_NGX_VULKAN_Init_ProjectID_Ext")
                && bind(core, api.GetCapabilityParameters, "NVSDK_NGX_VULKAN_GetCapabilityParameters")
                && bind(core, api.AllocateParameters, "NVSDK_NGX_VULKAN_AllocateParameters")
                && bind(core, api.DestroyParameters, "NVSDK_NGX_VULKAN_DestroyParameters")
                && bind(core, api.Shutdown, "NVSDK_NGX_VULKAN_Shutdown1")
                && bind(core, api.CreateFeature, "NVSDK_NGX_VULKAN_CreateFeature")
                && bind(core, api.EvaluateFeature, "NVSDK_NGX_VULKAN_EvaluateFeature")
                && bind(core, api.ReleaseFeature, "NVSDK_NGX_VULKAN_ReleaseFeature")
                && bind(core, api.RequiredExtensions, "NVSDK_NGX_VULKAN_RequiredExtensions");

            if (!resolved)
                api.mLibrary.reset();

            return api;
        }
    }

    const Api* api()
    {
        // Loaded once and kept for the process: NGX's feature calls outlive every renderer, and
        // unloading the core under a driver that still has a thread in it is not a risk worth
        // taking at exit. A machine without the core pays one failed open. `load` keeps the
        // library open only where every entry point resolved.
        static const Api sApi = load();
        return sApi.mLibrary.isOpen() ? &sApi : nullptr;
    }

    Result createDlss(const Api& api, const VkCommandBuffer commands, Parameter& parameters,
        const DlssCreateParams& create, Handle** feature)
    {
        parameters.Set(sCreationNodeMask, 1u);
        parameters.Set(sVisibilityNodeMask, 1u);
        parameters.Set(sWidth, create.Feature.Width);
        parameters.Set(sHeight, create.Feature.Height);
        parameters.Set(sOutWidth, create.Feature.TargetWidth);
        parameters.Set(sOutHeight, create.Feature.TargetHeight);
        parameters.Set(sPerfQualityValue, static_cast<int>(create.Feature.PerfQuality));
        parameters.Set(sFeatureCreateFlags, create.Flags);
        parameters.Set(sEnableOutputSubrects, create.OutputSubrects ? 1 : 0);

        return api.CreateFeature(commands, Feature::SuperSampling, &parameters, feature);
    }

    Result evaluateDlss(const Api& api, const VkCommandBuffer commands, const Handle* feature, Parameter& parameters,
        const DlssEvalParams& evaluation)
    {
        // Every `Resource*` through the `void*` slot, which is the one NGX's Vulkan path reads.
        const auto resource = [](Resource* value) { return static_cast<void*>(value); };
        parameters.Set(sColor, resource(evaluation.Feature.Color));
        parameters.Set(sOutput, resource(evaluation.Feature.Output));
        parameters.Set(sDepth, resource(evaluation.Depth));
        parameters.Set(sMotionVectors, resource(evaluation.MotionVectors));
        parameters.Set(sJitterX, evaluation.JitterX);
        parameters.Set(sJitterY, evaluation.JitterY);
        parameters.Set(sSharpness, evaluation.Feature.Sharpness);
        parameters.Set(sReset, evaluation.Reset);
        parameters.Set(sMotionVectorScaleX, evaluation.MotionVectorScaleX);
        parameters.Set(sMotionVectorScaleY, evaluation.MotionVectorScaleY);
        parameters.Set(sPreExposure, evaluation.PreExposure);
        parameters.Set(sExposureScale, evaluation.ExposureScale);
        parameters.Set(sFrameTimeDelta, evaluation.FrameTimeDeltaInMsec);
        parameters.Set(sRenderSubrectWidth, evaluation.RenderSubrectDimensions.Width);
        parameters.Set(sRenderSubrectHeight, evaluation.RenderSubrectDimensions.Height);

        // Set even where null, as the SDK's helper sets them, so a map carried from one frame to the
        // next never reads the last frame's mask or exposure as this frame's.
        parameters.Set(sTransparencyMask, resource(evaluation.TransparencyMask));
        parameters.Set(sExposureTexture, resource(evaluation.ExposureTexture));
        parameters.Set(sBiasCurrentColorMask, resource(evaluation.BiasCurrentColorMask));

        return api.EvaluateFeature(commands, feature, &parameters, nullptr);
    }
}
