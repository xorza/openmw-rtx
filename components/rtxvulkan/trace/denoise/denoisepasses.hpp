#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/visibility.h>

#include "accumulatepass.hpp"
#include "atrouspass.hpp"
#include "denoised.hpp"
#include "panepass.hpp"
#include "shadowpass.hpp"
#include "specularpass.hpp"

namespace Rtx
{
    class Device;
    class DenoiseHistory;
    class GBuffer;
    class GpuTimer;

    /// The denoiser: every pass it runs, one set of pipelines for every chain, and the order a frame
    /// runs them in. What differs between chains is the history each keeps — `DenoiseHistory` —
    /// which a frame hands in.
    ///
    /// **Five passes and one statement of how they meet.** The accumulator averages the bounce over
    /// time and hands the cascade its variance; the shadow denoiser filters the sky's source's bit
    /// where the sky has a source that lights; the glossy filter averages the lobe's light where a
    /// surface wears a map; the pane filter averages the layers' drawn light; and the wavelet spreads
    /// the bounce across the screen. The shadow denoiser and the glossy filter read the surface the
    /// accumulator's history belongs to (`AccumulateImages::mSurfaceBefore`).
    class DenoisePasses
    {
    public:
        DenoisePasses(const Device& device, const std::filesystem::path& shaderDirectory);

        /// Records every pass over `buffer`, which must have been handed over, and hands back where
        /// the light ended up.
        ///
        /// @param sampled the camera the trace sampled: both eyes, the previous basis and the far
        ///        plane.
        /// @param mapped `TraceSubject::mMapped`: whether any surface of the frame has a lobe.
        /// @param timer null where the run is not being timed, which a picture is not.
        Denoised record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
            const Shaders::VisibilityConstants& sampled, bool mapped, GpuTimer* timer) const;

    private:
        AccumulatePass mAccumulate;
        ShadowPass mShadow;
        SpecularPass mSpecular;
        PanePass mPane;
        AtrousPass mFilter;
    };
}
