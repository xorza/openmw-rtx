#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC and RGTC formats, which Windows's and Apple's gl.h lack.
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/firsttexel.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
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
        /// The first texel of `made`'s level `level`, as the device decodes it.
        Testing::Texel texelOf(const MadeImage& made, std::size_t level)
        {
            return Testing::firstTexelOf(
                made.getWholeFormat().value(), std::span(made.getBytes()).subspan(made.getLevels()[level].mOffset));
        }

        /// A 4x4 BC1 normal map of two levels: its finest one block of (132, 130, 255), the first endpoint
        /// (16, 32, 31) in 5:6:5 above a black second with every index nought, and its second level black.
        osg::ref_ptr<osg::Image> makeBlockNormalMap()
        {
            const std::uint8_t bytes[] = { 0x1f, 0x84, 0x00, 0x00, 0, 0, 0, 0, //
                0, 0, 0, 0, 0, 0, 0, 0 };
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new unsigned char[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            image->setImage(4, 4, 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE,
                data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(osg::Image::MipmapDataType{ 8 });
            image->setFileName("textures/tx_block_n.dds");
            return image;
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
        /// once the threads are done a block map of data's is the box under the file's finest level, one
        /// BC1 block of (132, 130, 255) where the file held black, standing on the file's own finest level and
        /// holding none of it. Asked again while a texture holds it, it is the same image; let go of by
        /// every holder, it is made again, from the store. Each making is one ask of the chain pass in the
        /// threads' figures, the second found.
        TEST(RtxPreparedImagesTest, aMapsImageIsPendingThenMadeAndHeldByWhatHoldsIt)
        {
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            PreparedImages prepared(&store, 2);
            const osg::ref_ptr<osg::Image> file = makeBlockNormalMap();

            const PreparedImage first = prepared.find(*file, TextureEncoding::Data, TextureWrap::Repeat);
            EXPECT_TRUE(first.mPending);
            EXPECT_EQ(first.mImage, nullptr);
            prepared.flush();
            EXPECT_TRUE(prepared.isIdle());
            const ContentStats madeOnce = prepared.takeStats();
            EXPECT_EQ(madeOnce.at(ContentPassId::Chain).mAsked, 1u);
            EXPECT_EQ(madeOnce.at(ContentPassId::Chain).mHits, 0u);
            EXPECT_EQ(prepared.takeStats().at(ContentPassId::Chain).mAsked, 0u) << "taken twice";

            osg::ref_ptr<const MadeImage> made
                = prepared.find(*file, TextureEncoding::Data, TextureWrap::Repeat).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_FALSE(made->getWholeFormat().has_value()) << "the finest level is the file's";
            ASSERT_EQ(made->getLevels().size(), 1u) << "the 2x2, as many levels as the file carries";
            ASSERT_EQ(made->getBytes().size(), 8u) << "one BC1 block";
            EXPECT_EQ(Testing::firstTexelOf(TextureFormat::Bc1RgbaUnorm, made->getBytes()),
                (Testing::Texel{ 132, 130, 255, 255 }));

            EXPECT_EQ(prepared.find(*file, TextureEncoding::Data, TextureWrap::Repeat).mImage, made)
                << "held by what holds it";

            made = nullptr;
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Data, TextureWrap::Repeat).mPending)
                << "let go of, made again";
            prepared.flush();
            EXPECT_NE(prepared.find(*file, TextureEncoding::Data, TextureWrap::Repeat).mImage, nullptr);
            const ContentStats madeAgain = prepared.takeStats();
            EXPECT_EQ(madeAgain.at(ContentPassId::Chain).mAsked, 1u);
            EXPECT_EQ(madeAgain.at(ContentPassId::Chain).mHits, 1u) << "made again and not found";
            store.flush();
            EXPECT_GE(store.getStats().mFound, 1u) << "the second making read the store";
        }

        /// **A normal map's whole chain is made in BC7**: a 2x2 RGBA8 normal map whose second level is
        /// black is two BC7 blocks, linear, its second the direction of the mean of its four unit normals,
        /// (122, 130, 255) under its heights' `75` (`aNormalMapsChainIsTheDirectionOfTheMeanOfItsUnitNormals`),
        /// within BC7's step. A widened three-byte RGB normal map of (10, 20, 255), (30, 40, 255), (50, 60,
        /// 255) and (70, 80, 255) is made the same way: their unit normals (−0.575961, −0.526944, 0.624979),
        /// (−0.533345, −0.478643, 0.697453), (−0.473241, −0.412177, 0.778559) and (−0.389272, −0.321573,
        /// 0.863169) average to (−0.492955, −0.434834, 0.741040), of length 0.990568, so (−0.497649,
        /// −0.438974, 0.748096) and the bytes (64.05, 71.53, 222.88): (64, 72, 223), under the alpha the
        /// widening gave. A BC1 normal map that carries its chain is made whole as well, its finest texel
        /// (132, 130, 255) as unit: (0.035294, 0.019608, 1) over its length 1.000815 is (0.035265,
        /// 0.019592, 0.999186), the bytes (132.00, 130.00, 254.90).
        TEST(RtxPreparedImagesTest, aNormalMapsWholeChainIsMadeInBc7)
        {
            const std::uint8_t bytes[] = { 10, 20, 255, 30, 40, 255, 50, 60, 255, 70, 80, 255, 1, 2, 3 };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new unsigned char[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            file->setImage(2, 2, 1, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE, 1);
            file->setMipmapLevels(osg::Image::MipmapDataType{ 12 });
            file->setFileName("textures/tx_rgb_n.dds");

            PreparedImages prepared(nullptr, 1);
            const osg::ref_ptr<osg::Image> normals = makeNormalMap();
            EXPECT_TRUE(prepared.find(*normals, TextureEncoding::Normal, TextureWrap::Repeat).mPending);
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Normal, TextureWrap::Repeat).mPending);
            prepared.flush();

            const osg::ref_ptr<const MadeImage> loose
                = prepared.find(*normals, TextureEncoding::Normal, TextureWrap::Repeat).mImage;
            ASSERT_NE(loose, nullptr);
            EXPECT_EQ(loose->getWholeFormat(), TextureFormat::Bc7Unorm);
            ASSERT_EQ(loose->getLevels().size(), 2u);
            EXPECT_EQ(loose->getBytes().size(), 32u) << "a BC7 block a level";
            EXPECT_TRUE(Testing::isNear(texelOf(*loose, 1), { 122, 130, 255, 75 }, 2)) << "the mean's direction";

            const osg::ref_ptr<const MadeImage> widened
                = prepared.find(*file, TextureEncoding::Normal, TextureWrap::Repeat).mImage;
            ASSERT_NE(widened, nullptr);
            EXPECT_EQ(widened->getWholeFormat(), TextureFormat::Bc7Unorm);
            EXPECT_TRUE(Testing::isNear(texelOf(*widened, 1), { 64, 72, 223, 255 }, 2))
                << "the widened mean's direction";

            // A block normal map that carries its chain is made whole too, its finest level with the rest.
            const osg::ref_ptr<osg::Image> block = makeBlockNormalMap();
            EXPECT_TRUE(prepared.find(*block, TextureEncoding::Normal, TextureWrap::Repeat).mPending);
            prepared.flush();
            const osg::ref_ptr<const MadeImage> blockMade
                = prepared.find(*block, TextureEncoding::Normal, TextureWrap::Repeat).mImage;
            ASSERT_NE(blockMade, nullptr);
            EXPECT_EQ(blockMade->getWholeFormat(), TextureFormat::Bc7Unorm);
            ASSERT_EQ(blockMade->getLevels().size(), 2u) << "4x4 and 2x2, as the file carries";
            EXPECT_TRUE(Testing::isNear(texelOf(*blockMade, 0), { 132, 130, 255, 255 }, 2))
                << "the file's normal, unit";
        }

        /// **A loose colour's own chain is encoded as it stands, and keyed by every level**: two 2x2
        /// RGBA8 colours of one white finest level, the one's second level red and the other's blue, are
        /// each their own two BC7 levels, the second within BC7's step of the file's own, and neither is
        /// found in the store under the other's key.
        TEST(RtxPreparedImagesTest, aLooseColoursOwnChainIsEncodedAsItStandsAndKeyedByEveryLevel)
        {
            const auto makeColour = [](std::uint8_t red, std::uint8_t blue, const char* name) {
                const std::uint8_t bytes[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                    255, 255, red, 0, blue, 255 };
                osg::ref_ptr<osg::Image> image = new osg::Image;
                auto* data = new unsigned char[sizeof(bytes)];
                std::memcpy(data, bytes, sizeof(bytes));
                image->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
                image->setMipmapLevels(osg::Image::MipmapDataType{ 16 });
                image->setFileName(name);
                return image;
            };
            const osg::ref_ptr<osg::Image> red = makeColour(255, 0, "textures/tx_red.dds");
            const osg::ref_ptr<osg::Image> blue = makeColour(0, 255, "textures/tx_blue.dds");
            ASSERT_TRUE(PreparedImages::prepares(*red, TextureEncoding::Colour));

            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            PreparedImages prepared(&store, 1);
            prepared.find(*red, TextureEncoding::Colour, TextureWrap::Repeat);
            prepared.flush();
            prepared.find(*blue, TextureEncoding::Colour, TextureWrap::Repeat);
            prepared.flush();
            EXPECT_EQ(prepared.takeStats().at(ContentPassId::Chain).mHits, 0u) << "one key for both";

            const osg::ref_ptr<const MadeImage> madeRed
                = prepared.find(*red, TextureEncoding::Colour, TextureWrap::Repeat).mImage;
            const osg::ref_ptr<const MadeImage> madeBlue
                = prepared.find(*blue, TextureEncoding::Colour, TextureWrap::Repeat).mImage;
            ASSERT_NE(madeRed, nullptr);
            ASSERT_NE(madeBlue, nullptr);
            EXPECT_EQ(madeRed->getWholeFormat(), TextureFormat::Bc7Srgb);
            ASSERT_EQ(madeRed->getLevels().size(), 2u);
            EXPECT_TRUE(Testing::isNear(texelOf(*madeRed, 1), { 255, 0, 0, 255 }, 2)) << "the file's own red";
            EXPECT_TRUE(Testing::isNear(texelOf(*madeBlue, 1), { 0, 0, 255, 255 }, 2)) << "the file's own blue";
        }

        /// **A painted block colour is made whole in its own format, and made once a wrap**: the 4x4 BC1
        /// map of one colour, (132, 130, 255), bound as a painted colour, estimates to one everywhere,
        /// so its two levels come back as one BC1 block each of that colour, 2 × 8 bytes, sRGB. Asked
        /// again at another wrap it is made again, since the estimate follows the wrap, where a normal
        /// map asked at two wraps is one job.
        TEST(RtxPreparedImagesTest, aPaintedBlockColourIsMadeWholeInItsOwnFormatOnceAWrap)
        {
            PreparedImages prepared(nullptr, 1);
            const osg::ref_ptr<osg::Image> file = makeBlockNormalMap();
            ASSERT_TRUE(PreparedImages::prepares(*file, TextureEncoding::Painted));
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Painted, TextureWrap::Repeat).mPending);
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Painted, TextureWrap::Clamp).mPending)
                << "another wrap, another chain";
            EXPECT_TRUE(prepared.find(*file, TextureEncoding::Normal, TextureWrap::Repeat).mPending);
            prepared.flush();
            EXPECT_EQ(prepared.takeStats().at(ContentPassId::Chain).mAsked, 3u);
            EXPECT_FALSE(prepared.find(*file, TextureEncoding::Normal, TextureWrap::Clamp).mPending)
                << "a normal map's chain is one at every wrap";

            const osg::ref_ptr<const MadeImage> made
                = prepared.find(*file, TextureEncoding::Painted, TextureWrap::Repeat).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getWholeFormat(), TextureFormat::Bc1RgbaSrgb);
            ASSERT_EQ(made->getLevels().size(), 2u);
            EXPECT_EQ(made->getBytes().size(), 16u);
            EXPECT_TRUE(Testing::isNear(texelOf(*made, 0), { 132, 130, 255, 255 }, 2)) << "a neutral estimate";
        }

        /// **A file whose chain stands answers nothing made**: a BC5 map of data, whose two channels no
        /// host reader gives a meaning, is neither pending nor made once its ask is done.
        TEST(RtxPreparedImagesTest, aFileWhoseChainStandsAnswersNothingMade)
        {
            PreparedImages prepared(nullptr, 1);
            osg::ref_ptr<osg::Image> two = new osg::Image;
            two->allocateImage(4, 4, 1, GL_COMPRESSED_RED_GREEN_RGTC2_EXT, GL_UNSIGNED_BYTE);
            two->setFileName("textures/tx_two_data.dds");

            EXPECT_TRUE(prepared.find(*two, TextureEncoding::Data, TextureWrap::Repeat).mPending);
            prepared.flush();
            const PreparedImage answer = prepared.find(*two, TextureEncoding::Data, TextureWrap::Repeat);
            EXPECT_FALSE(answer.mPending);
            EXPECT_EQ(answer.mImage, nullptr);
        }

        /// **A colour file of one level has its chain made**: a 2x2 RGBA8 colour, its one solid texel
        /// white beside three holes, is made a chain of two levels in BC7 under the curve, the second its
        /// coverage-weighed box, white under a quarter's alpha. A block colour that carries its own chain
        /// is not asked for. **Its key is its one level's texels**: the same file again is
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
            EXPECT_FALSE(PreparedImages::prepares(*makeBlockNormalMap(), TextureEncoding::Colour));

            PreparedImages prepared(nullptr, 1);
            prepared.find(*file, TextureEncoding::Colour, TextureWrap::Repeat);
            prepared.flush();
            const osg::ref_ptr<const MadeImage> made
                = prepared.find(*file, TextureEncoding::Colour, TextureWrap::Repeat).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getWholeFormat(), TextureFormat::Bc7Srgb);
            EXPECT_TRUE(Testing::isNear(texelOf(*made, 1), { 255, 255, 255, 64 }, 2)) << "the coverage-weighed box";

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

        /// **A block file of one level has its whole chain made in BC7**: a 4x4 BC1 colour of one block,
        /// its first endpoint pure red in 5:6:5 (0xf800) above a black second and every index nought, is
        /// made in BC7 to a texel, a block a level, 3 × 16 = 48 bytes, its last texel the box of red.
        TEST(RtxPreparedImagesTest, aBlockFileOfOneLevelHasItsWholeChainMadeInBc7)
        {
            const std::uint8_t block[] = { 0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0 };
            osg::ref_ptr<osg::Image> file = new osg::Image;
            auto* data = new unsigned char[sizeof(block)];
            std::memcpy(data, block, sizeof(block));
            file->setImage(4, 4, 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE,
                data, osg::Image::USE_NEW_DELETE);
            file->setFileName("textures/tx_block.dds");

            PreparedImages prepared(nullptr, 1);
            prepared.find(*file, TextureEncoding::Colour, TextureWrap::Repeat);
            prepared.flush();
            const osg::ref_ptr<const MadeImage> made
                = prepared.find(*file, TextureEncoding::Colour, TextureWrap::Repeat).mImage;
            ASSERT_NE(made, nullptr);
            EXPECT_EQ(made->getWholeFormat(), TextureFormat::Bc7Srgb);
            ASSERT_EQ(made->getLevels().size(), 3u);
            EXPECT_EQ(made->getBytes().size(), 48u);
            EXPECT_TRUE(Testing::isNear(texelOf(*made, 2), { 255, 0, 0, 255 }, 2)) << "the box of red";
        }
    }
}
