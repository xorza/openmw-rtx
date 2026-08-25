#pragma once

#include <cstdint>
#include <vector>

namespace Rtx
{
    struct TextureData;

    /// A texture's alpha channel, decoded to a byte a texel.
    ///
    /// **What a cutout is, separated from what it looks like.** Every other reader of a texture in
    /// this renderer wants its colour, and the two block formats that carry alpha keep it in eight
    /// bytes that `ColourBlock` deliberately steps over — so the one question a cutout asks, *is
    /// there anything here*, had nowhere to be answered. Opacity micromaps ask it a few hundred
    /// thousand times per mesh, which is what makes it worth decoding once into a plain array
    /// instead of unpacking a block per lookup.
    ///
    /// **Alpha is linear in every format.** The colours beside it are display-encoded and the
    /// sampler converts them on the way in; alpha never was, so nothing here has a transfer
    /// function in it and a byte means what it says.
    ///
    /// The largest level only. A micromap is a statement about the finest detail a surface has, and
    /// a mip is that detail already averaged away.
    class AlphaImage
    {
    public:
        /// Decodes the largest level. A texture with no levels leaves this empty, which `sample`
        /// answers as fully opaque — a surface whose cutout could not be read is one that should be
        /// drawn, not one that vanishes.
        explicit AlphaImage(const TextureData& texture);

        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        bool isEmpty() const { return mValues.empty(); }

        /// Alpha at a texel, which must be inside the image.
        std::uint8_t at(std::uint32_t x, std::uint32_t y) const;

        /// Alpha at a texture coordinate, wrapping the way the one sampler this scene shares does.
        ///
        /// **Nearest and not bilinear.** What this feeds is a comparison against a cutoff, and the
        /// question a micromap settles is whether *every* texel under a microtriangle is on one side
        /// of it. Interpolating first would answer about a texel that is not there.
        std::uint8_t sample(float u, float v) const;

    private:
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint8_t> mValues;
    };
}
