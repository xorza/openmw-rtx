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

    Result createDlss(const Api& api, const VkCommandBuffer commands, Parameter& parameters, const DlssFeature& create,
        Handle** feature)
    {
        parameters.Set(sCreationNodeMask, 1u);
        parameters.Set(sVisibilityNodeMask, 1u);
        parameters.Set(sWidth, create.mRender.width);
        parameters.Set(sHeight, create.mRender.height);
        parameters.Set(sOutWidth, create.mOutput.width);
        parameters.Set(sOutHeight, create.mOutput.height);
        parameters.Set(sPerfQualityValue, static_cast<int>(create.mQuality));
        parameters.Set(sFeatureCreateFlags, create.mFlags);
        parameters.Set(sEnableOutputSubrects, 0);

        return api.CreateFeature(commands, Feature::SuperSampling, &parameters, feature);
    }

    Result evaluateDlss(const Api& api, const VkCommandBuffer commands, const Handle* feature, Parameter& parameters,
        const DlssFrame& frame)
    {
        // Every `Resource*` through the `void*` slot, which is the one NGX's Vulkan path reads.
        const auto resource = [](Resource* value) { return static_cast<void*>(value); };
        parameters.Set(sColor, resource(frame.mColour));
        parameters.Set(sOutput, resource(frame.mOutput));
        parameters.Set(sDepth, resource(frame.mDepth));
        parameters.Set(sMotionVectors, resource(frame.mMotion));
        parameters.Set(sJitterX, frame.mJitterX);
        parameters.Set(sJitterY, frame.mJitterY);
        parameters.Set(sReset, frame.mReset ? 1 : 0);
        parameters.Set(sFrameTimeDelta, frame.mFrameMs);
        parameters.Set(sRenderSubrectWidth, frame.mRender.width);
        parameters.Set(sRenderSubrectHeight, frame.mRender.height);

        // **What the renderer never varies**: no sharpening; motion in render pixels, the unit the trace
        // writes it in; and colour at its own exposure, which DLSS's own meter reads (`sCreateFlags`).
        parameters.Set(sSharpness, 0.0f);
        parameters.Set(sMotionVectorScaleX, 1.0f);
        parameters.Set(sMotionVectorScaleY, 1.0f);
        parameters.Set(sPreExposure, 1.0f);
        parameters.Set(sExposureScale, 1.0f);

        // Set even where null, as the SDK's helper sets them, so a map carried from one frame to the
        // next never reads the last frame's mask or exposure as this frame's.
        parameters.Set(sTransparencyMask, resource(nullptr));
        parameters.Set(sExposureTexture, resource(nullptr));
        parameters.Set(sBiasCurrentColorMask, resource(nullptr));

        return api.EvaluateFeature(commands, feature, &parameters, nullptr);
    }
}
