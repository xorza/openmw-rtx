#pragma once

#include <array>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/shadow.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;
    class Image;

    /// The shadow denoiser: a port of AMD's FidelityFX Shadow Denoiser over the one bit a pixel's
    /// rays to the sun or a moon came back with, `CHANNEL_SUNLIT`'s alpha. A pass that packs the
    /// bits, a temporal pass that also classifies the tiles every receiver of which is lit alike,
    /// and three levels of a spatial filter over the rest — `shadowtiles.comp` and
    /// `shadowfilter.comp` say what the port keeps and what it changes. It runs where the wavelet
    /// does and the sky has a source that lights.
    class ShadowPass
    {
    public:
        explicit ShadowPass(const Device& device);

        /// Records the five dispatches and hands back the filtered visibility, its mean in `r`,
        /// ordered for a compute read. `buffer` must have been handed over, and
        /// `DenoiseHistory::discard` has readied the images.
        const Image& record(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::ShadowMaskConstants> mMask;
        ComputePipeline<Shaders::ShadowTilesConstants> mTiles;
        std::array<ComputePipeline<Shaders::ShadowFilterConstants>, Shaders::SHADOW_FILTER_LEVELS> mFilters;
    };
}
