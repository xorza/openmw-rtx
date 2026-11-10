#pragma once

#include <cstdint>

#include "textureencoding.hpp"

namespace Rtx
{
    /// What a `_spec` map's channels mean. The file does not say, and one install can hold both, so
    /// each map is measured (`specularLayoutOf`): OpenMW documents a classic layout, a highlight colour
    /// in grey RGB and glossiness in A, and the PBR packs this renderer reads another. A classic map is
    /// converted with its diffuse to the other on the host (`docs/rtx/architecture.md`, "One specular layout,
    /// a classic map converted"), so the trace reads one.
    enum class SpecularLayout : std::uint8_t
    {
        /// OpenMW's own: the highlight colour in RGB, the reflectance at normal incidence as the
        /// specular-glossiness workflow reads it (glTF's `KHR_materials_pbrSpecularGlossiness`), and the
        /// Blinn-Phong exponent over 255 in A.
        Classic,

        /// R metalness, G perceptual roughness, B ambient occlusion, A one less subsurface scattering:
        /// the layout of Wareya's and Rafael's PBR shaders and of the packs made for them.
        MetalRoughness,
    };

    /// What a ground layer's diffuse is bound as: an authored albedo with its roughness in alpha where
    /// it is a `_diffusespec`, as the PBR packs ship every one, and a painted colour, delit on the host,
    /// otherwise. A `_diffusespec` holds a colour in either layout, so no measure tells a classic one
    /// apart, and no install read here holds one (`docs/rtx/architecture.md`, "One specular layout, a classic
    /// map converted").
    constexpr TextureEncoding layerEncodingOf(const bool diffuseSpec)
    {
        return diffuseSpec ? TextureEncoding::Colour : TextureEncoding::Painted;
    }
}
