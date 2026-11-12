#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/platform/library.hpp>

/// **The NGX Vulkan interface, declared here and not taken from NVIDIA's SDK**, which is proprietary and
/// may not be linked into or shipped with a GPL program. These are independent declarations of the public
/// binary interface, pinned to NGX API 1.5.0 as the SDK's 310.9.16 headers state it: the driver's NGX core
/// is opened at run time, and the feature library the player supplies sits where NGX looks for it. The
/// source of truth is the NGX API, not this file.
namespace Rtx::Dlss::Ngx
{
    /// The interface version the discovery record and the initialisation call state; NGX refuses a
    /// record from a later one.
    inline constexpr int sApiVersion = 0x0000015;

    /// The SDK's success is one and every failure shares the top twelve bits, which is all a
    /// caller needs: the low bits say which failure and go into the log unchanged.
    enum class Result : std::uint32_t
    {
        Success = 0x1,
        Failure = 0xBAD00000,

        /// What a core answers for a call it has no implementation of for the feature asked about.
        NotImplemented = 0xBAD00012,
    };

    constexpr bool failed(const Result result)
    {
        return (static_cast<std::uint32_t>(result) & 0xFFF00000u) == static_cast<std::uint32_t>(Result::Failure);
    }

    constexpr bool succeeded(const Result result)
    {
        return !failed(result);
    }

    /// The one feature this backend makes: Ray Reconstruction, which NVIDIA's guide says overrides
    /// Super Resolution wherever it runs (DLSS-RR Integration Guide, 3.2).
    enum class Feature : int
    {
        RayReconstruction = 13,
    };

    enum class Engine : int
    {
        Custom = 0,
    };

    /// NGX's own names for the ratios; the renderer's modes are ratios of their own, and the
    /// nearest name is only what the feature's default preset and memory budget are drawn from.
    enum class Quality : int
    {
        MaxPerformance = 0,
        Balanced = 1,
        MaxQuality = 2,
        UltraPerformance = 3,
        Dlaa = 5,
    };

    enum class ApplicationKind : int
    {
        Application = 0,
        Project = 1,
    };

    enum class ResourceKind : int
    {
        ImageView = 0,
        Buffer = 1,
    };

    constexpr int sDlssFlagIsHdr = 1 << 0;
    constexpr int sDlssFlagMotionVectorsLowRes = 1 << 1;
    constexpr int sDlssFlagDepthInverted = 1 << 3;

    /// `DLSS.Denoise.Mode`'s one value Ray Reconstruction takes: the learned denoiser and upscaler in one.
    constexpr int sDenoiseModeUnified = 1;

    /// `DLSS.Roughness.Mode`: the roughness read from the normals' fourth component, and not an image
    /// of its own.
    constexpr unsigned int sRoughnessPacked = 1;

    /// `DLSS.Use.HW.Depth`: the depth is a device depth, as a rasterizer writes it, and not a view one.
    constexpr unsigned int sDepthHardware = 1;

    /// A feature NGX created. Its contents are NGX's; the pointer is only ever handed back.
    struct Handle;

    enum class LoggingLevel : int
    {
        Off = 0,
        On = 1,
        Verbose = 2,
        Count = 3,
    };

    struct PathListInfo
    {
        const wchar_t* const* Path;
        unsigned int Length;
    };

    struct LoggingInfo
    {
        void (*LoggingCallback)(const char*, LoggingLevel, Feature);
        LoggingLevel MinimumLoggingLevel;
        bool DisableOtherLoggingSinks;
    };

    /// The extra folders NGX searches for a feature library, besides the application's own: the
    /// launcher downloads into the user data folder, the only writable place an AppImage can name.
    struct FeatureCommonInfo
    {
        PathListInfo PathList;
        void* InternalData;
        LoggingInfo Logging;
    };

    struct ProjectDescription
    {
        const char* ProjectId;
        Engine EngineType;
        const char* EngineVersion;
    };

    struct ApplicationIdentifier
    {
        ApplicationKind IdentifierType;
        union
        {
            ProjectDescription Project;
            unsigned long long ApplicationId;
        } Value;
    };

    struct FeatureDiscoveryInfo
    {
        int SDKVersion;
        Feature FeatureId;
        ApplicationIdentifier Identifier;
        const wchar_t* ApplicationDataPath;
        const FeatureCommonInfo* FeatureInfo;
    };

