#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/gbuffer.h>

namespace Rtx
{
    namespace
    {
        /// One texel, in the order a backend hands them over.
        constexpr std::array<std::uint8_t, 4> texel(std::uint8_t red, std::uint8_t green)
        {
            return { red, green, 0, 255 };
        }

        /// A frame `width` by `height` whose every texel names its own column in red and its own row
        /// in green, so what a resample picked and which way up it is are both readable.
        std::vector<std::uint8_t> makeFrame(int width, int height)
        {
            std::vector<std::uint8_t> pixels;
            pixels.reserve(static_cast<std::size_t>(width) * height * 4);

            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    for (const std::uint8_t channel : texel(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)))
                        pixels.push_back(channel);

            return pixels;
        }

        /// The red and green of one texel of a result, at whichever stride its format has.
        std::pair<std::uint8_t, std::uint8_t> at(const osg::Image& image, int x, int y)
        {
            const std::uint8_t* row = image.data(0, y);
            const int stride = image.getPixelFormat() == GL_RGB ? 3 : 4;
            return { row[x * stride], row[x * stride + 1] };
        }

        /// The whole frame comes back at the frame's own extents, and the row order is the only
        /// thing that separates the two callers: the loading screen draws its backdrop downwards
        /// and `osgDB` reads a screenshot upwards, so one of the two is upside down if this flips
        /// for both or for neither.
        TEST(RtxFrameImageTest, aFrameComesBackAtItsOwnExtentsAndOnlyTheRowOrderDiffers)
        {
            const std::vector<std::uint8_t> pixels = makeFrame(2, 2);
            const TracedFrame frame{ .mWidth = 2, .mHeight = 2, .mPixels = pixels };

            const osg::ref_ptr<osg::Image> top = frameImage(frame, 2, 2, RowOrder::TopFirst);
            ASSERT_NE(top, nullptr);
            EXPECT_EQ(top->s(), 2);
            EXPECT_EQ(top->t(), 2);
            EXPECT_EQ(top->getPixelFormat(), GL_RGBA);

            // The trace's own order, kept: row zero of the result is row zero of the frame.
            EXPECT_EQ(at(*top, 0, 0), (std::pair<std::uint8_t, std::uint8_t>{ 0, 0 }));
            EXPECT_EQ(at(*top, 1, 0), (std::pair<std::uint8_t, std::uint8_t>{ 1, 0 }));
            EXPECT_EQ(at(*top, 0, 1), (std::pair<std::uint8_t, std::uint8_t>{ 0, 1 }));
            EXPECT_EQ(at(*top, 1, 1), (std::pair<std::uint8_t, std::uint8_t>{ 1, 1 }));

            const osg::ref_ptr<osg::Image> bottom = frameImage(frame, 2, 2, RowOrder::BottomFirst);
            ASSERT_NE(bottom, nullptr);
            EXPECT_EQ(bottom->s(), 2);
            EXPECT_EQ(bottom->t(), 2);

            // Turned over: row zero of the result is the last row of the frame, and the columns are
            // untouched — a flip of both axes would pass a test that only looked at row zero.
            EXPECT_EQ(at(*bottom, 0, 0), (std::pair<std::uint8_t, std::uint8_t>{ 0, 1 }));
            EXPECT_EQ(at(*bottom, 1, 0), (std::pair<std::uint8_t, std::uint8_t>{ 1, 1 }));
            EXPECT_EQ(at(*bottom, 0, 1), (std::pair<std::uint8_t, std::uint8_t>{ 0, 0 }));
            EXPECT_EQ(at(*bottom, 1, 1), (std::pair<std::uint8_t, std::uint8_t>{ 1, 0 }));
        }

