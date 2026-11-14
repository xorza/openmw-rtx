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
            const double scale = static_cast<double>(source) / static_cast<double>(target);
            const double from = at * scale;
            const double to = (at + 1) * scale;
            for (int pixel = static_cast<int>(from); pixel < source && pixel < to; ++pixel)
            {
                const double covered
                    = std::min(to, static_cast<double>(pixel + 1)) - std::max(from, static_cast<double>(pixel));
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

        // A row at a time where nothing is being resized, which is every caller that wants the
        // whole frame: the general path below weighs each texel by one, which at 7680 by 2160 is
        // four hundred megabytes of sums for the same bytes. Three channels drop the alpha as they
        // copy.
        if (width == wide && height == tall)
        {
            for (int y = 0; y < height; ++y)
            {
                const int row = order == RowOrder::BottomFirst ? height - 1 - y : y;
                const std::uint8_t* const from = frame.mPixels.data() + static_cast<std::size_t>(row) * wide * 4;
                std::uint8_t* const into = image->data(0, y);
                if (channels == Channels::Rgba)
                    std::memcpy(into, from, static_cast<std::size_t>(wide) * 4);
                else
                    for (std::size_t x = 0; x < static_cast<std::size_t>(wide); ++x)
                        std::memcpy(into + x * bytes, from + x * 4, bytes);
            }

            return image;
        }

        // **The middle of the frame at the asked aspect, averaged over the area each pixel covers**:
        // cut by the rule `MWRender::ScreenshotManager` cuts the rasterizer's thumbnail by
        // (`Misc::cropToAspect`, in whole numbers where it works in double), and scaled. A pixel nearest
        // its centre sampled one texel in sixty of a 4K frame.
        const Misc::Crop crop = Misc::cropToAspect(osg::Vec2i(wide, tall), osg::Vec2i(width, height));

        // **A row at a time, in the order the frame lies in memory**: each row of the image sums the crop's rows that
        // row covers, each read along its length, into one row of sums the width of the image.
        std::vector<double> sums(static_cast<std::size_t>(width) * bytes);
        for (int y = 0; y < height; ++y)
        {
            std::fill(sums.begin(), sums.end(), 0.0);
            forEachCover(crop.mSize.y(), height, y, [&](int row, double down) {
                const std::uint8_t* const line = frame.mPixels.data()
                    + (static_cast<std::size_t>(crop.mOrigin.y() + row) * wide + crop.mOrigin.x()) * 4;
                for (int x = 0; x < width; ++x)
                    forEachCover(crop.mSize.x(), width, x, [&](int column, double across) {
                        const std::uint8_t* const texel = line + static_cast<std::size_t>(column) * 4;
                        double* const sum = &sums[static_cast<std::size_t>(x) * bytes];
                        for (std::size_t channel = 0; channel < bytes; ++channel)
                            sum[channel] += down * across * texel[channel];
                    });
            });

            const int to = order == RowOrder::BottomFirst ? height - 1 - y : y;
            std::uint8_t* const into = image->data(0, to);
            for (std::size_t at = 0; at < sums.size(); ++at)
                into[at] = static_cast<std::uint8_t>(std::clamp(std::lround(sums[at]), 0L, 255L));
        }

        return image;
    }
}
