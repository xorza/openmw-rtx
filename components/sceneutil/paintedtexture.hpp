#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <osg/Image>
#include <osg/Texture2D>

#include "imageregion.hpp"

namespace SceneUtil
{
    /// What a reader has not yet uploaded: the rectangle, and the paint count it stands at, so the
    /// reader remembers the count the rectangle was read against and never a later one.
    struct Painted
    {
        ImageRegion mRegion;
        std::uint32_t mPaints = 0;
    };

    /// A picture the game paints into in main memory, and the rectangle it painted last: the fog of war
    /// and the world map's overlay. `osg::Image::dirty` names no rectangle, so whatever drew the texture
    /// sent the whole of it (megabytes at every cell crossing) or compared every row; the painter says
    /// where it painted instead, and every reader uploads that.
    ///
    /// **Any number of readers, and none of them clears anything.** Each remembers the paint count it last
    /// uploaded to, and `since` answers the union of what came after; a reader further behind than this
    /// remembers takes the whole picture, which is right and only slower.
    ///
    /// **RGBA, one byte a channel, tightly packed**, which both painters allocate and every reader copies
    /// rows of; another kind is a logic error at construction. The image is the caller's to write and
    /// belongs to this texture for the rest of its life.
    ///
    /// **The rasterizer uploads it as upstream uploads the fog**: `paint` dirties the image and the
    /// texture sends the whole of it on its next apply; the rectangles are for the mirrors. A frame that
    /// reads a rectangle half painted draws it again next frame, since the count moves after the bytes.
    class PaintedTexture : public osg::Texture2D
    {
    public:
        explicit PaintedTexture(osg::Image* image);

        /// Says `region` of the image was written. Nothing for an empty one.
        void paint(const ImageRegion& region);

        /// Says the whole image was written.
        void paintAll();

        /// Everything painted since a reader's count stood at `seen`: nothing where nothing was,
        /// the union of the paints after it, or the whole picture where more were painted than
        /// this remembers.
        Painted since(std::uint32_t seen) const;

        ImageRegion whole() const;

        /// How many paints there have been: what a reader that has just uploaded the whole picture
        /// remembers.
        std::uint32_t getPaintCount() const { return mPaints; }

    private:
        /// How many paints a reader may fall behind before it is handed the whole picture: a few frames of
        /// walking on the fog, past which the union is most of a tile anyway.
        static constexpr std::size_t sRemembered = 8;

        /// The last `sRemembered` paints, the paint numbered `n` at `(n - 1) % sRemembered`.
        std::array<ImageRegion, sRemembered> mRecent{};
        std::uint32_t mPaints = 0;
    };
}
