#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Vec3f>

#include "texturedata.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// A texture's alpha channel, decoded to a byte a texel, at every level the file carried —
    /// what a cutout is, apart from what it looks like, decoded once because a reader walks every
    /// texel it asks for. Alpha is linear in every format, so a byte means what it says.
    /// The whole chain, because a sprite is sampled at whatever level the ray's cone can resolve.
    class AlphaImage
    {
    public:
        AlphaImage() = default;

        /// For a caller with one texture to read and no image to reuse. `build` is the whole of it.
        explicit AlphaImage(const TextureData& texture) { build(texture); }

        /// Decodes every level the description carries. A texture with none leaves this empty,
        /// which is a cutout that could not be read. Refills this one rather than making another,
        /// so a loader keeps the room the last texture grew.
        void build(const TextureData& texture);

        /// Where the levels sit in the values. `MipLevel::mOffset` counts texels here rather than
        /// bytes, this being one byte a texel.
        const MipPyramid& getShape() const { return mShape; }

        std::uint32_t getLevelCount() const { return mShape.getLevelCount(); }
        const MipLevel& getLevel(std::uint32_t level) const { return mShape.getLevel(level); }

        /// The largest level's extent, which is what a bake made from this is sized to.
        std::uint32_t getWidth() const { return mShape.getWidth(); }
        std::uint32_t getHeight() const { return mShape.getHeight(); }

        bool isEmpty() const { return mShape.isEmpty(); }

        /// Alpha at a texel of a level, both of which must be inside the image. Defined here
        /// because a reader asks it for every texel of a level, and a call across a translation
        /// unit for a vector index is most of what that walk costs.
        std::uint8_t at(std::uint32_t level, std::uint32_t x, std::uint32_t y) const
        {
            return mValues[mShape.offsetOf(level, x, y, 1)];
        }

    private:
        MipPyramid mShape;
        std::vector<std::uint8_t> mValues;
    };

    /// The buffers the texture passes read an image through — `describeFinest`, and `meanTexel`'s
    /// alpha and colours — held by a thread's `ContentPreprocessor` rather than made per call,
    /// because a cell arrives with many translucent diffuse maps.
    struct AlphaScratch
    {
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
        AlphaImage mAlpha;

        /// A band of colours, `readTexelBand`'s, for a reader that takes the colour beside the
        /// alpha (`meanTexel`).
        std::vector<osg::Vec3f> mColours;
    };

    /// `image`'s finest level as `reachesSolid` and `meanTexel` read it, described into `scratch`'s
    /// buffers with every coarser level left off: a chain is the same picture at lower rates, and
    /// neither question needs more than the finest. Nothing for an image this cannot describe,
    /// which is the same image whose arrival in the texture table refuses it by name.
    ///
    /// @param scratch cleared and refilled here; the description spans it until the next reading.
    std::optional<TextureData> describeFinest(const osg::Image& image, AlphaScratch& scratch);

    /// Whether any texel of a finest level `describeFinest` gave is fully opaque — what tells a wisp
    /// from a mask, since Morrowind keeps its foliage and its clouds under one alpha mode: a leaf
    /// card is solid wherever its paint is, and `Tx_Dagoth_Cloud`'s alpha peaks at seven fifteenths.
    /// The finest level alone, because a mask's average stops reaching solid a level or two down.
    bool reachesSolid(const TextureData& finest);
}
