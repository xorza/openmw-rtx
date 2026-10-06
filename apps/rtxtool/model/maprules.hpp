#pragma once

#include <array>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace RtxTool
{
    /// Which companion maps a model takes, as the line names them: whether it looks for a normal and
    /// a specular map, what they are called, and what a specular map's channels mean. Named, never
    /// read from the player's `settings.cfg`, so two machines with the same content trace one scene.
    enum class MapRules
    {
        /// `settings-default.cfg`'s, which look for none.
        Shipped,

        /// Both maps, by the shipped name patterns, and a `_spec` map read as OpenMW's own layout
        /// (`Rtx::SpecularLayout::Classic`).
        Classic,

        /// Both maps, by the shipped name patterns, and a `_spec` map read as the PBR packs' layout
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
