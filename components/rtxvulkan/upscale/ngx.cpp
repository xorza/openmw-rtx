#include "ngx.hpp"

#include <array>

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

    namespace
    {
        /// The callback NGX publishes on a capability map for a query that is not an entry point of
        /// the core: the optimal settings and the stats, each `PFN(Parameter*)`, as the SDK's helpers
        /// call them.
        using QueryCallback = Result (*)(Parameter*);

        QueryCallback callbackOf(const Parameter& capabilities, const char* name)
        {
            void* callback = nullptr;
            if (failed(capabilities.Get(name, &callback)))
                return nullptr;
            return reinterpret_cast<QueryCallback>(callback); // NGX hands the callback through a void*.
        }

        /// Every resource the evaluate helper hands NGX that this backend has none for, set to null
        /// on every evaluation as the helper sets it, in the helper's order (`nvsdk_ngx_helpers_dlssd_vk.h`
        /// at 310.9.16).
        constexpr std::array sUnusedResources{
            "TransparencyMask",
            "ExposureTexture",
            "DLSS.Input.Bias.Current.Color.Mask",
            "GBuffer.Albedo",
            "GBuffer.Metallic",
            "GBuffer.Specular",
            "GBuffer.Subsurface",
            "GBuffer.ShadingModelId",
            "GBuffer.MaterialId",
            "GBuffer.Attrib.8",
            "GBuffer.Attrib.9",
            "GBuffer.SpecularMvec",
            "GBuffer.Attrib.11",
            "GBuffer.Attrib.12",
            "GBuffer.Attrib.13",
            "GBuffer.Attrib.14",
            "GBuffer.Attrib.15",
            "MotionVectors3D",
            "IsParticleMask",
            "AnimatedTextureMask",
            "DepthHighRes",
            "Position.ViewSpace",
            "RayTracingHitDistance",
            "GBuffer.Emissive",
            "DLSSD.Alpha",
            "DLSSD.OutputAlpha",
            "DLSSD.ReflectedAlbedo",
            "DLSSD.ColorBeforeParticles",
            "DLSSD.ColorAfterParticles",
            "DLSSD.ColorBeforeTransparency",
            "DLSSD.ColorAfterTransparency",
            "DLSSD.ColorBeforeFog",
            "DLSSD.ColorAfterFog",
            "DLSSD.ScreenSpaceSubsurfaceScatteringGuide",
            "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering",
            "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering",
            "DLSSD.ScreenSpaceRefractionGuide",
            "DLSSD.ColorBeforeScreenSpaceRefraction",
            "DLSSD.ColorAfterScreenSpaceRefraction",
            "DLSSD.DepthOfFieldGuide",
            "DLSSD.ColorBeforeDepthOfField",
            "DLSSD.ColorAfterDepthOfField",
            "DLSSD.DiffuseHitDistance",
            "DLSSD.SpecularHitDistance",
            "DLSSD.DiffuseRayDirection",
            "DLSSD.SpecularRayDirection",
            "DLSSD.DiffuseRayDirectionHitDistance",
            "DLSSD.SpecularRayDirectionHitDistance",
            "WorldToViewMatrix",
            "ViewToClipMatrix",
            "DLSS.TransparencyLayer",
            "DLSS.TransparencyLayerOpacity",
            "DLSS.TransparencyLayerMvecs",
            "DLSS.DisocclusionMask",
            "DLSSD.ResponsivityMask",
        };

        /// Every whole number the evaluate helper sets that this backend leaves at nought: each input's
        /// and the output's corner, since every image is read from its own, the tone mapper's kind, and
        /// the indicator's axes.
        constexpr std::array sZeroWholes{
            "TonemapperType",
            "DLSS.Input.Color.Subrect.Base.X",
            "DLSS.Input.Color.Subrect.Base.Y",
            "DLSS.Input.Depth.Subrect.Base.X",
            "DLSS.Input.Depth.Subrect.Base.Y",
            "DLSS.Input.MV.Subrect.Base.X",
            "DLSS.Input.MV.Subrect.Base.Y",
            "DLSS.Input.Translucency.Subrect.Base.X",
            "DLSS.Input.Translucency.Subrect.Base.Y",
            "DLSS.Input.Bias.Current.Color.Subrect.Base.X",
            "DLSS.Input.Bias.Current.Color.Subrect.Base.Y",
            "DLSS.Output.Subrect.Base.X",
            "DLSS.Output.Subrect.Base.Y",
            "DLSS.Input.DiffuseAlbedo.Subrect.Base.X",
            "DLSS.Input.DiffuseAlbedo.Subrect.Base.Y",
            "DLSS.Input.SpecularAlbedo.Subrect.Base.X",
            "DLSS.Input.SpecularAlbedo.Subrect.Base.Y",
            "DLSS.Input.Normals.Subrect.Base.X",
            "DLSS.Input.Normals.Subrect.Base.Y",
            "DLSS.Input.Roughness.Subrect.Base.X",
            "DLSS.Input.Roughness.Subrect.Base.Y",
            "DLSSD.Alpha.Subrect.Base.X",
            "DLSSD.Alpha.Subrect.Base.Y",
            "DLSSD.OutputAlpha.Subrect.Base.X",
            "DLSSD.OutputAlpha.Subrect.Base.Y",
            "DLSSD.ReflectedAlbedo.Subrect.Base.X",
            "DLSSD.ReflectedAlbedo.Subrect.Base.Y",
            "DLSSD.ColorBeforeParticles.Subrect.Base.X",
            "DLSSD.ColorBeforeParticles.Subrect.Base.Y",
            "DLSSD.ColorAfterParticles.Subrect.Base.X",
            "DLSSD.ColorAfterParticles.Subrect.Base.Y",
            "DLSSD.ColorBeforeTransparency.Subrect.Base.X",
            "DLSSD.ColorBeforeTransparency.Subrect.Base.Y",
            "DLSSD.ColorAfterTransparency.Subrect.Base.X",
            "DLSSD.ColorAfterTransparency.Subrect.Base.Y",
            "DLSSD.ColorBeforeFog.Subrect.Base.X",
            "DLSSD.ColorBeforeFog.Subrect.Base.Y",
            "DLSSD.ColorAfterFog.Subrect.Base.X",
            "DLSSD.ColorAfterFog.Subrect.Base.Y",
            "DLSSD.ScreenSpaceSubsurfaceScatteringGuide.Subrect.Base.X",
            "DLSSD.ScreenSpaceSubsurfaceScatteringGuide.Subrect.Base.Y",
            "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering.Subrect.Base.X",
            "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering.Subrect.Base.Y",
            "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering.Subrect.Base.X",
            "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering.Subrect.Base.Y",
            "DLSSD.ScreenSpaceRefractionGuide.Subrect.Base.X",
            "DLSSD.ScreenSpaceRefractionGuide.Subrect.Base.Y",
            "DLSSD.ColorBeforeScreenSpaceRefraction.Subrect.Base.X",
            "DLSSD.ColorBeforeScreenSpaceRefraction.Subrect.Base.Y",
            "DLSSD.ColorAfterScreenSpaceRefraction.Subrect.Base.X",
            "DLSSD.ColorAfterScreenSpaceRefraction.Subrect.Base.Y",
            "DLSSD.DepthOfFieldGuide.Subrect.Base.X",
            "DLSSD.DepthOfFieldGuide.Subrect.Base.Y",
            "DLSSD.ColorBeforeDepthOfField.Subrect.Base.X",
            "DLSSD.ColorBeforeDepthOfField.Subrect.Base.Y",
            "DLSSD.ColorAfterDepthOfField.Subrect.Base.X",
            "DLSSD.ColorAfterDepthOfField.Subrect.Base.Y",
            "DLSSD.DiffuseHitDistance.Subrect.Base.X",
            "DLSSD.DiffuseHitDistance.Subrect.Base.Y",
            "DLSSD.SpecularHitDistance.Subrect.Base.X",
            "DLSSD.SpecularHitDistance.Subrect.Base.Y",
            "DLSSD.DiffuseRayDirection.Subrect.Base.X",
            "DLSSD.DiffuseRayDirection.Subrect.Base.Y",
            "DLSSD.SpecularRayDirection.Subrect.Base.X",
            "DLSSD.SpecularRayDirection.Subrect.Base.Y",
            "DLSSD.DiffuseRayDirectionHitDistance.Subrect.Base.X",
            "DLSSD.DiffuseRayDirectionHitDistance.Subrect.Base.Y",
            "DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.X",
            "DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.Y",
            "DLSS.TransparencyLayer.Subrect.Base.X",
            "DLSS.TransparencyLayer.Subrect.Base.Y",
            "DLSS.TransparencyLayerOpacity.Subrect.Base.X",
            "DLSS.TransparencyLayerOpacity.Subrect.Base.Y",
            "DLSS.TransparencyLayerMvecs.Subrect.Base.X",
            "DLSS.TransparencyLayerMvecs.Subrect.Base.Y",
            "DLSS.DisocclusionMask.Subrect.Base.X",
            "DLSS.DisocclusionMask.Subrect.Base.Y",
            "DLSSD.ResponsivityMask.Subrect.Base.X",
            "DLSSD.ResponsivityMask.Subrect.Base.Y",
        };

        /// The indicator's axes, which the helper sets as signed whole numbers where the corners are
        /// unsigned: a key's type is part of what NGX matches.
        constexpr std::array sZeroSigned{
            "DLSS.Indicator.Invert.X.Axis",
            "DLSS.Indicator.Invert.Y.Axis",
        };
    }

    Result createRayReconstruction(const Api& api, const VkCommandBuffer commands, Parameter& parameters,
        const RayReconstructionFeature& create, Handle** feature)
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
        parameters.Set(sDenoiseMode, sDenoiseModeUnified);
        parameters.Set(sRoughnessMode, sRoughnessPacked);
        parameters.Set(sUseHardwareDepth, sDepthHardware);

        return api.CreateFeature(commands, Feature::RayReconstruction, &parameters, feature);
    }

    Result evaluateRayReconstruction(const Api& api, const VkCommandBuffer commands, const Handle* feature,
        Parameter& parameters, const RayReconstructionFrame& frame)
    {
        // Every `Resource*` through the `void*` slot, which is the one NGX's Vulkan path reads.
        const auto resource = [](Resource* value) { return static_cast<void*>(value); };
        parameters.Set(sColor, resource(frame.mColour));
        parameters.Set(sOutput, resource(frame.mOutput));
        parameters.Set(sDepth, resource(frame.mDepth));
        parameters.Set(sMotionVectors, resource(frame.mMotion));
        parameters.Set(sDiffuseAlbedo, resource(frame.mDiffuseAlbedo));
        parameters.Set(sSpecularAlbedo, resource(frame.mSpecularAlbedo));
        parameters.Set(sNormals, resource(frame.mNormalRoughness));
        parameters.Set(sJitterX, frame.mJitterX);
        parameters.Set(sJitterY, frame.mJitterY);
        parameters.Set(sReset, frame.mReset ? 1 : 0);
        parameters.Set(sFrameTimeDelta, frame.mFrameMs);
        // Each frame though no frame moves it: a feature `DlssUpscaler::measure` made at another extent
        // creates on the same map, so the extent is the evaluated feature's only where its evaluation sets it.
        parameters.Set(sRenderSubrectWidth, frame.mRender.width);
        parameters.Set(sRenderSubrectHeight, frame.mRender.height);

        return api.EvaluateFeature(commands, feature, &parameters, nullptr);
    }

    void setRayReconstructionConstants(Parameter& parameters)
    {
        // Every `Resource*` through the `void*` slot, which is the one NGX's Vulkan path reads.
        void* const none = nullptr;

        // **What the renderer never varies**: motion in render pixels, the unit the trace writes it
        // in; colour as linear radiance at its own scale, which RTX Remix hands RR too, since RR
        // supports no exposure of its own (DLSS-RR Integration Guide, 3.7); and the roughness in the
        // normals' fourth (`sRoughnessPacked`), so its own slot is null.
        parameters.Set(sMotionVectorScaleX, 1.0f);
        parameters.Set(sMotionVectorScaleY, 1.0f);
        parameters.Set(sPreExposure, 1.0f);
        parameters.Set(sExposureScale, 1.0f);
        parameters.Set(sRoughness, none);

        for (const char* const name : sUnusedResources)
            parameters.Set(name, none);
        for (const char* const name : sZeroWholes)
            parameters.Set(name, 0u);
        for (const char* const name : sZeroSigned)
            parameters.Set(name, 0);
    }

    Result optimalSettings(
        Parameter& capabilities, const VkExtent2D output, const Quality quality, OptimalSettings& into)
    {
        const QueryCallback callback = callbackOf(capabilities, sOptimalSettingsCallback);
        if (callback == nullptr)
            return Result::Failure;

        capabilities.Set(sWidth, output.width);
        capabilities.Set(sHeight, output.height);
        capabilities.Set(sPerfQualityValue, static_cast<int>(quality));
        // "Some older DLSS dlls still expect this value to be set", as the helper has it.
        capabilities.Set(sRtxValue, 0);
        const Result asked = callback(&capabilities);
        if (failed(asked))
            return asked;

        capabilities.Get(sOutWidth, &into.mOptimal.width);
        capabilities.Get(sOutHeight, &into.mOptimal.height);
        // An older library names no range: the optimal extent is both ends, as the helper reads it.
        into.mMax = into.mOptimal;
        into.mMin = into.mOptimal;
        capabilities.Get(sDynamicMaxWidth, &into.mMax.width);
        capabilities.Get(sDynamicMaxHeight, &into.mMax.height);
        capabilities.Get(sDynamicMinWidth, &into.mMin.width);
        capabilities.Get(sDynamicMinHeight, &into.mMin.height);
        return asked;
    }

    Result statsBytes(Parameter& capabilities, unsigned long long& into)
    {
        const QueryCallback callback = callbackOf(capabilities, sStatsCallback);
        if (callback == nullptr)
            return Result::Failure;

        const Result asked = callback(&capabilities);
        if (failed(asked))
            return asked;
        return capabilities.Get(sSizeInBytes, &into);
    }
}
