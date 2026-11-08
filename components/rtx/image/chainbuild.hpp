#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "alphaimage.hpp"
#include "texturedata.hpp"

namespace Rtx
{
    /// A texture's chain made again from its finest level, the one way the host makes one: **a box of
    /// the level above** (`Shaders::axisTaps`, three taps along an odd axis), in light where the format
    /// is display-encoded, with a colour weighed by its alpha where the alpha is coverage
    /// (`TextureEncoding::Colour`) and divided back, and the alpha averaged on its own; a normal is
    /// averaged as a vector and not renormalized. `mipchain.comp` is the device's statement of the same
    /// filter, which a test holds the two to.
    ///
    /// One a thread: it keeps the decoded levels and its scratch between builds.
    class ChainBuild
    {
    public:
        /// Whether `file`'s levels below its finest can be made again in its own format (`encode`): a
        /// BC1, BC2 or BC3 file, or four loose bytes a texel, of more than one level. Not BC4 or BC5,
        /// which no host reader here decodes.
        static bool rebuilds(const TextureData& file);

        /// Decodes the first level of `finest`, a description `readsColour` holds, and boxes `levels`
        /// levels in all from it, each half the one above and at least a texel, as a file lays its
        /// own: four bytes a texel, display-encoded where `finest`'s format is. Refills this one.
        void build(const TextureData& finest, std::uint32_t levels);

        /// Whether the finest level built holds a texel of no alpha in a BC1 format: a cut in its one
        /// bit of alpha, which a box blurs into a coverage BC1 cannot hold, so its chain is not made
        /// again in BC1.
        bool holdsCut(const TextureData& finest) const;

        /// Appends levels 1 and down to `into`, encoded in `file`'s own format and laid as its levels
        /// are, so `file`'s finest level and these make a chain of `file`'s format and shape: `build`
        /// of `file` to its level count.
        void encode(const TextureData& file, std::vector<std::byte>& into) const;

        std::uint32_t getLevelCount() const { return static_cast<std::uint32_t>(mLevels.size()); }
        const MipLevel& getLevel(std::uint32_t level) const { return mLevels[level]; }

        /// Level `level`'s texels, RGBA, four bytes each, row after row.
        std::span<const std::byte> getTexels(std::uint32_t level) const;

    private:
        AlphaImage mAlpha;
        std::vector<osg::Vec3f> mBand;

        /// Every level, back to back, four bytes a texel.
        std::vector<std::byte> mTexels;
        std::vector<MipLevel> mLevels;
    };
}
