#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "alphaimage.hpp"
#include "texturedata.hpp"
#include "textureencoding.hpp"

namespace Rtx
{
    /// A texture's chain made again from its finest level, the one way the host makes one: **a box of
    /// the level above** (`Shaders::axisTaps`, three taps along an odd axis), in light where the format
    /// is display-encoded, with a colour weighed by its alpha where the alpha is coverage
    /// (`TextureEncoding::Colour`) and divided back, and the alpha averaged on its own. **A normal map
    /// in one encoding** (`.notes/content-cache.md`, normal maps in one encoding): every texel its unit
    /// normal in three channels, a map of two given its blue, and each level the direction of the mean of
    /// the finest normals it covers, its height beside it.
    ///
    /// One a thread: it keeps the decoded levels and its scratch between builds.
    class ChainBuild
    {
    public:
        /// Whether `file`'s chain can be built: a BC1, BC2 or BC3 file, four loose bytes a texel, or a
        /// BC5 normal map, of more than a texel. Not BC4, nor a BC5 map of data, whose channels no reader
        /// here gives a meaning.
        static bool rebuilds(const TextureData& file);

        /// Decodes the first level of `finest`, a description `rebuilds` holds, and boxes `levels` levels
        /// in all from it, each half the one above and at least a texel, as a file lays its own: four
        /// bytes a texel, display-encoded where `finest`'s format is. Refills this one.
        void build(const TextureData& finest, std::uint32_t levels);

        /// Lays `file`'s own levels as the built ones, four bytes a texel in red, green, blue and alpha:
        /// for a loose file, whose bytes are its texels, encoded whole without a level made again.
        /// Refills this one.
        void layOwnLevels(const TextureData& file);

        /// Whether the finest level built holds a texel of no alpha in a BC1 format: a cut in its one
        /// bit of alpha, which a box blurs into a coverage BC1 cannot hold, so its chain is not made
        /// again in BC1.
        bool holdsCut(const TextureData& finest) const;

        /// Appends the built levels 1 and down to `into`, encoded in `file`'s own format and laid as a
        /// file lays its levels, so `file`'s finest level and these make a chain of `file`'s format:
        /// `build` of `file`, to its own level count or, for a file of one level, to a texel.
        void encode(const TextureData& file, std::vector<std::byte>& into) const;

        /// Appends every built level, the finest first, to `into` in BC7 as `encoding` (`bc7FormatOf`),
        /// laid as a file lays its levels: a colour's error weighed as the eye sees it, a map of data's
        /// channel for channel, and a normal map's block in one mode, by whether it holds a height.
        void encodeBc7(TextureEncoding encoding, std::vector<std::byte>& into) const;

        std::uint32_t getLevelCount() const { return static_cast<std::uint32_t>(mLevels.size()); }
        const MipLevel& getLevel(std::uint32_t level) const { return mLevels[level]; }

        /// Level `level`'s texels, RGBA, four bytes each, row after row.
        std::span<const std::byte> getTexels(std::uint32_t level) const;

    private:
        /// `build` of a normal map: the levels laid, the finest's alpha read.
        void buildNormals(const TextureData& first);

        AlphaImage mAlpha;
        std::vector<osg::Vec3f> mBand;

        /// A normal map's means, a texel each, laid as the levels are: the unnormalised mean of the
        /// finest unit normals each covers.
        std::vector<osg::Vec3f> mMeans;

        /// Every level, back to back, four bytes a texel.
        std::vector<std::byte> mTexels;
        std::vector<MipLevel> mLevels;
    };
}