    struct ImageViewInfo
    {
        VkImageView ImageView;
        VkImage Image;
        VkImageSubresourceRange SubresourceRange;
        VkFormat Format;
        unsigned int Width;
        unsigned int Height;
    };

    struct BufferInfo
    {
        VkBuffer Buffer;
        unsigned int SizeInBytes;
    };

    struct Resource
    {
        union
        {
            ImageViewInfo ImageView;
            BufferInfo Buffer;
        } Content;
        ResourceKind Type;
        bool ReadWrite;
    };

    /// The `ID3D11Resource` and `ID3D12Resource` slots of the parameter map's vtable: never called, but
    /// the two slots before the `void*` setter and getter must keep their places.
    struct D3D11Resource;
    struct D3D12Resource;

    /// **The parameter map's vtable, in the order NGX lays it out.** Every `Set` and `Get`
    /// overload is a slot even where this backend does not use it, because a slot left out moves
    /// every one after it.
    class Parameter
    {
    public:
        virtual void Set(const char* name, unsigned long long value) = 0;
        virtual void Set(const char* name, float value) = 0;
        virtual void Set(const char* name, double value) = 0;
        virtual void Set(const char* name, unsigned int value) = 0;
        virtual void Set(const char* name, int value) = 0;
        virtual void Set(const char* name, D3D11Resource* value) = 0;
        virtual void Set(const char* name, D3D12Resource* value) = 0;
        virtual void Set(const char* name, void* value) = 0;

        virtual Result Get(const char* name, unsigned long long* value) const = 0;
        virtual Result Get(const char* name, float* value) const = 0;
        virtual Result Get(const char* name, double* value) const = 0;
        virtual Result Get(const char* name, unsigned int* value) const = 0;
        virtual Result Get(const char* name, int* value) const = 0;
        virtual Result Get(const char* name, D3D11Resource** value) const = 0;
        virtual Result Get(const char* name, D3D12Resource** value) const = 0;
        virtual Result Get(const char* name, void** value) const = 0;

        virtual void Reset() = 0;

    protected:
        /// The driver owns the map and destroys it through `DestroyParameters`; a protected destructor
        /// adds no virtual slot the driver's vtable lacks.
        ~Parameter() = default;
    };

    // **The layouts are the interface.** These are the sizes and offsets the NGX 1.5.0 headers
    // describe, checked once at compile time so a moved or retyped field cannot silently shift
    // every field after it. The interface is 64-bit; a 32-bit build has no DLSS.
    static_assert(sizeof(void*) == 8, "NGX's Vulkan interface is declared for 64-bit targets");
    static_assert(sizeof(FeatureDiscoveryInfo) == 56);
    static_assert(offsetof(FeatureDiscoveryInfo, Identifier) == 8);
    static_assert(offsetof(FeatureDiscoveryInfo, ApplicationDataPath) == 40);
    static_assert(sizeof(PathListInfo) == 16);
    static_assert(offsetof(PathListInfo, Length) == 8);
    static_assert(sizeof(LoggingInfo) == 16);
    static_assert(offsetof(LoggingInfo, MinimumLoggingLevel) == 8);
    static_assert(offsetof(LoggingInfo, DisableOtherLoggingSinks) == 12);
    static_assert(sizeof(FeatureCommonInfo) == 40);
    static_assert(offsetof(FeatureCommonInfo, InternalData) == 16);
    static_assert(offsetof(FeatureCommonInfo, Logging) == 24);
    static_assert(sizeof(ApplicationIdentifier) == 32);
    static_assert(sizeof(ImageViewInfo) == 48);
    static_assert(sizeof(Resource) == 56);
    static_assert(offsetof(Resource, Content) == 0);
    static_assert(offsetof(Resource, Type) == 48);
    static_assert(offsetof(Resource, ReadWrite) == 52);
    static_assert(sizeof(Parameter) == sizeof(void*));

    /// The entry points this backend calls, resolved from the driver's NGX core. The types are
    /// the interface's and the names are only what `Platform::Library::find` asks the driver for.
    struct Api
    {
        Platform::Library::ScopedHandle mLibrary;

