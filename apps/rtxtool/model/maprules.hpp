#pragma once

#include <array>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace RtxTool
{
    /// What a model's companion maps are called: the shipped name patterns, which every run but the played
    /// one takes, so two machines with the same content trace one scene; the played game takes the
    /// player's (an empty `std::optional<MapRules>`). What a `_spec` map's channels mean is measured on
    /// each map (`Rtx::specularLayoutOf`).
    enum class MapRules
    {
        Shipped,
    };

    /// How a `MapRules` is spelled in a report.
    inline constexpr Rtx::NamedEnum sMapRulesNames{ std::array{
        std::pair{ MapRules::Shipped, std::string_view("shipped") },
    } };
}
