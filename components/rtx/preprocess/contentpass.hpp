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

        /// Every fact a material asks of a file's texels — `FileFactsPass`.
        FileFacts,

        /// A file's levels under its finest, made again from it — `ChainPass`.
        Chain,

        /// A classic pair's base colour or metal-roughness map, made of its diffuse and its `_spec` —
        /// `ConversionPass`.
        Conversion,

        Count,
    };

    /// What a report heads each pass's row with, and what a key is made under. A name changed here
    /// files every output of the pass under a new key.
    inline constexpr NamedEnum sContentPasses{ std::array{
        std::pair{ ContentPassId::Shape, std::string_view("shape") },
        std::pair{ ContentPassId::FileFacts, std::string_view("file facts") },
        std::pair{ ContentPassId::Chain, std::string_view("chain") },
        std::pair{ ContentPassId::Conversion, std::string_view("conversion") },
    } };

    inline constexpr std::size_t sContentPassCount = sContentPasses.mNames.size();

    /// The folder the ray tracer's store stands in under the user's cache directory, the game's and the
    /// harness's both, so a measured run reads what the game wrote.
    inline constexpr std::string_view sContentStoreFolder = "rtx-content";

    /// **The kind a pass files its entries as in the ray tracer's store, a number the store's folders keep across
    /// builds**, apart from the pass's place in `ContentPassId`: a pass's chains stand under the kind they were made
    /// under however the passes beside it change, and the kinds no pass files under any more (`sStoreKindCount`) are
    /// emptied by the store's sweep, their version nought.
    inline constexpr std::array<std::uint32_t, sContentPassCount> sStoreKinds{ 0, 1, 4, 6 };

    /// Every kind the store has a folder of: the passes' and those retired between them, the four fact passes'
    /// three after the first, kinds 2, 3 and 5, which `FileFactsPass` took the place of.
    inline constexpr std::uint32_t sStoreKindCount = 7;

    constexpr std::uint32_t storeKindOf(ContentPassId pass)
    {
        return sStoreKinds[static_cast<std::size_t>(pass)];
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
    /// two inputs filed under one key. `sVersion` goes up whenever what `run` computes from the same
    /// input changes, which files every older output apart from the new ones.
    ///
    /// **`run` reads its input whether or not `digest` came first**, because no key is made where there is no
    /// store. Where `digest` did come first, on the same input, `run` may use what it read. `run` is not called where
    /// the cache found the output.
    template <class Pass>
    concept ContentPass = requires(
        Pass& pass, const typename Pass::Input& input, typename Pass::Output& output, ContentStore::Digest& digest) {
        { Pass::sPass } -> std::convertible_to<ContentPassId>;
        { Pass::sVersion } -> std::convertible_to<std::uint32_t>;
        pass.digest(input, digest);
        pass.run(input, output);
    };
}
