#include "rtxrun.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "rtxbackend.hpp"
#include "rtxsettings.hpp"

namespace MWRender
{
    namespace
    {
        /// Whether an environment variable is set to anything other than nothing or `0`.
        bool askedFor(const char* name)
        {
            const char* const value = std::getenv(name);
            return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
        }
    }

    std::string describe(const RunSetup& setup)
    {
        const Rtx::RunProfile& run = setup.mRun;
        const Rtx::ValidationOptions& validation = run.mValidation;
        return std::format(
            "backend {}, window {}, validation {}{}, markers {}, checkpoints {}, upscale {} by {}, "
            "kernels {}, content store {}, video memory {}, a build that {}validates by default",
            sRtxBackendNames.name(setup.mBackend), setup.mHeadless ? "hidden" : "shown",
            Rtx::sValidationNames.name(validation.mLevel), validation.mWatchSubmits ? " with every submit watched" : "",
            validation.mMarkers ? "on" : "off", validation.mCheckpoints ? "on" : "off",
            Rtx::sUpscaleNames.name(run.mProfile.mUpscale), Rtx::sUpscalerNames.name(run.mProfile.mUpscaler),
            setup.mKernels == Rtx::KernelSet::Drawing ? "drawing" : "every",
            setup.mContentStore.has_value() ? "on" : "off",
            run.mMemoryBudget.has_value() ? std::format("held to {} bytes", *run.mMemoryBudget)
                                          : std::string("as the device states it"),
            Rtx::sValidationByDefault ? "" : "never ");
    }

    Rtx::ValidationOptions askedValidation(Rtx::ValidationOptions validation)
    {
        if (askedFor("OPENMW_RTX_SYNC_VALIDATION"))
            validation.mLevel = std::max(validation.mLevel, Rtx::ValidationLevel::Sync);
        if (askedFor("OPENMW_RTX_GPU_VALIDATION"))
            validation.mLevel = Rtx::ValidationLevel::Gpu;
        validation.mWatchSubmits = validation.mWatchSubmits || askedFor("OPENMW_RTX_WATCH_SUBMITS");
        validation.mCheckpoints = validation.mCheckpoints || askedFor("OPENMW_RTX_BREADCRUMBS");
        return validation;
    }

    RtxBackend askedBackend()
    {
        const char* const named = std::getenv("OPENMW_RTX_BACKEND");
        if (named == nullptr || *named == '\0')
            return RtxBackend::Vulkan;
        return sRtxBackendNames.require(named, "a backend `OPENMW_RTX_BACKEND` names");
    }

    RunSetup playedRunSetup(const std::filesystem::path& cachePath)
    {
        const RtxSettings settings = RtxSettings::derive(RtxSettingValues::fromRegistry());
        const RtxBackend backend = askedBackend();

        Rtx::RenderProfile profile{
            .mExposure = Rtx::ExposureRule{},
            .mRadianceWidth = Rtx::RadianceWidth::Shown,
        };
        settings.applyTo(profile);

        return RunSetup{
            .mRun = {
                .mProfile = profile,
                .mValidation
                = { .mLevel = Rtx::sValidationByDefault ? Rtx::ValidationLevel::On : Rtx::ValidationLevel::Off },
            },
            .mMirror = settings.mMirror,
            .mBackend = backend,
            .mHeadless = backend == RtxBackend::Null,
            .mSettled = std::nullopt,
            .mContentStore
            = cachePath.empty() ? std::nullopt : std::optional(cachePath / Rtx::sContentStoreFolder),
        };
    }
}
