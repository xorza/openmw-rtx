#pragma once

#include <cstdint>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The layers a temporal filter of its own averages beside the accumulator, each over the frames
    /// its own surface held still.
    enum class TemporalLayer
    {
        /// `CHANNEL_SPECULAR`, by ReLAX's rule for how far a view may turn against a lobe
        /// (`specular.comp`). It runs where the scene wears a map, the only place a surface has a lobe.
        Glossy,

        /// `CHANNEL_PANE`, the nearest see-through layer's light, against a history of the layer's own
        /// (`pane.comp`, which says why over time alone). It runs wherever the wavelet does.
        Pane,
    };

    /// What a layer's filter is: its kernel, the channels it reads, and the layer its clamp holds.
    struct TemporalLayerRow
    {
        std::string_view mModule;
        std::string_view mName;
        Channel mSampled;
        Channel mSurface;
        Channel mMotion;
        std::uint32_t mClampLayer;
    };

    /// **One pass for both layers**, from a row a layer: the two kernels bind the same eight images
    /// in one order, and their host side was one body written twice.
    class TemporalLayerPass
    {
    public:
        TemporalLayerPass(const Device& device, TemporalLayer layer);

        /// Records the one dispatch, whose mean the history clamp then holds to its fast means
        /// (`HistoryClampPass`, at `getRow().mClampLayer`). `buffer` must have been handed over, and
        /// `DenoiseHistory::discard` has readied the images.
        void record(VkCommandBuffer commands, const DenoiseHistory::LayerImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        const TemporalLayerRow& getRow() const { return mRow; }

    private:
        const TemporalLayerRow& mRow;
        ComputePipeline<Shaders::HistoryConstants> mPipeline;
    };
}