        Result (*GetFeatureInstanceExtensionRequirements)(
            const FeatureDiscoveryInfo*, std::uint32_t*, VkExtensionProperties**) = nullptr;
        Result (*GetFeatureDeviceExtensionRequirements)(VkInstance, VkPhysicalDevice, const FeatureDiscoveryInfo*,
            std::uint32_t*, VkExtensionProperties**) = nullptr;
        Result (*InitWithProjectId)(const char*, Engine, const char*, const wchar_t*, VkInstance, VkPhysicalDevice,
            VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, int, const FeatureCommonInfo*) = nullptr;
        Result (*GetCapabilityParameters)(Parameter**) = nullptr;
        Result (*AllocateParameters)(Parameter**) = nullptr;
        Result (*DestroyParameters)(Parameter*) = nullptr;
        /// The core's own signature and not the SDK's `Shutdown1(VkDevice)`, as `nvsdk_ngx_vk.h`
        /// warns of: the core writes how many devices still hold NGX through the second argument,
        /// and a caller that passes none has it write through whatever its register held.
        Result (*Shutdown)(VkDevice, int*) = nullptr;
        Result (*CreateFeature)(VkCommandBuffer, Feature, Parameter*, Handle**) = nullptr;
        Result (*EvaluateFeature)(VkCommandBuffer, const Handle*, const Parameter*, void*) = nullptr;
        Result (*ReleaseFeature)(Handle*) = nullptr;
        Result (*RequiredExtensions)(unsigned int*, const char***, unsigned int*, const char***) = nullptr;
    };

    /// The driver's core, loaded once with every entry point above resolved, or null where the
    /// machine has none or its core lacks one. The feature library beside the executable is not
    /// opened here: NGX finds it itself, and creating a feature says whether it did.
    const Api* api();

    /// The parameter names the interface publishes, and the values this backend sets. They are
    /// string keys, not C++ identifiers: NGX matches them byte for byte.
    inline constexpr const char* sRayReconstructionAvailable = "SuperSamplingDenoising.Available";
    inline constexpr const char* sRayReconstructionNeedsUpdatedDriver = "SuperSamplingDenoising.NeedsUpdatedDriver";
    inline constexpr const char* sRayReconstructionMinDriverMajor = "SuperSamplingDenoising.MinDriverVersionMajor";
    inline constexpr const char* sRayReconstructionMinDriverMinor = "SuperSamplingDenoising.MinDriverVersionMinor";
    inline constexpr const char* sRayReconstructionFeatureInitResult = "SuperSamplingDenoising.FeatureInitResult";
    inline constexpr const char* sOptimalSettingsCallback = "DLSSDOptimalSettingsCallback";
    inline constexpr const char* sStatsCallback = "DLSSDGetStatsCallback";
    inline constexpr const char* sFreeMemoryOnRelease = "FreeMemOnReleaseFeature";
    inline constexpr const char* sCreationNodeMask = "CreationNodeMask";
    inline constexpr const char* sVisibilityNodeMask = "VisibilityNodeMask";
    inline constexpr const char* sWidth = "Width";
    inline constexpr const char* sHeight = "Height";
    inline constexpr const char* sOutWidth = "OutWidth";
    inline constexpr const char* sOutHeight = "OutHeight";
    inline constexpr const char* sPerfQualityValue = "PerfQualityValue";
    inline constexpr const char* sRtxValue = "RTXValue";
    inline constexpr const char* sDynamicMaxWidth = "DLSS.Get.Dynamic.Max.Render.Width";
    inline constexpr const char* sDynamicMaxHeight = "DLSS.Get.Dynamic.Max.Render.Height";
    inline constexpr const char* sDynamicMinWidth = "DLSS.Get.Dynamic.Min.Render.Width";
    inline constexpr const char* sDynamicMinHeight = "DLSS.Get.Dynamic.Min.Render.Height";
    inline constexpr const char* sSizeInBytes = "SizeInBytes";
    inline constexpr const char* sFeatureCreateFlags = "DLSS.Feature.Create.Flags";
    inline constexpr const char* sEnableOutputSubrects = "DLSS.Enable.Output.Subrects";
    inline constexpr const char* sDenoiseMode = "DLSS.Denoise.Mode";
    inline constexpr const char* sRoughnessMode = "DLSS.Roughness.Mode";
    inline constexpr const char* sUseHardwareDepth = "DLSS.Use.HW.Depth";
    inline constexpr const char* sColor = "Color";
    inline constexpr const char* sOutput = "Output";
    inline constexpr const char* sDepth = "Depth";
    inline constexpr const char* sMotionVectors = "MotionVectors";
    inline constexpr const char* sDiffuseAlbedo = "DLSS.Input.DiffuseAlbedo";
    inline constexpr const char* sSpecularAlbedo = "DLSS.Input.SpecularAlbedo";
    inline constexpr const char* sNormals = "GBuffer.Normals";
    inline constexpr const char* sRoughness = "GBuffer.Roughness";
    inline constexpr const char* sJitterX = "Jitter.Offset.X";
    inline constexpr const char* sJitterY = "Jitter.Offset.Y";
    inline constexpr const char* sReset = "Reset";
    inline constexpr const char* sMotionVectorScaleX = "MV.Scale.X";
    inline constexpr const char* sMotionVectorScaleY = "MV.Scale.Y";
    inline constexpr const char* sPreExposure = "DLSS.Pre.Exposure";
    inline constexpr const char* sExposureScale = "DLSS.Exposure.Scale";
    inline constexpr const char* sFrameTimeDelta = "FrameTimeDeltaInMsec";
    inline constexpr const char* sRenderSubrectWidth = "DLSS.Render.Subrect.Dimensions.Width";
    inline constexpr const char* sRenderSubrectHeight = "DLSS.Render.Subrect.Dimensions.Height";

