#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// Every computation the renderer makes of what the content files hold, as it loads them. What
    /// a pass computes is a function of its input alone, so it can be filed under that input and
    /// found again (`ContentCache`); a format conversion such as a colour decode, which costs no
    /// more than reading what it converts, is not one.
    enum class ContentPassId : std::uint32_t
    {
        /// A shape's reversed twins and pockets folded away, and its normals split where the content
        /// smoothed them across a hard edge — `ShapePass`.
        Shape,

        /// Whether a texture's alpha ever reaches solid — `SolidReach`.
        SolidReach,

        /// What a texel of a texture is worth on average — `TexelMean`.
        TexelMean,

        /// Whether a texture's alpha ever falls below solid — `SolidFall`.
        SolidFall,

        /// The lowest and the highest alpha a texture holds — `AlphaSpan`.
        AlphaSpan,

        /// A file's levels under its finest, made again from it — `ChainPass`.
        Chain,

        /// What a `_spec` map's channels mean — `SpecularKind`.
        SpecularKind,

        /// A classic pair's base colour or metal-roughness map, made of its diffuse and its `_spec` —
        /// `ConversionPass`.
        Conversion,

        Count,
    };

    /// What a report heads each pass's row with, and what a key is made under. A name changed here
    /// files every output of the pass under a new key.
    inline constexpr NamedEnum sContentPasses{ std::array{
        std::pair{ ContentPassId::Shape, std::string_view("shape") },
        std::pair{ ContentPassId::SolidReach, std::string_view("solid reach") },
        std::pair{ ContentPassId::TexelMean, std::string_view("texel mean") },
        std::pair{ ContentPassId::SolidFall, std::string_view("solid fall") },
        std::pair{ ContentPassId::AlphaSpan, std::string_view("alpha span") },
        std::pair{ ContentPassId::Chain, std::string_view("chain") },
        std::pair{ ContentPassId::SpecularKind, std::string_view("specular kind") },
        std::pair{ ContentPassId::Conversion, std::string_view("conversion") },
    } };

    inline constexpr std::size_t sContentPassCount = sContentPasses.mNames.size();

    /// The folder the ray tracer's store stands in under the user's cache directory, the game's and the
    /// harness's both, so a measured run reads what the game wrote.
    inline constexpr std::string_view sContentStoreFolder = "rtx-content";

    /// The ray tracer's store: a pass a kind, held in memory where small, so the frame's thread finds a
    /// fact without a wait on the disk, and a shape at any size, since a shape the frame misses it folds and
    /// splits again, about 140 large ones a session at the ship, 58 ms of frames.
    inline constexpr ContentStore::Policy sContentStorePolicy{
        .mKindCount = static_cast<std::uint32_t>(ContentPassId::Count),
        .mHolding
        = ContentStore::Holding{ .mAtAnySize = std::uint64_t{ 1 } << static_cast<std::uint32_t>(ContentPassId::Shape) },
    };

    /// The kind a pass files its entries as in the ray tracer's store.
    constexpr std::uint32_t storeKindOf(ContentPassId pass)
    {
        return static_cast<std::uint32_t>(pass);
    }

    /// An entry the store holds: its key and the pass that filed it, which a key alone does not say.
    struct StoredKey
    {
        ContentStore::Key mKey{};
        ContentPassId mPass = ContentPassId::Count;

        bool operator==(const StoredKey& other) const = default;
    };

    /// What `ContentPreprocessor` runs: a computation over content, with the one statement of what
    /// it reads that its key is made of.
    ///
    /// **`digest` states everything `run` reads.** An input `run` reads and `digest` leaves out is
    /// two inputs filed under one key, and a cache hands the first one's output to the second
    /// without a word. `sVersion` goes up whenever what `run` computes from the same input changes,
    /// which files every older output apart from the new ones.
    ///
    /// **`run` reads its input whether or not `digest` came first**, because no key is made for a
    /// pass the store keeps nothing of (`sStored`), or where there is no store. Where `digest` did
    /// come first, on the same input, `run` may use what it read — a texture pass describes the
    /// image once for both. `run` is not called where the cache found the output.
    ///
    /// **`sStored` says whether its outputs are kept** (`ContentStore`): false for a pass whose key
    /// costs more than its run, as a walk that stops at the first texel that answers does, since a
    /// key reads every byte (`docs/rtx/research/2026-11-08-normal-map-chains.md`, "What the
    /// preprocessor costs a start").
    template <class Pass>
    concept ContentPass = requires(
        Pass& pass, const typename Pass::Input& input, typename Pass::Output& output, ContentStore::Digest& digest) {
        { Pass::sPass } -> std::convertible_to<ContentPassId>;
        { Pass::sVersion } -> std::convertible_to<std::uint32_t>;
        { Pass::sStored } -> std::convertible_to<bool>;
        pass.digest(input, digest);
        pass.run(input, output);
    };
}
