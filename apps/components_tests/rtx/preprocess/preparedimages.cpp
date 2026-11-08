#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/madeimage.hpp>
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
        std::vector<std::byte> bytesOf(std::initializer_list<std::uint8_t> values)
        {
            std::vector<std::byte> bytes;
            for (const std::uint8_t value : values)
                bytes.push_back(static_cast<std::byte>(value));
            return bytes;
        }

        std::vector<std::byte> copyOf(std::span<const std::byte> bytes)
        {
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        }

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

        /// **A map's chain is pending, then made**: the first ask queues it and answers pending, and
        /// once the threads are done it is the box under the file's finest level, (122, 130, 252, 75),
        /// where the file held black, standing on the file's own finest level and holding none of it. Asked again while
        /// a texture holds it, it is the same image; let go of by every holder, it is made again, from the store. Each
        /// making is one ask of the chain pass in the threads' figures, the second found.
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

            osg::ref_ptr<const MadeImage> made = prepared.find(*file, TextureEncoding::Normal).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(&made->finestOf(*file), file.get()) << "the finest level is the file's";
            ASSERT_EQ(made->getLevels().size(), 1u);
            EXPECT_EQ(made->getLevels()[0].mWidth, 1u);
            EXPECT_EQ(copyOf(made->getCoarser()), bytesOf({ 122, 130, 252, 75 }));

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

        /// **A widened file's chain is laid in RGBA8**, the format its levels are described in: a
        /// three-byte RGB normal map's second level is the box of its four texels in four bytes,
        /// `(10 + 30 + 50 + 70) / 4 = 40`, `(20 + 40 + 60 + 80) / 4 = 50`, `(255 * 4) / 4 = 255`, under
        /// an opaque alpha the widening gave.
        TEST(RtxPreparedImagesTest, aWidenedFilesChainIsLaidInTheFormatItsLevelsAreDescribedIn)
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
            const osg::ref_ptr<const MadeImage> made = prepared.find(*file, TextureEncoding::Normal).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(&made->finestOf(*file), file.get());
            EXPECT_EQ(copyOf(made->getCoarser()), bytesOf({ 40, 50, 255, 255 }));
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
            const osg::ref_ptr<const MadeImage> made = prepared.find(*file, TextureEncoding::Colour).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(&made->finestOf(*file), file.get());
            EXPECT_EQ(copyOf(made->getCoarser()), bytesOf({ 255, 255, 255, 64 }));

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
        /// made RGBA8 to a texel: its finest a 4x4 RGBA8 image of its own, and its 2x2 and 1x1 levels
        /// 16 + 4 = 20 bytes, its last texel the box of red, (255, 0, 0, 255).
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
            const osg::ref_ptr<const MadeImage> made = prepared.find(*file, TextureEncoding::Colour).mImage;
            ASSERT_NE(made, nullptr);
            const osg::Image& finest = made->finestOf(*file);
            EXPECT_NE(&finest, file.get());
            EXPECT_EQ(finest.getPixelFormat(), static_cast<GLenum>(GL_RGBA));
            EXPECT_EQ(finest.getTotalSizeInBytesIncludingMipmaps(), 64u);
            ASSERT_EQ(made->getLevels().size(), 2u);
            ASSERT_EQ(made->getCoarser().size(), 20u);
            const std::span<const std::byte> last = made->getCoarser().subspan(made->getLevels()[1].mOffset);
            EXPECT_EQ(copyOf(last), bytesOf({ 255, 0, 0, 255 }));
        }
    }
}