    /// How NGX names the images and buffers it makes for a feature (`nv.ngx.dlssd.resource` among them):
    /// NGX records its own barriers and dispatches over them, so none are the renderer's to synchronize.
    inline constexpr std::string_view sOwnResourcePrefix = "nv.ngx.";

    /// What a Ray Reconstruction feature is made for: the fork's own record, which the parameter map is
    /// set from by name, so no layout of the SDK's helper structures is relied on.
    struct RayReconstructionFeature
    {
        VkExtent2D mRender{};
        VkExtent2D mOutput{};
        Quality mQuality = Quality::MaxQuality;
        int mFlags = 0;
    };

    /// What one evaluation reads: the noisy frame, its guides and the output, the jitter as NGX names
    /// it (the image's displacement, in render pixels), whether the history is worthless, and the
    /// frame's step.
    struct RayReconstructionFrame
    {
        Resource* mColour = nullptr;
        Resource* mOutput = nullptr;
        Resource* mDepth = nullptr;
        Resource* mMotion = nullptr;
        Resource* mDiffuseAlbedo = nullptr;
        Resource* mSpecularAlbedo = nullptr;
        Resource* mNormalRoughness = nullptr;
        float mJitterX = 0.0f;
        float mJitterY = 0.0f;
        VkExtent2D mRender{};
        bool mReset = false;
        float mFrameMs = 0.0f;
    };

    /// The traced extents NGX deems right for a mode at an output, and the range it takes: what
    /// `NGX_DLSSD_GET_OPTIMAL_SETTINGS` answers. A library that names no range has the optimal extent
    /// for both ends, as the SDK's helper reads it.
    struct OptimalSettings
    {
        VkExtent2D mOptimal{};
        VkExtent2D mMin{};
        VkExtent2D mMax{};
    };

    /// Sets the parameter map up as the SDK's Ray Reconstruction create helper does
    /// (`NGX_VULKAN_CREATE_DLSSD_EXT1`), then creates the feature on `commands`.
    Result createRayReconstruction(const Api& api, VkCommandBuffer commands, Parameter& parameters,
        const RayReconstructionFeature& create, Handle** feature);

    /// Sets once, on the map every evaluation is handed, each key the SDK's evaluate helper
    /// (`NGX_VULKAN_EVALUATE_DLSSD_EXT`) sets to a value this backend never varies. **Once, not each frame
    /// as the helper does**: the map is the backend's own and nothing else writes these keys, so a frame
    /// sets only the dozen keys that move.
    void setRayReconstructionConstants(Parameter& parameters);

    /// Sets the keys of one evaluation that move from frame to frame — the images, the jitter, the reset,
    /// the frame's step and the feature's extent — over a map `setRayReconstructionConstants` set up, then
    /// evaluates `feature` on `commands`.
    Result evaluateRayReconstruction(const Api& api, VkCommandBuffer commands, const Handle* feature,
        Parameter& parameters, const RayReconstructionFrame& frame);

    /// Asks `capabilities`, a map `GetCapabilityParameters` made, NGX's optimal traced extents for
    /// `quality` at `output`, as `NGX_DLSSD_GET_OPTIMAL_SETTINGS` does. Fails where the library publishes
    /// no such callback.
    Result optimalSettings(Parameter& capabilities, VkExtent2D output, Quality quality, OptimalSettings& into);

    /// Asks `capabilities` how much device memory Ray Reconstruction holds, as `NGX_DLSSD_GET_STATS` does.
    /// Fails where the library publishes no such callback.
    Result statsBytes(Parameter& capabilities, unsigned long long& into);
}
