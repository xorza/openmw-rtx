#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// What a `_spec` map's channels mean. The file cannot say: OpenMW documents a classic layout
    /// (highlight colour in RGB, glossiness in A), the PBR packs this renderer is made to read use
    /// another, and one install can hold both. So the player states it, `[RTX] specular map layout`.
    enum class SpecularLayout : std::uint8_t
    {
        /// Read no specular map at all.
        Ignore,

        /// OpenMW's own: the highlight colour in RGB, read as the reflectance at normal incidence
        /// as the specular-glossiness workflow reads it (glTF's
        /// `KHR_materials_pbrSpecularGlossiness`), and the Blinn-Phong exponent over 255 in A,
        /// read as a roughness by `Shaders::roughnessOfExponent`. The diffuse stays the classic
        /// picture of a lit surface, delit as a vanilla one is.
        Classic,

        /// R metalness, G perceptual roughness, B ambient occlusion, A one less subsurface scattering:
        /// the layout of Wareya's and Rafael's PBR shaders and of the packs made for them.
        MetalRoughness,
    };

    inline constexpr NamedEnum sSpecularLayoutNames{ std::array{
        std::pair{ SpecularLayout::Ignore, std::string_view("ignore") },
        std::pair{ SpecularLayout::Classic, std::string_view("classic") },
        std::pair{ SpecularLayout::MetalRoughness, std::string_view("metal roughness") },
    } };
}
