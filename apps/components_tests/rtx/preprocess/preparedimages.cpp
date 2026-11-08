#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// A 2x2 RGBA8 normal map whose file's second level is the premultiplied black, laid as a
        /// loader hands one over.
        osg::ref_ptr<osg::Image> makeNormalMap()
        {
            const std::uint8_t bytes[] = {
                128, 128, 255, 0, 128, 136, 255, 0, 120, 128, 247, 100, 112, 128, 251, 200, //
                0, 0, 0, 0, //
            };
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new unsigned char[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            image->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(osg::Image::MipmapDataType{ 16 });
            image->setFileName("textures/tx_test_nh.dds");
            return image;
        }

        /// **A map's image is pending, then made**: the first ask queues it and answers pending, and
        /// once the threads are done the image holds the file's finest level and the box under it,
        /// (122, 130, 252, 75), where the file held black. Asked again while a texture holds it, it is
        /// the same image; let go of by every holder, it is made again, from the store.
        TEST(RtxPreparedImagesTest, aMapsImageIsPendingThenMadeAndHeldByWhatHoldsIt)
        {
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            PreparedImages prepared(&store, 2);
            const osg::ref_ptr<osg::Image> file = makeNormalMap();

            const PreparedImage first = prepared.find(*file, TextureEncoding::Normal);
            EXPECT_TRUE(first.mPending);
            EXPECT_EQ(first.mImage, nullptr);
            prepared.flush();
            EXPECT_TRUE(prepared.isIdle());

            osg::ref_ptr<const osg::Image> made = prepared.find(*file, TextureEncoding::Normal).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_NE(made.get(), file.get());
            EXPECT_EQ(made->getNumMipmapLevels(), 2u);
            const unsigned char* const second = made->getMipmapData(1);
            EXPECT_EQ(second[0], 122);
            EXPECT_EQ(second[1], 130);
            EXPECT_EQ(second[2], 252);
            EXPECT_EQ(second[3], 75);
            EXPECT_EQ(std::memcmp(made->data(), file->data(), 16), 0) << "the finest level is the file's";

            EXPECT_EQ(prepared.find(*file, TextureEncoding::Normal).mImage, made) << "held by what holds it";

            made = nullptr;
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Normal).mPending) << "let go of, made again";
            prepared.flush();
            EXPECT_NE(prepared.find(*file, TextureEncoding::Normal).mImage, nullptr);
            store.flush();
            EXPECT_GE(store.getStats().mFound, 1u) << "the second making read the store";
        }

        /// **A widened file's image is made in RGBA8**, the format its levels were laid in: a
        /// three-byte RGB normal map's made image is RGBA8 of four bytes a texel, its finest level the
        /// file's texels with an opaque alpha, and its second the box of the four, `(10 + 30 + 50 + 70)
        /// / 4 = 40`, `(20 + 40 + 60 + 80) / 4 = 50`, `(255 * 4) / 4 = 255`.
        TEST(RtxPreparedImagesTest, aWidenedFilesImageIsMadeInTheFormatItsLevelsWereLaidIn)
        {
            const std::uint8_t bytes[] = { 10, 20, 255, 30, 40, 255, 50, 60, 255, 70, 80, 255, 1, 2, 3 };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new unsigned char[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            file->setImage(2, 2, 1, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE, 1);
            file->setMipmapLevels(osg::Image::MipmapDataType{ 12 });
            file->setFileName("textures/tx_rgb_n.dds");

            PreparedImages prepared(nullptr, 1);
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Normal).mPending);
            prepared.flush();
            const osg::ref_ptr<const osg::Image> made = prepared.find(*file, TextureEncoding::Normal).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
            EXPECT_EQ(made->getTotalSizeInBytesIncludingMipmaps(), 16u + 4u);
            const unsigned char* const finest = made->data();
            EXPECT_EQ(finest[0], 10);
            EXPECT_EQ(finest[3], 255) << "the alpha the widening gave";
            const unsigned char* const second = made->getMipmapData(1);
            EXPECT_EQ(second[0], 40);
            EXPECT_EQ(second[1], 50);
            EXPECT_EQ(second[2], 255);
        }

        /// **A file whose chain stands answers nothing made**: a single-level map has no chain to
        /// make again, and is neither pending nor made once its ask is done.
        TEST(RtxPreparedImagesTest, aFileWhoseChainStandsAnswersNothingMade)
        {
            PreparedImages prepared(nullptr, 1);
            osg::ref_ptr<osg::Image> single = new osg::Image;
            single->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            single->setFileName("textures/tx_single_n.dds");

            EXPECT_TRUE(prepared.find(*single, TextureEncoding::Data).mPending);
            prepared.flush();
            const PreparedImage answer = prepared.find(*single, TextureEncoding::Data);
            EXPECT_FALSE(answer.mPending);
            EXPECT_EQ(answer.mImage, nullptr);
        }
    }
}
