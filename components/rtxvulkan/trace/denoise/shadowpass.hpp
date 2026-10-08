#pragma once

#include <array>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>
#include <components/rtxvulkan/shaders/shared/shadow.h>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;
    class Image;

    /// The shadow denoiser: a port of AMD's FidelityFX Shadow Denoiser over the bit a pixel kept of
    /// its ray to the sun or a moon, `CHANNEL_SHADOWED`'s alpha, and apart from it over the bit of
    /// its ray to a lamp, `CHANNEL_LAMPED`'s (`ShadowField`). A pass that packs the bits, a temporal
    /// pass that also classifies the tiles every receiver of which is lit alike, and three levels
    /// of a spatial filter over the rest — `shadowtiles.comp` and `shadowfilter.comp` say what the
    /// port keeps and what it changes. It runs where the wavelet does, once for the sky where it
    /// has a source that lights and once for the lamps where the scene has one: a field a source,
    /// for the reason `CHANNEL_SHADOWED` gives.
    class ShadowPass
    {
    public:
        explicit ShadowPass(const Device& device);

        /// Records the five dispatches over the field `images` names and hands back its filtered
        /// visibility, its mean in `r`, as the last level wrote it: the caller orders it for a read,
        /// beside the passes that run alongside (`DenoisePasses::record`), and times it. `buffer`
        /// must have been handed over, and `DenoiseHistory::discard` has readied the images.
        const Image& record(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::ShadowMaskConstants> mMask;
        ComputePipeline<Shaders::HistoryConstants> mTiles;
        std::array<ComputePipeline<Shaders::ShadowFilterConstants>, Shaders::SHADOW_FILTER_LEVELS> mFilters;
    };
}
