#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/image/textureencoding.hpp>

namespace Rtx
{
    /// What a `_spec` map's channels mean. The file cannot say: OpenMW documents a classic layout
    /// (highlight colour in RGB, glossiness in A), the PBR packs this renderer is made to read use
    /// another, and one install can hold both. So the player states it, `[RTX] specular map layout`.
    /// Content with no `_spec` map, vanilla's, draws the same under either.
    enum class SpecularLayout : std::uint8_t
    {
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
        std::pair{ SpecularLayout::Classic, std::string_view("classic") },
        std::pair{ SpecularLayout::MetalRoughness, std::string_view("metal roughness") },
    } };

    /// What a ground layer's diffuse is bound as: an authored albedo where it is a `_diffusespec` read in
    /// the metal-roughness layout, and a painted colour, delit on the host, otherwise.
    constexpr TextureEncoding layerEncodingOf(const bool diffuseSpec, const SpecularLayout layout)
    {
        return diffuseSpec && layout == SpecularLayout::MetalRoughness ? TextureEncoding::Colour
                                                                       : TextureEncoding::Painted;
    }

    /// The layouts the launcher and the settings window offer, in `sSpecularLayoutNames`' order: a
    /// layout added there stops the build until each menu gives it a label.
    inline constexpr std::array<SpecularLayout, sSpecularLayoutNames.mNames.size()> sSpecularLayoutMenu
        = sSpecularLayoutNames.values();
}
