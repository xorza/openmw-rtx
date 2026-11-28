#pragma once

#include <cstdint>
#include <initializer_list>

namespace Rtx
{
    /// Whose words a placement's key is made of, so the walk's and the ring's words never make one key.
    enum class PlacementKeyKind : std::uint32_t
    {
        /// A placement the frame's walk stands, named by its identity under the nearest stamped node.
        Walked,

        /// A part of a reference the ring stands, named by the reference and the part.
        Referenced,

        /// A cell's ground, named by the cell.
        Ground,
    };

    /// **A placement's key, from the words that name it in every run of the same content** (`MeshInstance::mKey`):
    /// FNV-1a over the kind and the words, then one final mix, as `hashPoints` mixes, so two placements whose words
    /// differ in one low bit differ across the whole key.
    constexpr std::uint32_t placementKeyOf(PlacementKeyKind kind, std::initializer_list<std::uint32_t> words)
    {
        std::uint64_t seed = 0xcbf29ce484222325ull;
        const auto fold = [&seed](std::uint32_t word) {
            seed ^= word;
            seed *= 0x100000001b3ull;
        };
        fold(static_cast<std::uint32_t>(kind));
        for (const std::uint32_t word : words)
            fold(word);

        seed ^= seed >> 29;
        seed *= 0xbf58476d1ce4e5b9ull;
        seed ^= seed >> 32;
        return static_cast<std::uint32_t>(seed);
    }
}
