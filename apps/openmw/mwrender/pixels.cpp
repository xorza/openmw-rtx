#include "pixels.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <osg/GL>
#include <osg/Image>

#include <components/crashcatcher/crash.hpp>

namespace MWRender
{
    namespace
    {
        /// One texel of `area` in `image`, filtered `GL_LINEAR` with `GL_CLAMP_TO_EDGE`, into four
        /// bytes at `out`.
        ///
        /// **The one filter both public shapes are made of.** A sampler's answer and a whole
        /// rectangle of them are the same arithmetic asked once or asked in a loop, and two spellings
        /// of it are two roundings waiting to disagree about a map the game has always drawn one way.
        ///
        /// `u` and `v` are in texels of `area`, measured from its corner and already offset by the
        /// half texel a sampler puts between a coordinate and a centre. The clamp is to `area` and
        /// not to the image around it, so a rectangle filters as though it were the whole picture.
        void filterTexel(
            const osg::Image& image, const SceneUtil::ImageRegion& area, float u, float v, std::uint8_t* out)
        {
            const float flooredU = std::floor(u);
            const float flooredV = std::floor(v);
            const float fracU = u - flooredU;
            const float fracV = v - flooredV;

            const auto clamped = [](float at, std::int32_t extent) {
                return std::clamp(static_cast<std::int32_t>(at), 0, extent - 1);
            };

            const std::int32_t left = area.mX + clamped(flooredU, area.mWidth);
            const std::int32_t right = area.mX + clamped(flooredU + 1.0f, area.mWidth);
            const std::int32_t bottom = area.mY + clamped(flooredV, area.mHeight);
            const std::int32_t top = area.mY + clamped(flooredV + 1.0f, area.mHeight);

            const std::uint8_t* lowerLeft
                = image.data(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(bottom));
            const std::uint8_t* lowerRight
                = image.data(static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(bottom));
            const std::uint8_t* upperLeft
                = image.data(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top));
            const std::uint8_t* upperRight
                = image.data(static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(top));

            for (std::int32_t channel = 0; channel < 4; ++channel)
            {
                const float lower
                    = std::lerp(static_cast<float>(lowerLeft[channel]), static_cast<float>(lowerRight[channel]), fracU);
                const float upper
                    = std::lerp(static_cast<float>(upperLeft[channel]), static_cast<float>(upperRight[channel]), fracU);

                out[channel] = static_cast<std::uint8_t>(std::lround(std::lerp(lower, upper, fracV)));
            }
        }
    }

    void sampleBilinear(const osg::Image& image, float u, float v, std::uint8_t (&out)[4])
    {
        filterTexel(image, SceneUtil::ImageRegion{ 0, 0, image.s(), image.t() },
            u * static_cast<float>(image.s()) - 0.5f, v * static_cast<float>(image.t()) - 0.5f, out);
    }

    bool compositeTile(const osg::Image& tile, const osg::Image& landAlpha, osg::Image& into,
        const SceneUtil::ImageRegion& destination, std::vector<std::uint8_t>& scratch)
    {
        assert(tile.getPixelFormat() == GL_RGBA && tile.getDataType() == GL_UNSIGNED_BYTE);
        assert(into.getPixelFormat() == GL_RGBA && into.getDataType() == GL_UNSIGNED_BYTE);

        // Addressed as `osg::Image::data(column, row)` addresses it, off one pointer checked once.
        const std::uint8_t* alpha = Crash::notNull(landAlpha.data(), "a land mask with no texels");
        const std::size_t alphaBits = landAlpha.getPixelSizeInBits();
        const std::size_t alphaRow = landAlpha.getRowStepInBytes();

        const std::int32_t width = destination.mWidth;
        const std::int32_t height = destination.mHeight;
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        scratch.resize(stride * static_cast<std::size_t>(height));

        for (std::int32_t y = 0; y < height; ++y)
        {
            for (std::int32_t x = 0; x < width; ++x)
            {
                std::uint8_t sampled[4];
                sampleBilinear(tile, (static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                    (static_cast<float>(y) + 0.5f) / static_cast<float>(height), sampled);

                // One texel of the mask per pixel of the overlay
                const std::uint32_t mask = alpha[static_cast<std::size_t>(destination.mX + x) * alphaBits / 8
                    + static_cast<std::size_t>(destination.mY + y) * alphaRow];
                std::uint8_t* out
                    = scratch.data() + static_cast<std::size_t>(y) * stride + static_cast<std::ptrdiff_t>(x * 4);
                out[0] = sampled[0];
                out[1] = sampled[1];
                out[2] = sampled[2];
                out[3] = static_cast<std::uint8_t>((sampled[3] * mask + 127) / 255);
            }
        }

        bool changed = false;
        for (std::int32_t y = 0; y < height && !changed; ++y)
            changed = std::memcmp(into.data(static_cast<std::uint32_t>(destination.mX),
                                      static_cast<std::uint32_t>(destination.mY + y)),
                          scratch.data() + static_cast<std::size_t>(y) * stride, stride)
                != 0;
        if (!changed)
            return false;

        for (std::int32_t y = 0; y < height; ++y)
            std::memcpy(
                into.data(static_cast<std::uint32_t>(destination.mX), static_cast<std::uint32_t>(destination.mY + y)),
                scratch.data() + static_cast<std::size_t>(y) * stride, stride);

        return true;
    }

    void resampleRegion(const osg::Image& from, const SceneUtil::ImageRegion& source, osg::Image& into,
        const SceneUtil::ImageRegion& target)
    {
        assert(source.mWidth > 0 && source.mHeight > 0 && target.mWidth > 0 && target.mHeight > 0);

        const float acrossU = static_cast<float>(source.mWidth) / static_cast<float>(target.mWidth);
        const float acrossV = static_cast<float>(source.mHeight) / static_cast<float>(target.mHeight);

        for (std::int32_t y = 0; y < target.mHeight; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) * acrossV - 0.5f;

            for (std::int32_t x = 0; x < target.mWidth; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) * acrossU - 0.5f;

                filterTexel(from, source, u, v,
                    into.data(static_cast<std::uint32_t>(target.mX + x), static_cast<std::uint32_t>(target.mY + y)));
            }
        }
    }

    osg::ref_ptr<osg::Image> asRgba(osg::ref_ptr<osg::Image> image)
    {
        if (image->getPixelFormat() == GL_RGBA && image->getDataType() == GL_UNSIGNED_BYTE && image->isDataContiguous())
            return image;

        osg::ref_ptr<osg::Image> converted = new osg::Image;
        converted->allocateImage(image->s(), image->t(), 1, GL_RGBA, GL_UNSIGNED_BYTE);

        for (std::int32_t y = 0; y < image->t(); ++y)
            for (std::int32_t x = 0; x < image->s(); ++x)
                converted->setColor(image->getColor(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)),
                    static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));

        return converted;
    }
}
