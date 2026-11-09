#include <algorithm>
#include <array>
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
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
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

        /// **An entry whose file is gone is swept, whatever it holds**: a map made and never collected,
        /// whose file is let go of, is dropped by the sweep the 1024th entry starts, beside the 1023 whose
        /// files stand. Kept, the table would remember 1025, and the made image with it. Every file is
        /// made before the first is let go of, so none takes its address and drops it through `find`.
        TEST(RtxPreparedImagesTest, aMadeImageNeverCollectedIsDroppedOnceItsFileIsGone)
        {
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            PreparedImages prepared(&store, 1);
            osg::ref_ptr<osg::Image> gone = makeBlockNormalMap();
            EXPECT_TRUE(prepared.find(*gone, TextureEncoding::Data, TextureWrap::Repeat).mPending);
            prepared.flush();

            std::vector<osg::ref_ptr<osg::Image>> standing(1024);
            for (osg::ref_ptr<osg::Image>& file : standing)
                file = makeBlockNormalMap();
            for (std::size_t at = 0; at < 1023; ++at)
                prepared.find(*standing[at], TextureEncoding::Data, TextureWrap::Repeat);
            EXPECT_EQ(prepared.getEntryCount(), 1024u) << "the sweep starts at 1024";

            gone = nullptr;
            prepared.find(*standing.back(), TextureEncoding::Data, TextureWrap::Repeat);
            EXPECT_EQ(prepared.getEntryCount(), 1024u) << "1023 standing and the new one, the one gone dropped";
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

        /// **A classic pair is made into a base colour and a metal-roughness map, each of both files**:
        /// a 2x2 RGBA8 diffuse of 128 and a classic `_spec` of 128 under an alpha of 255, both 0.215861 in
        /// light. Khronos' converter solves `b = 0.215861 · 0.784139 / 0.96 + 0.215861 − 0.08 = 0.312179`,
        /// `c = −0.175861`, a metalness of `(−0.312179 + sqrt(0.125594)) / 0.08 = 0.527663`, 135 of 255, and
        /// a base of 0.37328 in light, 164 encoded; the exponent of 255 is a roughness of `(2 / 257)^(1/4) =
        /// 0.297020`, 76, and the occlusion one. Both whole in BC7, the colour display-encoded, the map
        /// linear, within BC7's step.
        TEST(RtxPreparedImagesTest, aClassicPairIsMadeIntoABaseColourAndAMetalRoughnessMap)
        {
            const auto make = [](std::uint8_t alpha, const char* name) {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                for (std::size_t texel = 0; texel < 4; ++texel)
                {
                    std::fill_n(image->data() + texel * 4, 3, static_cast<unsigned char>(128));
                    image->data()[texel * 4 + 3] = alpha;
                }
                image->setFileName(name);
                return image;
            };
            const osg::ref_ptr<osg::Image> diffuse = make(255, "textures/tx_ore.dds");
            const osg::ref_ptr<osg::Image> specular = make(255, "textures/tx_ore_spec.dds");

            PreparedImages prepared(nullptr, 1);
            EXPECT_TRUE(
                prepared.find(*diffuse, TextureEncoding::ConvertedColour, TextureWrap::Repeat, specular).mPending);
            EXPECT_TRUE(
                prepared.find(*specular, TextureEncoding::ConvertedSpecular, TextureWrap::Repeat, diffuse).mPending);
            prepared.flush();
            EXPECT_EQ(prepared.takeStats().at(ContentPassId::Conversion).mAsked, 2u);

            const osg::ref_ptr<const MadeImage> colour
                = prepared.find(*diffuse, TextureEncoding::ConvertedColour, TextureWrap::Repeat, specular).mImage;
            ASSERT_NE(colour, nullptr);
            EXPECT_EQ(colour->getWholeFormat(), TextureFormat::Bc7Srgb);
            ASSERT_EQ(colour->getLevels().size(), 2u);
            EXPECT_TRUE(Testing::isNear(texelOf(*colour, 0), { 164, 164, 164, 255 }, 2)) << "the base colour";

            const osg::ref_ptr<const MadeImage> map
                = prepared.find(*specular, TextureEncoding::ConvertedSpecular, TextureWrap::Repeat, diffuse).mImage;
            ASSERT_NE(map, nullptr);
            EXPECT_EQ(map->getWholeFormat(), TextureFormat::Bc7Unorm);
            EXPECT_TRUE(Testing::isNear(texelOf(*map, 0), { 135, 76, 255, 255 }, 2)) << "metal, roughness, occlusion";

            // **Keyed by the finest levels alone**, the one level a conversion reads: a diffuse whose
            // second level differs is the same key, and one whose finest differs another.
            const auto chained = [&](unsigned char finest, unsigned char second) {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                auto* data = new unsigned char[20];
                std::fill_n(data, 16, finest);
                std::fill_n(data + 16, 4, second);
                image->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
                image->setMipmapLevels(osg::Image::MipmapDataType{ 16 });
                image->setFileName("textures/tx_ore.dds");
                return image;
            };
            const auto keyOf = [&](const osg::Image& image) {
                ChainBuild build;
                ConversionPass pass(build);
                ContentDigest digest(sContentPasses.name(ConversionPass::sPass), ConversionPass::sVersion);
                pass.digest(
                    ConversionPass::Input{
                        .mImage = image, .mPartner = *specular, .mEncoding = TextureEncoding::ConvertedColour },
                    digest);
                return digest.getKey();
            };
            EXPECT_EQ(keyOf(*chained(128, 9)), keyOf(*chained(128, 200))) << "the second level is not read";
            EXPECT_NE(keyOf(*chained(128, 9)), keyOf(*chained(129, 9)));
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
                ChainBuild build;
                ChainPass pass(build);
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

        /// A `width` by `height` BC1 file of `levels` levels: every block white and opaque, or, where
        /// `cut`, transparent black, a cut in BC1's one bit of alpha.
        osg::ref_ptr<osg::Image> makeBc1(std::uint32_t width, std::uint32_t height, std::uint32_t levels, bool cut)
        {
            std::vector<unsigned char> bytes;
            osg::Image::MipmapDataType offsets;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                if (level > 0)
                    offsets.push_back(static_cast<unsigned int>(bytes.size()));
                const std::uint32_t across = (std::max(width >> level, 1u) + 3) / 4;
                const std::uint32_t down = (std::max(height >> level, 1u) + 3) / 4;
                for (std::uint32_t block = 0; block < across * down; ++block)
                {
                    // Colour nought at or under colour one is BC1's three colours and a transparent
                    // black, which index three, set on every texel, names.
                    const std::array<unsigned char, 8> white{ 0xff, 0xff, 0, 0, 0, 0, 0, 0 };
                    const std::array<unsigned char, 8> hole{ 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
                    const std::array<unsigned char, 8>& written = cut ? hole : white;
                    bytes.insert(bytes.end(), written.begin(), written.end());
                }
            }
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new unsigned char[bytes.size()];
            std::memcpy(data, bytes.data(), bytes.size());
            image->setImage(static_cast<int>(width), static_cast<int>(height), 1, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(offsets);
            image->setFileName("textures/tx_bc1.dds");
            return image;
        }

        /// **What a chain is made into, at each of its pass's edges** (`ChainPass`): a chainless BC1 colour
        /// of 512 texels on its long side, `sLargestBc7ChainSide`, is made whole in BC7 and one of 516 has
        /// its levels made under its own; a painted colour with a cut is made in BC7, which holds the cut's
        /// coverage; and a map of data with a chain and a cut stands as it is, since BC1 cannot hold the
        /// coverage a box makes of a cut.
        TEST(RtxPreparedImagesTest, aChainIsMadeIntoWhatEachEdgeOfItsPassSays)
        {
            ContentPreprocessor content;
            const auto made = [&](const osg::Image& image, TextureEncoding encoding) {
                MadeChain chain;
                content.chain(ChainPass::Input{ .mImage = image, .mEncoding = encoding }, chain);
                return chain.mForm;
            };
            ASSERT_EQ(sLargestBc7ChainSide, 512u) << "the edge this test stands at";
            EXPECT_EQ(made(*makeBc1(512, 4, 1, false), TextureEncoding::Colour), ChainForm::Bc7);
            EXPECT_EQ(made(*makeBc1(516, 4, 1, false), TextureEncoding::Colour), ChainForm::Coarser);
            EXPECT_EQ(made(*makeBc1(8, 8, 2, true), TextureEncoding::Painted), ChainForm::Bc7);
            EXPECT_EQ(made(*makeBc1(8, 8, 2, true), TextureEncoding::Data), ChainForm::Stands);
            EXPECT_EQ(made(*makeBc1(8, 8, 2, false), TextureEncoding::Data), ChainForm::Coarser) << "no cut, made";
        }

        /// **A classic pair of two sizes is converted at the slot's own file's**: a 4x4 diffuse beside a 2x2
        /// `_spec` makes a 4x4 base colour, three levels in BC7, and a 2x2 metal-roughness map, two, each
        /// file reading the other's nearest texel.
        TEST(RtxPreparedImagesTest, aClassicPairOfTwoSizesIsConvertedAtTheSlotsOwnFile)
        {
            const auto make = [](int side, const char* name) {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->allocateImage(side, side, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                std::fill_n(image->data(), image->getTotalSizeInBytes(), static_cast<unsigned char>(128));
                image->setFileName(name);
                return image;
            };
            const osg::ref_ptr<osg::Image> diffuse = make(4, "textures/tx_ore.dds");
            const osg::ref_ptr<osg::Image> specular = make(2, "textures/tx_ore_spec.dds");

            ContentPreprocessor content;
            MadeChain colour;
            content.convert(
                ConversionPass::Input{
                    .mImage = *diffuse, .mPartner = *specular, .mEncoding = TextureEncoding::ConvertedColour },
                colour);
            EXPECT_EQ(colour.mForm, ChainForm::Bc7);
            EXPECT_EQ(colour.mBytes.size(), 3u * 16) << "4x4, 2x2 and 1x1, a block each";
            MadeChain map;
            content.convert(
                ConversionPass::Input{
                    .mImage = *specular, .mPartner = *diffuse, .mEncoding = TextureEncoding::ConvertedSpecular },
                map);
            EXPECT_EQ(map.mForm, ChainForm::Bc7);
            EXPECT_EQ(map.mBytes.size(), 2u * 16) << "2x2 and 1x1, a block each";
        }
    }
}