        /// A thumbnail is nearest-sampled, and which texels it lands on is arithmetic rather than
        /// taste.
        ///
        /// Hand-computed for four across to two: a target column takes source `x * 4 / 2`, so
        /// columns 0 and 2; a target row takes the same, so rows 0 and 2 read downwards. Read
        /// upwards the target rows are 1 and 0 of the target, which land on source rows 2 and 0.
        TEST(RtxFrameImageTest, aThumbnailTakesTheNearestTexelAndTheSizeItWasAskedFor)
        {
            const std::vector<std::uint8_t> pixels = makeFrame(4, 4);
            const TracedFrame frame{ .mWidth = 4, .mHeight = 4, .mPixels = pixels };

            const osg::ref_ptr<osg::Image> top = frameImage(frame, 2, 2, RowOrder::TopFirst);
            ASSERT_NE(top, nullptr);
            EXPECT_EQ(top->s(), 2);
            EXPECT_EQ(top->t(), 2);

            EXPECT_EQ(at(*top, 0, 0), (std::pair<std::uint8_t, std::uint8_t>{ 0, 0 }));
            EXPECT_EQ(at(*top, 1, 0), (std::pair<std::uint8_t, std::uint8_t>{ 2, 0 }));
            EXPECT_EQ(at(*top, 0, 1), (std::pair<std::uint8_t, std::uint8_t>{ 0, 2 }));
            EXPECT_EQ(at(*top, 1, 1), (std::pair<std::uint8_t, std::uint8_t>{ 2, 2 }));

            const osg::ref_ptr<osg::Image> bottom = frameImage(frame, 2, 2, RowOrder::BottomFirst);
            ASSERT_NE(bottom, nullptr);
            EXPECT_EQ(at(*bottom, 0, 0), (std::pair<std::uint8_t, std::uint8_t>{ 0, 2 }));
            EXPECT_EQ(at(*bottom, 1, 1), (std::pair<std::uint8_t, std::uint8_t>{ 2, 0 }));

            // Three channels: the same texels, packed three bytes apart with the alpha dropped, in
            // a row of six bytes and not eight.
            const osg::ref_ptr<osg::Image> thumbnail = frameImage(frame, 2, 2, RowOrder::BottomFirst, Channels::Rgb);
            ASSERT_NE(thumbnail, nullptr);
            EXPECT_EQ(thumbnail->getPixelFormat(), GL_RGB);
            EXPECT_EQ(thumbnail->getRowSizeInBytes(), 6u);
            EXPECT_EQ(at(*thumbnail, 0, 0), (std::pair<std::uint8_t, std::uint8_t>{ 0, 2 }));
            EXPECT_EQ(at(*thumbnail, 1, 0), (std::pair<std::uint8_t, std::uint8_t>{ 2, 2 }));
            EXPECT_EQ(at(*thumbnail, 1, 1), (std::pair<std::uint8_t, std::uint8_t>{ 2, 0 }));
            const std::uint8_t* const second = thumbnail->data(0, 1);
            ASSERT_NE(second, nullptr);
            EXPECT_EQ(second[2], 0) << "blue, and not the alpha that stood after it";

            // And at the frame's own size, where four channels take the row-copy path and three
            // cannot.
            const osg::ref_ptr<osg::Image> whole = frameImage(frame, 4, 4, RowOrder::TopFirst, Channels::Rgb);
            ASSERT_NE(whole, nullptr);
            EXPECT_EQ(whole->getRowSizeInBytes(), 12u);
            EXPECT_EQ(at(*whole, 3, 3), (std::pair<std::uint8_t, std::uint8_t>{ 3, 3 }));
        }

