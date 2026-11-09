#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// How a texture is addressed past its edges, along `s` and along `t`, as the content file
    /// states it on the texture and not on the image. Two bits, because the four combinations are
    /// what a sampler is made for; `Repeat` is what nearly everything states, and `Clamp` is what
    /// every banner, flag, tapestry and torch flame in the game states, so that its sheet's edge
    /// stops at the edge rather than bleeding the opposite one through the filter.
    enum class TextureWrap : std::uint8_t
    {
        Repeat = 0,
        ClampS = 1,
        ClampT = 2,
        Clamp = 3,
    };

    inline constexpr std::size_t sTextureWrapCount = static_cast<std::size_t>(TextureWrap::Clamp) + 1;

    /// What a crash report names a wrap by (`ContentPreprocessor::chain`).
    inline constexpr NamedEnum sTextureWraps{ std::array{
        std::pair{ TextureWrap::Repeat, std::string_view("repeat") },
        std::pair{ TextureWrap::ClampS, std::string_view("clamp s") },
        std::pair{ TextureWrap::ClampT, std::string_view("clamp t") },
        std::pair{ TextureWrap::Clamp, std::string_view("clamp") },
    } };
    static_assert(sTextureWraps.mNames.size() == sTextureWrapCount);

    inline constexpr TextureWrap textureWrapOf(const bool clampS, const bool clampT)
    {
        return static_cast<TextureWrap>((clampS ? 1u : 0u) | (clampT ? 2u : 0u));
    }

    inline constexpr bool clampsS(const TextureWrap wrap)
    {
        return (static_cast<std::uint8_t>(wrap) & 1u) != 0u;
    }

    inline constexpr bool clampsT(const TextureWrap wrap)
    {
        return (static_cast<std::uint8_t>(wrap) & 2u) != 0u;
    }
}
