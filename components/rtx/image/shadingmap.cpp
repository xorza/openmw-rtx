#include "shadingmap.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/shadingmap.h>

#include "alphaimage.hpp"
#include "colour.hpp"
#include "colourblock.hpp"
#include "texels.hpp"
#include "texturedata.hpp"
#include "textureformat.hpp"
#include "texturewrap.hpp"

namespace Rtx
{
    namespace
    {
        /// What one block or one texel contributes: the sum of its colours in linear light, each
        /// weighed by its alpha, and the alpha summed — `Shaders::ShadingSum` says why.
        struct TexelSum
        {
            osg::Vec3f mSum;
            float mWeight = 0.0f;
        };

        osg::Vec3f linearOf(const osg::Vec3f& colour, bool srgb)
        {
            return srgb ? toLinear(colour) : colour;
        }

        /// The colours of the block whose first texel is at `x`, `y`, from its palette and the
        /// indices that chose it, each weighed by its alpha: what the image holds, so a texel the
        /// block pads past the image's edge weighs nothing.
        TexelSum blockSum(std::span<const std::byte, 8> bytes, bool punchThrough, bool srgb, const AlphaImage& alpha,
            std::uint32_t x, std::uint32_t y)
        {
            const ColourBlock block = ColourBlock::read(bytes, punchThrough);
            std::array<osg::Vec3f, 4> palette{};
            for (std::size_t entry = 0; entry < palette.size(); ++entry)
                palette[entry] = linearOf(block.mPalette[entry], srgb);

            TexelSum total;
            for (std::uint32_t texel = 0; texel < 16; ++texel)
            {
                const std::uint32_t across = x + texel % 4;
                const std::uint32_t down = y + texel / 4;
                if (across >= alpha.getWidth() || down >= alpha.getHeight())
                    continue;

                const float weight = alpha.at(0, across, down) / 255.0f;
                total.mSum += palette[block.indexAt(texel)] * weight;
                total.mWeight += weight;
            }

            return total;
        }

