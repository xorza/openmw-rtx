#include <bit>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/scene/placementkey.hpp>

namespace Rtx
{
    namespace
    {
        /// **A key is FNV-1a over the kind and the words, then the final mix**, held to values an implementation
        /// of its own computed (Python over the same constants): FNV-1a 64 from `0xcbf29ce484222325` by
        /// `0x100000001b3` over each word, then `x ^= x >> 29; x *= 0xbf58476d1ce4e5b9; x ^= x >> 32`, its low 32
        /// bits. The kind, each word and the words' order each move it: the part alone moves 12 of its 32 bits.
        TEST(RtxPlacementKeyTest, aKeyIsTheFoldOfItsKindAndWordsAndEachOneMovesIt)
        {
            constexpr std::uint32_t tree = placementKeyOf(PlacementKeyKind::Referenced, { 1u, 0u, 0u });
            static_assert(tree == 0xc8c4191au);
            EXPECT_EQ(placementKeyOf(PlacementKeyKind::Referenced, { 1u, 0u, 1u }), 0x46aa5d3au);
            EXPECT_EQ(placementKeyOf(PlacementKeyKind::Ground, { 3u, 0u }), 0x9e1d7709u);
            EXPECT_EQ(placementKeyOf(PlacementKeyKind::Walked, { 0u, 0u }), 0x3100de68u);

            EXPECT_EQ(std::popcount(tree ^ 0x46aa5d3au), 12) << "the part's lowest bit reaches the whole key";
            EXPECT_NE(placementKeyOf(PlacementKeyKind::Walked, { 1u, 0u, 0u }), tree) << "the kind";
            EXPECT_NE(placementKeyOf(PlacementKeyKind::Referenced, { 0u, 1u, 0u }), tree) << "the words' order";
            EXPECT_NE(placementKeyOf(PlacementKeyKind::Referenced, { 1u, 0u }), tree) << "a word left out";
        }
    }
}
