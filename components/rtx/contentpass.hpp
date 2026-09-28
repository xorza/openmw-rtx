#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "contentkey.hpp"
#include "namedenum.hpp"

namespace Rtx
{
    /// Every computation the renderer makes of what the content files hold, as it loads them. What
    /// a pass computes is a function of its input alone, so it can be filed under that input and
    /// found again (`ContentCache`); a format conversion such as a colour decode, which costs no
    /// more than reading what it converts, is not one.
    enum class ContentPassId : std::uint32_t
    {
        /// A shape's reversed twins and pockets folded away, and whether it closes — `ShapeFold`.
        Fold,

        /// Whether a texture's alpha ever reaches solid — `SolidReach`.
        SolidReach,

        /// What a texel of a texture is worth on average — `TexelMean`.
        TexelMean,

        Count,
    };

    /// What a report heads each pass's row with, and what a key is made under. A name changed here
    /// files every output of the pass under a new key.
    inline constexpr NamedEnum sContentPasses{ std::array{
        std::pair{ ContentPassId::Fold, std::string_view("fold") },
        std::pair{ ContentPassId::SolidReach, std::string_view("solid reach") },
        std::pair{ ContentPassId::TexelMean, std::string_view("texel mean") },
    } };

    inline constexpr std::size_t sContentPassCount = sContentPasses.mNames.size();

    /// What `ContentPreprocessor` runs: a computation over content, with the one statement of what
    /// it reads that its key is made of.
    ///
    /// **`digest` states everything `run` reads.** An input `run` reads and `digest` leaves out is
    /// two inputs filed under one key, and a cache hands the first one's output to the second
    /// without a word. `sVersion` goes up whenever what `run` computes from the same input changes,
    /// which files every older output apart from the new ones.
    ///
    /// **`run` follows `digest` on the same input**, so a pass may keep what its digest read — a
    /// texture pass describes the image once, for both. `run` is not called where the cache found
    /// the output.
    template <class Pass>
    concept ContentPass
        = requires(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output, ContentDigest& digest)
    {
        {
            Pass::sPass
            } -> std::convertible_to<ContentPassId>;
        {
            Pass::sVersion
            } -> std::convertible_to<std::uint32_t>;
        pass.digest(input, digest);
        pass.run(input, output);
    };
}