        /// Nothing to give is null, not a picture of part of a frame.
        ///
        /// **The first load is the case that matters.** `freezeFrame` is asked before anything has
        /// been presented, so the extents are zero and there is no readback behind them; a screenshot
        /// key pressed at the same moment is the same question. Answering with whatever the buffer
        /// happened to hold is how a loading screen comes up showing the last session.
        TEST(RtxFrameImageTest, aFrameThatIsNotThereIsRefusedRatherThanPartlyDrawn)
        {
            const std::vector<std::uint8_t> pixels = makeFrame(2, 2);

            EXPECT_EQ(frameImage(TracedFrame{}, 2, 2, RowOrder::TopFirst), nullptr) << "no frame at all";

            EXPECT_EQ(frameImage(TracedFrame{ .mWidth = 2, .mHeight = 2, .mPixels = std::span(pixels).first(4) }, 2, 2,
                          RowOrder::TopFirst),
                nullptr)
                << "one texel standing in for four";

            const TracedFrame whole{ .mWidth = 2, .mHeight = 2, .mPixels = pixels };
            EXPECT_EQ(frameImage(whole, 0, 2, RowOrder::TopFirst), nullptr) << "asked for no width";
            EXPECT_EQ(frameImage(whole, 2, -1, RowOrder::TopFirst), nullptr) << "asked for a negative height";

            // And the frame it does have comes back, so the refusals above are not simply everything.
            EXPECT_NE(frameImage(whole, 2, 2, RowOrder::TopFirst), nullptr);
        }

        /// The surface channel's normal code: the axes come back exactly, nought comes back as the
        /// code for no normal and that code as nought, the largest code is a float exactly, and every
        /// direction with whole coordinates from minus three to three comes back within the steps'
        /// bound.
        ///
        /// **The bound.** A stored coordinate is at most half a step, `1 / 4094`, from the one
        /// packed, which moves the point on the octahedron by at most
        /// `sqrt(du² + dv² + (|du| + |dv|)²) = sqrt(1.5) / 2047`. The octahedron is nowhere nearer
        /// the centre than `1 / sqrt(3)`, so the projection onto the sphere stretches that by at most
        /// `sqrt(3)`: `sqrt(4.5) / 2047`, about `1.0363e-3`, which is 0.059 degrees. The `1e-6` over
        /// it is the float arithmetic's.
        ///
        /// **The largest code** is both coordinates at their top step, `4094 + 4094 * 4095 =
        /// 16,769,024`, under the 2^24 past which a float skips integers: straight down, which the
        /// fold puts on the square's far corner.
        TEST(RtxFrameImageTest, theSurfaceCodeHoldsANormalWithinTheBoundOfItsSteps)
        {
            using Shaders::packSurfaceNormal;
            using Shaders::unpackSurfaceNormal;

            for (const osg::Vec3f axis :
                { osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(-1.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f),
                    osg::Vec3f(0.0f, -1.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(0.0f, 0.0f, -1.0f) })
                EXPECT_EQ(unpackSurfaceNormal(packSurfaceNormal(axis)), axis)
                    << axis.x() << ' ' << axis.y() << ' ' << axis.z();

            EXPECT_EQ(packSurfaceNormal(osg::Vec3f()), Shaders::SURFACE_NO_NORMAL);
            EXPECT_EQ(unpackSurfaceNormal(Shaders::SURFACE_NO_NORMAL), osg::Vec3f());

            // Straight down folds onto the square's far corner, `(1, 1)`, which is the largest code.
            EXPECT_EQ(packSurfaceNormal(osg::Vec3f(0.0f, 0.0f, -1.0f)), 16769024.0f);
            EXPECT_LT(16769024u, 1u << 24);

            const float bound = std::sqrt(4.5f) / 2047.0f + 1e-6f;
            for (int x = -3; x <= 3; ++x)
                for (int y = -3; y <= 3; ++y)
                    for (int z = -3; z <= 3; ++z)
                    {
                        if (x == 0 && y == 0 && z == 0)
                            continue;

                        osg::Vec3f unit(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                        unit.normalize();
                        const float code = packSurfaceNormal(unit);
                        EXPECT_EQ(code, std::floor(code)) << "a whole code: " << x << ' ' << y << ' ' << z;
                        EXPECT_LE((unpackSurfaceNormal(code) - unit).length(), bound) << x << ' ' << y << ' ' << z;
                    }
        }
    }
}
