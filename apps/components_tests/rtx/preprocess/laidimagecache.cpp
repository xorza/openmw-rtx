#include <cstddef>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <components/rtx/preprocess/laidimagecache.hpp>

namespace Rtx
{
    namespace
    {
        /// A two-by-two image of `pixelFormat` in bytes, under `name`, with no chain.
        osg::ref_ptr<osg::Image> makeImage(GLenum pixelFormat, std::size_t texelBytes, const char* name)
        {
            const std::size_t bytes = 4 * texelBytes;
            auto* data = new unsigned char[bytes];
            for (std::size_t at = 0; at < bytes; ++at)
                data[at] = static_cast<unsigned char>(at);

            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName(name);
            image->setImage(2, 2, 1, static_cast<GLint>(pixelFormat), pixelFormat, GL_UNSIGNED_BYTE, data,
                osg::Image::USE_NEW_DELETE, 1);
            return image;
        }

        /// **A file is laid once while anything holds what it laid, and again once nothing does.**
        /// An RGB8 file asked twice is one laid image, under two spellings of its name too; dropped,
        /// it is watched and not held, so the next ask lays it again. Another image opened under the
        /// same name is laid for itself. RGBA8, uploaded as it is, and a file with no name are not
        /// laid at all, and an entry is kept only for a file that was.
        TEST(RtxLaidImageCacheTest, aFileIsLaidOnceWhileHeldAndAgainOnceLetGo)
        {
            LaidImageCache cache;

            const osg::ref_ptr<osg::Image> rgb = makeImage(GL_RGB, 3, "textures/tx_rgb.tga");
            osg::ref_ptr<const osg::Image> laid = cache.of(*rgb);
            ASSERT_NE(laid, nullptr);
            EXPECT_EQ(laid->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
            EXPECT_EQ(laid->s(), 2);

            // Byte 3 of the file is the second texel's red, laid at byte 4; its alpha is filled.
            EXPECT_EQ(laid->data()[4], 3u);
            EXPECT_EQ(laid->data()[7], 255u);

            EXPECT_EQ(cache.of(*rgb), laid) << "a file held laid was laid again";

            rgb->setFileName("Textures\\TX_RGB.tga");
            EXPECT_EQ(cache.of(*rgb), laid) << "another spelling of the name laid the file again";
            EXPECT_EQ(cache.size(), 1u);

            const osg::observer_ptr<const osg::Image> watched = laid;
            laid = nullptr;
            EXPECT_FALSE(watched.valid()) << "the cache held a laid image nothing else did";
            const osg::ref_ptr<const osg::Image> again = cache.of(*rgb);
            ASSERT_NE(again, nullptr) << "a file let go of was answered with nothing";
            EXPECT_EQ(again->data()[4], 3u);

            const osg::ref_ptr<osg::Image> reopened = makeImage(GL_RGB, 3, "textures/tx_rgb.tga");
            reopened->data()[3] = 99;
            const osg::ref_ptr<const osg::Image> other = cache.of(*reopened);
            ASSERT_NE(other, nullptr);
            EXPECT_NE(other, again) << "another image under the name was answered with what the first laid";
            EXPECT_EQ(other->data()[4], 99u);

            EXPECT_EQ(cache.of(*makeImage(GL_RGBA, 4, "textures/tx_rgba.tga")), nullptr)
                << "a format uploaded as it is was laid";
            EXPECT_EQ(cache.of(*makeImage(GL_RGB, 3, "")), nullptr) << "a file with no name was laid";
            EXPECT_EQ(cache.size(), 1u) << "an entry was kept for a file never laid";
        }
    }
}
