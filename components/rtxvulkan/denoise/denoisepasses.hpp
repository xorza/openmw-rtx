#pragma once

#include <optional>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/shaders/visibility.h>

#include "accumulatepass.hpp"
#include "atrouspass.hpp"
#include "denoised.hpp"
#include "historyclamppass.hpp"
#include "shadowpass.hpp"
#include "temporallayerpass.hpp"

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
    /// **Five passes and one statement of how they meet.** The accumulator averages the bounce over time
    /// and hands the cascade its variance; the shadow denoiser filters the sky's bit where the sky has a
    /// source that lights, and the lamps' where the scene has a lamp; the glossy filter averages the lobe's
    /// light where a surface wears a map; the pane filter averages the layers' drawn light; and the wavelet
    /// spreads the bounce across the screen. The shadow denoiser and the glossy filter read the held
    /// surface the accumulator's history belongs to (`GBuffer::getHeld`).
    class DenoisePasses
    {
    public:
        /// @param masks whether the upscaler reads the masks: `AtrousPass` says what it changes.
        DenoisePasses(const Device& device, UpscaleMasks masks);

        /// What the frame asks of the denoisers.
        struct Request
        {
            /// The camera the trace sampled: both eyes, the previous basis and the far plane.
            const Shaders::VisibilityConstants& mSampled;

            /// `TraceSubject::mMapped`: whether any surface of the frame has a lobe.
            bool mMapped;

            /// `TraceSubject::mLamps`: whether the scene holds a lamp.
            bool mLamps;

            /// What puts the frame back together, of which this reads the accumulator's and the
            /// wavelet's switches.
            const Reconstruction& mReconstruction;
        };

        /// Records every pass over `buffer`, which must have been handed over, and hands back where
        /// the light ended up, which the composite composes: a frame that sums, or one where water may
        /// split a pixel.
        ///
        /// @param timer null where the run is not being timed, which a picture is not.
        Denoised record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
            const Request& request, GpuTimer* timer) const;

        /// `record`, composing the frame into `composed` in the wavelet's last level, from
        /// `CHANNEL_DIRECT` and the filters' answers: the composite's work, done where the cascade
        /// ends, for a frame nothing sums. `composed`, writable by compute, is then left as
        /// `Use::sComputeWrite`, for the caller to order for what reads it next.
        void recordComposing(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
            const Request& request, const Image& composed, GpuTimer* timer) const;

    private:
        /// The passes of `record` and of `recordComposing`, which hands `composed`; the answer where
        /// nothing composed.
        std::optional<Denoised> recordAll(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
            const Request& request, const Image* composed, GpuTimer* timer) const;

        /// Holds `layer`'s mean to its fast means by the history clamp, behind a dependency on the
        /// layer's dispatch the caller records, and hands back the filtered light as the clamp left it.
        const Image& clampLayer(VkCommandBuffer commands, const TemporalLayerPass& layer,
            const DenoiseHistory::LayerImages& images, const GBuffer& buffer, const DenoiseFrame& frame) const;

        AccumulatePass mAccumulate;
        ShadowPass mShadow;
        HistoryClampPass mHistoryClamp;
        TemporalLayerPass mSpecular;
        TemporalLayerPass mPane;
        AtrousPass mFilter;
    };
}
