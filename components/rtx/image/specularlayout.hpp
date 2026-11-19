#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a `_spec` map's channels mean. The file does not say, and one install can hold both, so
    /// each map is measured (`specularLayoutOf`): OpenMW documents a classic layout, a highlight colour
    /// in grey RGB and glossiness in A, and the PBR packs this renderer reads another. A classic map is
    /// converted with its diffuse to the other on the host (`docs/architecture.md`, "One specular layout,
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
}
