#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/misc/bytebuffer.hpp>

#include "alphaimage.hpp"
#include "texturedata.hpp"
#include "textureencoding.hpp"

namespace Rtx
{
    /// A texture's chain made again from its finest level, the one way the host makes one: **a box of
    /// the level above** (`Shaders::axisTaps`, three taps along an odd axis), in light where the format
    /// is display-encoded, with a colour weighed by its alpha where the alpha is coverage
    /// (`TextureEncoding::Colour`) and divided back, and the alpha averaged on its own. **A normal map
    /// in one encoding** (`docs/rtx/architecture.md`, "A normal map is whole in BC7, in one encoding"): every
    /// texel its unit normal in three channels, a map of two given its blue, and each level the
    /// direction of the mean of the finest normals it covers, its height beside it.
    ///
    /// One a thread: it keeps the decoded levels and its scratch between builds.
    class ChainBuild
    {
    public:
        /// Whether `file`'s chain can be built: a BC1, BC2 or BC3 file, four loose bytes a texel, or a
        /// BC5 normal map, of more than a texel. Not BC4, nor a BC5 map of data, whose channels no reader
        /// here gives a meaning.
        static bool rebuilds(const TextureData& file);

        /// Decodes the first level of `file`, a description `rebuilds` holds, and boxes `levels` levels in
        /// all from it, each half the one above and at least a texel, as a file lays its own: four bytes a
        /// texel, display-encoded where `file`'s format is. **A painted colour** reads every level it
        /// carries instead, up to `levels`, and has the light painted into it divided out of each, the
        /// levels past its own boxed from the last of them (`docs/rtx/architecture.md`, "A painted colour is
        /// stored delit, keyed by use"). Refills this one.
        void build(const TextureData& file, std::uint32_t levels);

        /// Whether the finest level built holds a texel of no alpha in a BC1 format: a cut in its one
        /// bit of alpha, which a box blurs into a coverage BC1 cannot hold, so its chain is not made
        /// again in BC1.
        bool holdsCut(const TextureData& finest) const;

        /// Appends the built levels `from` and down to `into`, encoded in `file`'s own block format and
        /// laid as a file lays its levels: from 1, so `file`'s finest level and these make a chain of
        /// `file`'s format, and from 0 for a whole chain in it. `build` of `file`, to its own level count
        /// or, for a file of one level, to a texel. A loose file's chain is made in BC7 (`encodeBc7`).
        void encode(const TextureData& file, std::uint32_t from, Misc::ByteBuffer& into) const;

        /// Appends every built level, the finest first, to `into` in BC7 as `encoding` (`bc7FormatOf`),
        /// laid as a file lays its levels: a colour's error weighed as the eye sees it, a map of data's
        /// channel for channel, and a normal map's block in one mode, by whether it holds a height.
        void encodeBc7(TextureEncoding encoding, Misc::ByteBuffer& into) const;

        /// Appends a loose `file`'s own levels to `into` in BC7, as `encodeBc7` encodes the built ones:
        /// its bytes are its texels, read where they lie, in red, green, blue and alpha where the file
        /// lays them blue first, and no level made again.
        static void encodeOwnBc7(const TextureData& file, Misc::ByteBuffer& into);

        std::uint32_t getLevelCount() const { return static_cast<std::uint32_t>(mLevels.size()); }
        const MipLevel& getLevel(std::uint32_t level) const { return mLevels[level]; }

        /// Level `level`'s texels, RGBA, four bytes each, row after row.
        std::span<const std::byte> getTexels(std::uint32_t level) const;

    private:
        /// `encodeBc7` of `levels`, each level's texels as `texelsOf(level)` hands them, blue and red
        /// swapped where `bgr`.
        template <class TexelsOf>
        static void encodeLevelsBc7(TextureEncoding encoding, std::span<const MipLevel> levels,
            const TexelsOf& texelsOf, bool bgr, Misc::ByteBuffer& into);

        /// `build` of a normal map: the levels laid, the finest's alpha read.
        void buildNormals(const TextureData& first);

        /// `build` of a painted colour: the light painted into `read`, its levels read as the levels
        /// built, divided out of each.
        void delight(const TextureData& read);

        AlphaImage mAlpha;
        std::vector<osg::Vec3f> mBand;

        /// How many of the finest level's rows a normal map's build holds at once: a band of four the
        /// reader decodes, and the two before it the box of the level under reads.
        static constexpr std::uint32_t sRingRows = 8;

        /// A normal map's finest unit normals, `sRingRows` rows of them.
        std::vector<osg::Vec3f> mRing;

        /// A normal map's means under its finest level, a texel each, laid as those levels are: the
        /// unnormalised mean of the finest unit normals each covers.
        std::vector<osg::Vec3f> mMeans;

        /// Every level, back to back, four bytes a texel.
        std::vector<std::byte> mTexels;
        std::vector<MipLevel> mLevels;
    };
}
