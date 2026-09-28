#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

namespace Rtx
{
    /// The colour half of one block-compressed block: four colours and the texels that chose them.
    /// Eight bytes that every block-compressed format this renderer reads ends in — BC2 and BC3 put
    /// eight bytes of alpha in front — so one reader serves all three. The colours are as stored,
    /// display-encoded.
    struct ColourBlock
    {
        /// In index order. The fourth is meaningless where `mCutout` is set.
        std::array<osg::Vec3f, 4> mPalette;

        /// Sixteen two-bit indices, the first texel in the lowest bits.
        std::uint32_t mIndices = 0;

        /// Whether the fourth entry is transparent rather than a colour. BC1 spells that by storing
        /// its endpoints in ascending order, which costs it the fourth palette entry. BC2 and BC3
        /// carry alpha of their own and never do.
        bool mCutout = false;

        /// @param punchThrough whether the ascending spelling means transparency, which is BC1's
        ///        alone.
        static ColourBlock read(std::span<const std::byte, 8> bytes, bool punchThrough);

        /// Which of the four a texel chose, counting along rows from the top left.
        std::uint32_t indexAt(std::size_t texel) const { return mIndices >> (texel * 2) & 0x3u; }

        /// Whether a texel is the transparent entry, and so is not a colour at all.
        bool isTransparent(std::size_t texel) const { return mCutout && indexAt(texel) == 3; }
    };
}