        /// Reads the largest level of `texture` once, handing `sink` each block or texel along with
        /// where its centre lands. Block-compressed formats are read through their palettes: a
        /// block's sum is its palette weighted by how many texels chose each entry.
        template <class Sink>
        void readTexels(const TextureData& texture, const Sink& sink)
        {
            assert(!texture.mLevels.empty());
            const MipLevel& level = texture.mLevels.front();
            const std::uint32_t width = std::max(level.mWidth, 1u);
            const std::uint32_t height = std::max(level.mHeight, 1u);
            const bool srgb = isSrgb(texture.mFormat);
            const AlphaImage alpha(texture);

            const TexelLayout layout = layoutOf(texture.mFormat);
            if (layout.isBlocked())
            {
                const std::uint32_t columns = (width + 3) / 4;
                const std::uint32_t rows = (height + 3) / 4;
                for (std::uint32_t row = 0; row < rows; ++row)
                    for (std::uint32_t column = 0; column < columns; ++column)
                    {
                        // The block's own centre decides where it lands, so a block straddling a
                        // boundary is not split between two.
                        const std::span<const std::byte, 8> colour
                            = colourHalfAt(texture.mBytes, level.blockOffset(column, row, layout.mBytes), layout);
                        sink(column * 4 + 2, row * 4 + 2,
                            blockSum(colour, isBc1(texture.mFormat), srgb, alpha, column * 4, row * 4));
                    }
                return;
            }

            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const float weight = alpha.at(0, x, y) / 255.0f;
                    sink(x, y,
                        TexelSum{
                            linearOf(looseColourAt(texture, level.texelOffset(x, y, layout.mBytes)), srgb) * weight,
                            weight });
                }
        }
    }

    ShadingMap::ShadingMap()
    {
        mValues.fill(1.0f);
    }

    ShadingMap::ShadingMap(const TextureData& texture)
    {
        assert(!texture.mLevels.empty());

        const MipLevel& level = texture.mLevels.front();
        const std::uint32_t width = std::max(level.mWidth, 1u);
        const std::uint32_t height = std::max(level.mHeight, 1u);
        std::array<float, std::size_t{ sExtent } * sExtent> sums{};
        std::array<float, std::size_t{ sExtent } * sExtent> weights{};

        // Where a texel or a block lands in the grid. A texture smaller than the grid leaves cells
        // untouched, which is what the fill below is for.
        const auto cellOf = [&](std::uint32_t x, std::uint32_t y) {
            const std::uint32_t column = std::min(x * sExtent / width, sExtent - 1);
            const std::uint32_t row = std::min(y * sExtent / height, sExtent - 1);
            return std::size_t{ row } * sExtent + column;
        };

        // Luminance is linear in the colour, so the luminance of a sum is the sum of luminances and
        // a block resolves once for every texel in it.
        readTexels(texture, [&](std::uint32_t x, std::uint32_t y, const TexelSum& texels) {
            const std::size_t cell = cellOf(x, y);
            // Rec. 709, in linear light, which is where a luminance means anything.
            sums[cell] += texels.mSum * Shaders::LUMINANCE_WEIGHTS;
            weights[cell] += texels.mWeight;
        });

        // A texture smaller than the grid resolves into a handful of cells and leaves the rest
        // empty. Reading those as black would make the estimate a spike and drive the correction
        // into its clamps, so too few texels to resolve shading means the same as having none.
        float total = 0.0f;
        std::uint32_t sampled = 0;
        for (std::size_t cell = 0; cell < mValues.size(); ++cell)
            if (weights[cell] > 0.0f)
            {
                mValues[cell] = sums[cell] / weights[cell];
                total += mValues[cell];
                ++sampled;
            }

        // A texture that counted nothing is content, not a broken contract: a cutout whose every
        // texel is a hole weighs nothing, and resolves no cell.
        if (sampled == 0)
        {
            mValues.fill(1.0f);
            return;
        }

        const float average = total / static_cast<float>(sampled);
        for (std::size_t cell = 0; cell < mValues.size(); ++cell)
            if (!(weights[cell] > 0.0f))
                mValues[cell] = average;

        // Around the wrap along an axis the texture repeats, and held at the edge along one it
        // clamps: `Shaders::SHADING_BLUR_PASSES` says why each.
        const bool clampsAcross = clampsS(texture.mWrap);
        const bool clampsDown = clampsT(texture.mWrap);
        const std::uint32_t last = sExtent - 1;
        std::array<float, std::size_t{ sExtent } * sExtent> scratch{};
        for (std::uint32_t pass = 0; pass < Shaders::SHADING_BLUR_PASSES; ++pass)
        {
            for (std::uint32_t y = 0; y < sExtent; ++y)
                for (std::uint32_t x = 0; x < sExtent; ++x)
                {
                    const std::uint32_t left = clampsAcross ? std::max(x, 1u) - 1 : (x + last) % sExtent;
                    const std::uint32_t right = clampsAcross ? std::min(x + 1, last) : (x + 1) % sExtent;
                    const std::size_t row = std::size_t{ y } * sExtent;
                    scratch[row + x] = (mValues[row + left] + mValues[row + x] + mValues[row + right]) / 3.0f;
                }

            for (std::uint32_t y = 0; y < sExtent; ++y)
                for (std::uint32_t x = 0; x < sExtent; ++x)
                {
                    const std::size_t above
                        = std::size_t{ clampsDown ? std::max(y, 1u) - 1 : (y + last) % sExtent } * sExtent;
                    const std::size_t below
                        = std::size_t{ clampsDown ? std::min(y + 1, last) : (y + 1) % sExtent } * sExtent;
                    const std::size_t here = std::size_t{ y } * sExtent;
                    mValues[here + x] = (scratch[above + x] + scratch[here + x] + scratch[below + x]) / 3.0f;
                }
        }

        // Normalising is what makes this a redistribution rather than a dimmer. Dividing by a
        // map that averages one moves light from where the texture already had it to where it did
        // not, and leaves the total alone.
        float mean = 0.0f;
        for (const float value : mValues)
            mean += value;

        mean /= static_cast<float>(mValues.size());

        // A texture that is black everywhere has no lighting to redistribute and no scale to
        // divide by, so it keeps the neutral map it would otherwise be given nonsense in place of.
        if (!(mean > 0.0f))
        {
            mValues.fill(1.0f);
            return;
        }

        for (float& value : mValues)
            value = std::clamp(value / mean, sFloor, sCeiling);
    }

    std::uint16_t encodeShading(const float value)
    {
        return static_cast<std::uint16_t>(std::lround(Shaders::shadingUnit(value) * 65535.0f));
    }

    float decodeShading(const std::uint16_t stored)
    {
        return Shaders::shadingFactor(static_cast<float>(stored) / 65535.0f);
    }

    float paintedLight(std::span<const float> map, float u, float v)
    {
        constexpr int extent = static_cast<int>(ShadingMap::sExtent);
        assert(map.size() == std::size_t{ extent } * extent);

        const auto fraction = [](float value) { return value - std::floor(value); };

        const float x = fraction(u) * extent - 0.5f;
        const float y = fraction(v) * extent - 0.5f;
        const auto lowX = static_cast<int>(std::floor(x));
        const auto lowY = static_cast<int>(std::floor(y));
        const float acrossX = x - static_cast<float>(lowX);
        const float acrossY = y - static_cast<float>(lowY);

        // The half-texel back above puts the lowest cell at minus one, which the wrap takes to the
        // far edge — which is the whole point of a tiling map and the one thing a clamp would lose.
        const auto wrap = [](int at) { return (at % extent + extent) % extent; };
        const auto cell = [&](int column, int row) {
            return map[static_cast<std::size_t>(wrap(row)) * extent + static_cast<std::size_t>(wrap(column))];
        };

        const float top = std::lerp(cell(lowX, lowY), cell(lowX + 1, lowY), acrossX);
        const float bottom = std::lerp(cell(lowX, lowY + 1), cell(lowX + 1, lowY + 1), acrossX);

        return std::lerp(top, bottom, acrossY);
    }
}
