#pragma once

#include <array>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace RtxTool
{
    /// What a model's companion maps are called and what a specular map's channels mean, as the line
    /// names them; the ray tracer looks for both maps under every rule. Named, never read from the
    /// player's `settings.cfg`, so two machines with the same content trace one scene.
    enum class MapRules
    {
        /// `settings-default.cfg`'s layout, by the shipped name patterns.
        Shipped,

        /// The shipped name patterns, and a `_spec` map read as OpenMW's own layout
        /// (`Rtx::SpecularLayout::Classic`).
        Classic,

        /// The shipped name patterns, and a `_spec` map read as the PBR packs' layout
        /// (`Rtx::SpecularLayout::MetalRoughness`).
        MetalRoughness,
    };

    /// How a `MapRules` is spelled on a command line and in a report.
    inline constexpr Rtx::NamedEnum sMapRulesNames{ std::array{
        std::pair{ MapRules::Shipped, std::string_view("shipped") },
        std::pair{ MapRules::Classic, std::string_view("classic") },
        std::pair{ MapRules::MetalRoughness, std::string_view("metal-roughness") },
    } };
}
