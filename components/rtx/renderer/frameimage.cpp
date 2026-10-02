#include "frameimage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <osg/GL>
#include <osg/Vec2i>

#include <components/misc/presentation.hpp>

namespace Rtx
{
    namespace
    {
        /// Hands `visit` every source pixel of `source` that pixel `at` of `target` covers, with the
        /// share of `at` it covers: the pixel's span of the source, `[at, at + 1) * source / target`,
        /// cut at the source's pixel edges. The shares of one pixel sum to one.
        template <class Visit>
        void forEachCover(const int source, const int target, const int at, Visit visit)
        {
            const double scale = double(source) / double(target);
            const double from = at * scale;
            const double to = (at + 1) * scale;
            for (int pixel = static_cast<int>(from); pixel < source && pixel < to; ++pixel)
            {
                const double covered = std::min(to, double(pixel + 1)) - std::max(from, double(pixel));
                if (covered > 0.0)
                    visit(pixel, covered / scale);
            }
        }
    }

    osg::ref_ptr<osg::Image> frameImage(
        const TracedFrame& frame, const int width, const int height, const RowOrder order, const Channels channels)
    {
        if (width <= 0 || height <= 0 || frame.mWidth == 0 || frame.mHeight == 0)
            return nullptr;

        if (frame.mPixels.size() < std::size_t{ frame.mWidth } * frame.mHeight * 4)
            return nullptr;

        const int wide = static_cast<int>(frame.mWidth);
        const int tall = static_cast<int>(frame.mHeight);
        const std::size_t bytes = static_cast<std::size_t>(channels);

        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(width, height, 1, channels == Channels::Rgb ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE);

        // A row at a time where nothing is being resized or dropped, which is both callers
        // that want the whole frame: at 4K the general path below is eight million short copies.
        if (width == wide && height == tall && channels == Channels::Rgba)
        {
            for (int y = 0; y < height; ++y)
            {
                const int row = order == RowOrder::BottomFirst ? height - 1 - y : y;
                std::memcpy(image->data(0, y), frame.mPixels.data() + static_cast<std::size_t>(row) * wide * 4,
                    static_cast<std::size_t>(wide) * 4);
            }

            return image;
        }

        // **The middle of the frame at the asked aspect, averaged over the area each pixel covers**,
        // as the rasterizer's thumbnail is cut (`Misc::cropToAspect`) and scaled. A pixel nearest
        // its centre was a sample of one texel in sixty of a 4K frame, and the whole frame squashed
        // into a thumbnail of another aspect was a picture of no place.
        const Misc::Crop crop = Misc::cropToAspect(osg::Vec2i(wide, tall), osg::Vec2i(width, height));

        // Across first, into a row of sums a crop row tall, then down.
        std::vector<double> across(static_cast<std::size_t>(width) * crop.mSize.y() * bytes, 0.0);
        for (int x = 0; x < width; ++x)
            forEachCover(crop.mSize.x(), width, x, [&](int column, double weight) {
                for (int row = 0; row < crop.mSize.y(); ++row)
                {
                    const std::uint8_t* const texel = frame.mPixels.data()
                        + (static_cast<std::size_t>(crop.mOrigin.y() + row) * wide + crop.mOrigin.x() + column) * 4;
                    double* const sum = &across[(static_cast<std::size_t>(row) * width + x) * bytes];
                    for (std::size_t channel = 0; channel < bytes; ++channel)
                        sum[channel] += weight * texel[channel];
                }
            });

        for (int y = 0; y < height; ++y)
        {
            const int to = order == RowOrder::BottomFirst ? height - 1 - y : y;
            std::uint8_t* const into = image->data(0, to);
            for (int x = 0; x < width; ++x)
                for (std::size_t channel = 0; channel < bytes; ++channel)
                {
                    double value = 0.0;
                    forEachCover(crop.mSize.y(), height, y, [&](int row, double weight) {
                        value += weight * across[(static_cast<std::size_t>(row) * width + x) * bytes + channel];
                    });
                    into[static_cast<std::size_t>(x) * bytes + channel]
                        = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
                }
        }

        return image;
    }
}
