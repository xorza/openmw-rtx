#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
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

        /// The five dispatches over the field `images` names, one a step, each behind a dependency on
        /// everything the step before read and wrote, which the caller records between the steps of
        /// every field and family that runs beside it (`DenoisePasses::record`). The last level
        /// leaves the field's filtered visibility, its mean in `r`, in `images.mVisibility`. `buffer`
        /// must have been handed over, and `DenoiseHistory::discard` has readied the images.
        ///
        /// The rays' bits packed, and the widest penumbra of each tile.
        void recordMask(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// The temporal blend and the tiles' classification.
        void recordTiles(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// One of the spatial filter's `SHADOW_FILTER_LEVELS`, in order, over every field whose bit
        /// `filtering` sets (`SHADOW_FIELD_*`), one dispatch reading the surface once for all of them.
        /// At least one. `fields` holds each field's images at its index, the ones not filtered as
        /// well, which the module names all the same.
        void recordLevel(VkCommandBuffer commands, std::uint32_t level,
            const std::array<const DenoiseHistory::ShadowImages*, sShadowFields>& fields, std::uint32_t filtering,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::ShadowMaskConstants> mMask;
        ComputePipeline<Shaders::ShadowTilesConstants> mTiles;
        /// A level's module for each set of fields it filters, by the set's bits less one
        /// (`SHADOW_SPEC_FIELDS`): the sky's, the lamps' and both.
        using FieldFilters
            = std::array<ComputePipeline<Shaders::ShadowFilterConstants>, (1u << Shaders::SHADOW_FIELD_COUNT) - 1u>;
        std::array<FieldFilters, Shaders::SHADOW_FILTER_LEVELS> mFilters;
    };
}
