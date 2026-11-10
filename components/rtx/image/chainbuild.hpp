#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/misc/bytebuffer.hpp>

#include "alphaimage.hpp"
#include "shadingmap.hpp"
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
        /// The bytes of each buffer a build holds that grows with the image, which a thread keeps between
        /// builds: what `capacity` reads and `scratchOf` predicts. The buffers that grow with the count of
        /// levels alone, a few hundred bytes, are left out.
        struct Scratch
        {
            std::size_t mAlpha = 0;
            std::size_t mTexels = 0;
            std::size_t mBand = 0;
            std::size_t mRing = 0;
            std::size_t mColumns = 0;
        };

        /// What `build` of a file `width` by `height`, of `fileLevels` levels, read as `encoding`, to
        /// `levels` levels holds of each buffer at its largest: what a thread's buffers grow to, at most,
        /// where they held less.
        static Scratch scratchOf(std::uint32_t width, std::uint32_t height, std::uint32_t fileLevels,
            TextureEncoding encoding, std::uint32_t levels);

        /// What each buffer holds now, its capacity, which no build gives back.
        Scratch capacity() const;

        /// Gives every buffer's room back.
        void letGo();

        /// Whether `file`'s chain can be built: a BC1, BC2 or BC3 file, four loose bytes a texel, or a
        /// BC5 normal map, of more than a texel and built at its side (`buildsAt`). Not BC4, nor a BC5
        /// map of data, whose channels no reader here gives a meaning.
        static bool rebuilds(const TextureData& file);

        /// Whether a finest level `finest` is one a build lays: no side past what a slot holds
        /// (`TEXTURE_SIDE_MOST`), so its texels, four bytes each with the levels under them, stay
        /// within a level's 32-bit offset. A larger file stands on its own chain, which a slot holds
        /// from its first level within the side.
        static bool buildsAt(const MipLevel& finest);

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

        /// A painted colour's level's columns placed on its light (`delight`), refilled each level.
        std::vector<PaintedAxis> mColumns;

        /// How many of a level's rows a normal map's build holds at once: the finest's band of four the
        /// reader decodes, and the two before it the box of the level under reads.
        static constexpr std::uint32_t sRingRows = 8;

        /// A normal map's means, `sRingRows` rows of each level, the levels back to back from `mRingAt`:
        /// the finest's unit normals, and under it the unnormalised mean of the finest unit normals each
        /// texel covers.
        std::vector<osg::Vec3f> mRing;
        std::vector<std::size_t> mRingAt;

        /// How many rows of each level a normal map's build has boxed.
        std::vector<std::uint32_t> mBoxed;

        /// Every level, back to back, four bytes a texel.
        std::vector<std::byte> mTexels;
        std::vector<MipLevel> mLevels;
    };
}
