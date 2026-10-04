#pragma once

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
    struct Reconstruction;

    /// The denoiser: every pass it runs, one set of pipelines for every chain, and the order a frame
    /// runs them in. What differs between chains is the history each keeps — `DenoiseHistory` —
    /// which a frame hands in.
    ///
    /// **Five passes and one statement of how they meet.** The accumulator averages the bounce over
    /// time and hands the cascade its variance; the shadow denoiser filters the shadowed sources'
    /// bit where the sky has a source that lights or the scene a lamp; the glossy filter averages
    /// the lobe's light where a surface wears a map; the pane filter averages the layers' drawn
    /// light; and the wavelet spreads the bounce across the screen. The shadow denoiser and the
    /// glossy filter read the surface the accumulator's history belongs to
    /// (`AccumulateImages::mSurfaceBefore`). **Where the frame takes no indirect light**, the
    /// accumulator keeps that surface alone, and neither its mean nor the wavelet runs.
    class DenoisePasses
    {
    public:
        explicit DenoisePasses(const Device& device);

        /// Records every pass over `buffer`, which must have been handed over, and hands back where
        /// the light ended up.
        ///
        /// @param sampled the camera the trace sampled: both eyes, the previous basis and the far
        ///        plane.
        /// @param mapped `TraceSubject::mMapped`: whether any surface of the frame has a lobe.
        /// @param lamps `TraceSubject::mLamps`: whether the scene holds a lamp.
        /// @param reconstruction what puts the frame back together, of which this reads whether the
        ///        bounce is filtered (`Reconstruction::filtersBounce`) and its clamp.
        /// @param timer null where the run is not being timed, which a picture is not.
        Denoised record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
            const Shaders::VisibilityConstants& sampled, bool mapped, bool lamps, const Reconstruction& reconstruction,
            GpuTimer* timer) const;

    private:
        AccumulatePass mAccumulate;
        ShadowPass mShadow;
        SpecularPass mSpecular;
        PanePass mPane;
        AtrousPass mFilter;
    };
}
