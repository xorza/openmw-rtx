#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
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
        /// the same image; let go of by every holder, it is made again, from the store. Each making is
        /// one ask of the chain pass in the threads' figures, the second found.
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
            const ContentStats madeOnce = prepared.takeStats();
            EXPECT_EQ(madeOnce.at(ContentPassId::Chain).mAsked, 1u);
            EXPECT_EQ(madeOnce.at(ContentPassId::Chain).mHits, 0u);
            EXPECT_EQ(prepared.takeStats().at(ContentPassId::Chain).mAsked, 0u) << "taken twice";

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
            const ContentStats madeAgain = prepared.takeStats();
            EXPECT_EQ(madeAgain.at(ContentPassId::Chain).mAsked, 1u);
            EXPECT_EQ(madeAgain.at(ContentPassId::Chain).mHits, 1u) << "made again and not found";
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

        /// **A file whose chain stands answers nothing made**: a BC5 map, which no host reader
        /// decodes, is neither pending nor made once its ask is done.
        TEST(RtxPreparedImagesTest, aFileWhoseChainStandsAnswersNothingMade)
        {
            PreparedImages prepared(nullptr, 1);
            osg::ref_ptr<osg::Image> two = new osg::Image;
            two->allocateImage(4, 4, 1, GL_COMPRESSED_RED_GREEN_RGTC2_EXT, GL_UNSIGNED_BYTE);
            two->setFileName("textures/tx_two_n.dds");

            EXPECT_TRUE(prepared.find(*two, TextureEncoding::Normal).mPending);
            prepared.flush();
            const PreparedImage answer = prepared.find(*two, TextureEncoding::Normal);
            EXPECT_FALSE(answer.mPending);
            EXPECT_EQ(answer.mImage, nullptr);
        }

        /// **A colour file of one level has its chain made**: a 2x2 RGBA8 colour, its one solid texel
        /// white beside three holes, is made a chain of two levels in its own four bytes a texel, the
        /// second its coverage-weighed box, white under a quarter's alpha. A colour that carries its
        /// own chain is not asked for. **Its key is its one level's texels**: the same file again is
        /// one key, and one whose solid texel is red another.
        TEST(RtxPreparedImagesTest, aColourOfOneLevelHasItsChainMade)
        {
            const std::uint8_t bytes[] = { 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new unsigned char[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            file->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            file->setFileName("textures/tx_rain.dds");
            ASSERT_TRUE(PreparedImages::prepares(*file, TextureEncoding::Colour));
            EXPECT_FALSE(PreparedImages::prepares(*makeNormalMap(), TextureEncoding::Colour));

            PreparedImages prepared(nullptr, 1);
            prepared.find(*file, TextureEncoding::Colour);
            prepared.flush();
            const osg::ref_ptr<const osg::Image> made = prepared.find(*file, TextureEncoding::Colour).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getNumMipmapLevels(), 2u);
            const unsigned char* const second = made->getMipmapData(1);
            EXPECT_EQ(second[0], 255);
            EXPECT_EQ(second[1], 255);
            EXPECT_EQ(second[2], 255);
            EXPECT_EQ(second[3], 64);

            const auto keyOf = [](const osg::Image& image) {
                ChainPass pass;
                ContentDigest digest(sContentPasses.name(ChainPass::sPass), ChainPass::sVersion);
                pass.digest(ChainPass::Input{ .mImage = image, .mEncoding = TextureEncoding::Colour }, digest);
                return digest.getKey();
            };
            osg::ref_ptr<osg::Image> red = new osg::Image(*file, osg::CopyOp::DEEP_COPY_ALL);
            EXPECT_EQ(keyOf(*file), keyOf(*red)) << "one picture, two keys";
            red->data()[1] = 0;
            red->data()[2] = 0;
            EXPECT_NE(keyOf(*file), keyOf(*red)) << "the level's texels are not in the key";
        }

        /// **A block file of one level has its chain made loose**: a 4x4 BC1 colour of one block, its
        /// first endpoint pure red in 5:6:5 (0xf800) above a black second and every index nought, is
        /// made RGBA8 to a texel, 4x4, 2x2 and 1x1, 64 + 16 + 4 = 84 bytes, its last texel the box of
        /// red, (255, 0, 0, 255).
        TEST(RtxPreparedImagesTest, aBlockFileOfOneLevelHasItsChainMadeLoose)
        {
            const std::uint8_t block[] = { 0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0 };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new unsigned char[sizeof(block)];
            std::memcpy(data, block, sizeof(block));
            file->setImage(4, 4, 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE,
                data, osg::Image::USE_NEW_DELETE);
            file->setFileName("textures/tx_block.dds");

            PreparedImages prepared(nullptr, 1);
            prepared.find(*file, TextureEncoding::Colour);
            prepared.flush();
            const osg::ref_ptr<const osg::Image> made = prepared.find(*file, TextureEncoding::Colour).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
            EXPECT_EQ(made->getNumMipmapLevels(), 3u);
            EXPECT_EQ(made->getTotalSizeInBytesIncludingMipmaps(), 84u);
            const unsigned char* const last = made->getMipmapData(2);
            EXPECT_EQ(last[0], 255);
            EXPECT_EQ(last[1], 0);
            EXPECT_EQ(last[2], 0);
            EXPECT_EQ(last[3], 255);
        }
    }
}
