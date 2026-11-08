#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC and RGTC formats, which Windows's and Apple's gl.h lack.
#include <osg/ref_ptr>

#include <rgbcx/rgbcx.h>

#include <apps/components_tests/rtx/support/firsttexel.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    namespace
    {
        /// An image of `format` laid as `levels` back to back, from `bytes`, as a file's loader hands
        /// one over.
        osg::ref_ptr<osg::Image> makeChained(GLenum format, GLenum pixel, std::uint32_t side,
            const std::vector<std::size_t>& levels, const std::vector<std::uint8_t>& bytes)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new unsigned char[bytes.size()];
            std::memcpy(data, bytes.data(), bytes.size());
            image->setImage(static_cast<int>(side), static_cast<int>(side), 1, static_cast<GLint>(format), pixel,
                GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            osg::Image::MipmapDataType offsets;
            std::size_t at = 0;
            for (std::size_t level = 0; level + 1 < levels.size(); ++level)
            {
                at += levels[level];
                offsets.push_back(static_cast<unsigned int>(at));
            }
            image->setMipmapLevels(offsets);
            image->setFileName("textures/tx_test_nh.dds");
            return image;
        }

        TextureData describe(const osg::Image& image, TextureEncoding encoding, std::vector<MipLevel>& levels,
            std::vector<std::byte>& texels)
        {
            return describeImage(image, encoding, levels, texels).value();
        }

        /// **A normal map's chain is the direction of the mean of its unit normals, every level made**: a
        /// 2x2 RGBA8 map whose file's second level is the premultiplied black is made again whole. Each
        /// finest texel is `2 c − 1` made unit: (120, 128, 247) is (−0.058824, 0.003922, 0.937255), of
        /// length 0.939108, so (−0.062638, 0.004176, 0.998023), whose bytes `(n + 1) / 2 · 255` are
        /// (119.51, 128.03, 254.75), so (120, 128, 255), its height 100 as it was. The second level is the
        /// mean of the four unit normals, (−0.044833, 0.019658, 0.996999), of length 0.998200, so
        /// (−0.044914, 0.019694, 0.998803) and the bytes (121.77, 130.01, 254.85): (122, 130, 255), with
        /// the heights' plain mean `(0 + 0 + 100 + 200) / 4 = 75` beside it. Each level is one BC7 block
        /// in mode 5, which keeps the height apart, the second within BC7's step of what was built.
        TEST(RtxChainBuildTest, aNormalMapsChainIsTheDirectionOfTheMeanOfItsUnitNormals)
        {
            const std::vector<std::uint8_t> bytes{
                128, 128, 255, 0, //
                128, 136, 255, 0, //
                120, 128, 247, 100, //
                112, 128, 251, 200, //
                0, 0, 0, 0, // the file's own second level: premultiplied black
            };
            const osg::ref_ptr<osg::Image> image = makeChained(GL_RGBA8, GL_RGBA, 2, { 16, 4 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Normal, levels, texels);
            ASSERT_TRUE(ChainBuild::rebuilds(file));

            ChainBuild chain;
            chain.build(file, 2);
            ASSERT_EQ(chain.getLevelCount(), 2u);
            const auto texelOf = [&](std::uint32_t level, std::size_t at) {
                const std::span<const std::byte> laid = chain.getTexels(level).subspan(at * 4, 4);
                return Testing::Texel{ std::to_integer<std::uint8_t>(laid[0]), std::to_integer<std::uint8_t>(laid[1]),
                    std::to_integer<std::uint8_t>(laid[2]), std::to_integer<std::uint8_t>(laid[3]) };
            };
            EXPECT_EQ(texelOf(0, 0), (Testing::Texel{ 128, 128, 255, 0 }));
            EXPECT_EQ(texelOf(0, 2), (Testing::Texel{ 120, 128, 255, 100 })) << "made unit";
            EXPECT_EQ(texelOf(1, 0), (Testing::Texel{ 122, 130, 255, 75 }));

            // Each level one BC7 block, its mode its first byte's lowest set bit: mode 5, which keeps the
            // height apart, for a block that holds one.
            std::vector<std::byte> encoded;
            chain.encodeBc7(TextureEncoding::Normal, encoded);
            ASSERT_EQ(encoded.size(), 32u);
            for (std::size_t block = 0; block < 2; ++block)
                EXPECT_EQ(std::to_integer<unsigned>(encoded[block * 16]) & 0x3fu, 0x20u) << "block " << block;
            EXPECT_TRUE(Testing::isNear(
                Testing::firstTexelOf(TextureFormat::Bc7Unorm, std::span(encoded).subspan(16)), texelOf(1, 0), 2));
        }

        /// **A map of two channels is given its blue, and a mean that points nowhere is made straight
        /// up**: a BC5 map of one block whose red is 191 and green 128 decodes to (0.498039, 0.003922),
        /// its blue raised to `sqrt(1 − 0.248043 − 0.000015) = 0.867146`, so the bytes (191.00, 128.50,
        /// 238.06): (191, 128, 238), and its second level the same. A 2x2 RGBA8 map of (255, 255, 255)
        /// and (0, 0, 0) twice each decodes to (1, 1, 1) and (−1, −1, −1), whose unit normals cancel: its
        /// second level is up, `(0 + 1) / 2 · 255 = 127.5`, so (128, 128, 255). The BC5 map, which holds
        /// no height, is encoded in BC7's mode 6.
        TEST(RtxChainBuildTest, aMapOfTwoChannelsIsGivenItsBlueAndAMeanOfNoDirectionIsUp)
        {
            // BC5: two BC4 blocks, each its two endpoints and every index nought, the first endpoint's.
            std::vector<std::uint8_t> bytes(32, 0);
            bytes[0] = 191;
            bytes[1] = 191;
            bytes[8] = 128;
            bytes[9] = 128;
            bytes[16] = 191;
            bytes[17] = 191;
            bytes[24] = 128;
            bytes[25] = 128;
            const osg::ref_ptr<osg::Image> two = makeChained(
                GL_COMPRESSED_RED_GREEN_RGTC2_EXT, GL_COMPRESSED_RED_GREEN_RGTC2_EXT, 4, { 16, 16 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*two, TextureEncoding::Normal, levels, texels);
            ASSERT_TRUE(ChainBuild::rebuilds(file));
            EXPECT_FALSE(ChainBuild::rebuilds(describe(*two, TextureEncoding::Data, levels, texels)))
                << "a map of data's two channels mean nothing a reader here knows";

            ChainBuild chain;
            chain.build(describe(*two, TextureEncoding::Normal, levels, texels), 2);
            for (std::uint32_t level = 0; level < 2; ++level)
            {
                const std::span<const std::byte> first = chain.getTexels(level).first(4);
                EXPECT_EQ(std::to_integer<int>(first[0]), 191) << "level " << level;
                EXPECT_EQ(std::to_integer<int>(first[1]), 128) << "level " << level;
                EXPECT_EQ(std::to_integer<int>(first[2]), 238) << "level " << level;
                EXPECT_EQ(std::to_integer<int>(first[3]), 255) << "level " << level << ", no height";
            }

            std::vector<std::byte> opaque;
            chain.encodeBc7(TextureEncoding::Normal, opaque);
            EXPECT_EQ(std::to_integer<unsigned>(opaque[0]) & 0x7fu, 0x40u) << "mode 6 for a block with no height";

            const osg::ref_ptr<osg::Image> cancelling = makeChained(
                GL_RGBA8, GL_RGBA, 2, { 16 }, { 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255 });
            chain.build(describe(*cancelling, TextureEncoding::Normal, levels, texels), 2);
            const std::span<const std::byte> up = chain.getTexels(1);
            EXPECT_EQ(std::to_integer<int>(up[0]), 128);
            EXPECT_EQ(std::to_integer<int>(up[1]), 128);
            EXPECT_EQ(std::to_integer<int>(up[2]), 255);
        }

        /// **A colour's chain weighs it by its coverage, and the channels keep the file's order**: a
        /// BGRA8 colour whose one solid texel is pure red, beside three holes of white, is made again
        /// red under a quarter's alpha, written blue, green, red.
        TEST(RtxChainBuildTest, aColoursChainWeighsItByCoverageInTheFilesOwnChannelOrder)
        {
            const std::vector<std::uint8_t> bytes{
                0, 0, 255, 255, //
                255, 255, 255, 0, //
                255, 255, 255, 0, //
                255, 255, 255, 0, //
                9, 9, 9, 9, //
            };
            const osg::ref_ptr<osg::Image> image = makeChained(GL_RGBA8, GL_BGRA, 2, { 16, 4 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Colour, levels, texels);
            ASSERT_TRUE(ChainBuild::rebuilds(file));

            ChainBuild chain;
            chain.build(file, 2);
            std::vector<std::byte> built;
            chain.encode(file, 1, built);
            ASSERT_EQ(built.size(), 4u);
            EXPECT_EQ(std::to_integer<int>(built[0]), 0) << "blue";
            EXPECT_EQ(std::to_integer<int>(built[1]), 0) << "green";
            EXPECT_EQ(std::to_integer<int>(built[2]), 255) << "red, the solid texel's alone";
            EXPECT_EQ(std::to_integer<int>(built[3]), 64) << "a quarter's coverage";
        }

        /// **A block-compressed chain is encoded in the file's own format**: a BC3 map of data of one
        /// block whose sixteen texels are one value and one alpha decodes, made again, to that texel within
        /// the encoder's step, and its second level is one block of sixteen bytes, as the file's was.
        TEST(RtxChainBuildTest, aBlockCompressedChainIsEncodedInTheFilesOwnFormat)
        {
            std::array<std::uint8_t, 64> solid{};
            for (std::size_t texel = 0; texel < 16; ++texel)
            {
                solid[texel * 4 + 0] = 100;
                solid[texel * 4 + 1] = 150;
                solid[texel * 4 + 2] = 240;
                solid[texel * 4 + 3] = 90;
            }
            rgbcx::init();
            std::vector<std::uint8_t> bytes(32);
            rgbcx::encode_bc3(10, bytes.data(), solid.data());
            std::memset(bytes.data() + 16, 0, 16);
            const osg::ref_ptr<osg::Image> image
                = makeChained(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, { 16, 16 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Data, levels, texels);
            ASSERT_EQ(file.mLevels.size(), 2u);
            ASSERT_TRUE(ChainBuild::rebuilds(file));

            ChainBuild chain;
            chain.build(file, 2);
            std::vector<std::byte> built;
            chain.encode(file, 1, built);
            ASSERT_EQ(built.size(), 16u);

            std::array<std::uint8_t, 64> decoded{};
            rgbcx::unpack_bc3(built.data(), decoded.data());
            for (std::size_t channel = 0; channel < 4; ++channel)
                EXPECT_NEAR(decoded[channel], solid[channel], 4) << "channel " << channel;
        }

        /// What is made and what is not: a BC1 file with a hole in it holds a cut, and a file of one
        /// texel has no level under it; a file of one level and more than a texel has its chain made.
        TEST(RtxChainBuildTest, aCutAndAFileOfOneTexelAreNotMadeAndAFileOfOneLevelIs)
        {
            std::vector<std::uint8_t> holed(16, 0);
            // BC1, its first endpoint not above its second, every index 3: transparent black.
            holed[4] = 0xff;
            holed[5] = 0xff;
            holed[6] = 0xff;
            holed[7] = 0xff;
            holed[12] = holed[4];
            holed[13] = holed[5];
            holed[14] = holed[6];
            holed[15] = holed[7];
            const osg::ref_ptr<osg::Image> cut
                = makeChained(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, { 8, 8 }, holed);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*cut, TextureEncoding::Normal, levels, texels);
            ASSERT_TRUE(ChainBuild::rebuilds(file));
            ChainBuild chain;
            chain.build(file, 2);
            EXPECT_TRUE(chain.holdsCut(file));

            const osg::ref_ptr<osg::Image> single
                = makeChained(GL_RGBA8, GL_RGBA, 2, { 16 }, std::vector<std::uint8_t>(16, 7));
            EXPECT_TRUE(ChainBuild::rebuilds(describe(*single, TextureEncoding::Normal, levels, texels)));

            const osg::ref_ptr<osg::Image> texel
                = makeChained(GL_RGBA8, GL_RGBA, 1, { 4 }, std::vector<std::uint8_t>(4, 7));
            EXPECT_FALSE(ChainBuild::rebuilds(describe(*texel, TextureEncoding::Normal, levels, texels)));
        }

        /// **A loose file's own levels are encoded whole in BC7, weighed by its encoding**: a 4x4 BGRA8
        /// file of two levels, its finest scattered colours and its second one colour, is laid in red, green, blue
        /// and alpha, the file's (10, 20, 30, 255) its first texel, and encoded a block a level, 2 × 16 =
        /// 32 bytes, its second level within BC7's step of that colour in either encoding. The scatter,
        /// which no block holds exactly, is weighed as the eye sees a colour and channel for channel as
        /// data, so its blocks differ.
        TEST(RtxChainBuildTest, aLooseFilesOwnLevelsAreEncodedWholeInBc7WeighedByItsEncoding)
        {
            std::vector<std::uint8_t> bytes{ 30, 20, 10, 255 };
            for (unsigned texel = 1; texel < 16; ++texel)
                bytes.insert(bytes.end(),
                    { static_cast<std::uint8_t>(texel * 29 + 201), static_cast<std::uint8_t>(texel * 151 + 97),
                        static_cast<std::uint8_t>(texel * 73 + 11), 255 });
            for (int texel = 0; texel < 4; ++texel)
                bytes.insert(bytes.end(), { 60, 120, 180, 255 });
            const osg::ref_ptr<osg::Image> image = makeChained(GL_RGBA8, GL_BGRA, 4, { 64, 16 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Colour, levels, texels);
            ASSERT_EQ(file.mFormat, TextureFormat::Bgra8Srgb);

            ChainBuild chain;
            chain.layOwnLevels(file);
            ASSERT_EQ(chain.getLevelCount(), 2u);
            const std::span<const std::byte> finest = chain.getTexels(0);
            EXPECT_EQ(std::to_integer<int>(finest[0]), 10) << "red, from the file's third byte";
            EXPECT_EQ(std::to_integer<int>(finest[1]), 20);
            EXPECT_EQ(std::to_integer<int>(finest[2]), 30) << "blue, from the file's first byte";
            EXPECT_EQ(std::to_integer<int>(finest[3]), 255);

            std::vector<std::byte> colour;
            chain.encodeBc7(TextureEncoding::Colour, colour);
            std::vector<std::byte> data;
            chain.encodeBc7(TextureEncoding::Data, data);
            ASSERT_EQ(colour.size(), 32u);
            ASSERT_EQ(data.size(), 32u);
            EXPECT_NE(std::vector<std::byte>(colour.begin(), colour.begin() + 16),
                std::vector<std::byte>(data.begin(), data.begin() + 16))
                << "the scatter weighed one way";
            for (const std::vector<std::byte>* encoded : { &colour, &data })
                EXPECT_TRUE(
                    Testing::isNear(Testing::firstTexelOf(TextureFormat::Bc7Srgb, std::span(*encoded).subspan(16)),
                        { 180, 120, 60, 255 }, 2));
        }

        /// **A painted colour's every level is delit where it stands, in linear light, held at one**:
        /// `Testing::paintTwoTones` says the estimate. Bright across its middle half, the texel at the
        /// middle column, 64, is 1.0 under a factor of 1.501, which is 0.66622 in light and encodes to
        /// 213; one tone across, the estimate is one everywhere and 255 stays. Over its first three
        /// quarters `(0.3325, 0.3325, 1)` and white past them, column 64 estimates to 0.7109 and divides
        /// to `(0.468, 0.468, 1.407)`, which brought down whole is the texel again: 156, 156, 255. The
        /// chain is made to a texel, every level under the finest boxed from the delit one. **The
        /// wrap decides the estimate**: bright over its left half, column 1 is blurred across the edge
        /// with the dark far side where the texture repeats, and with itself where it clamps.
        TEST(RtxChainBuildTest, aPaintedColoursEveryLevelIsDelitWhereItStands)
        {
            const auto built = [](Testing::TestTexture&& texture, TextureWrap wrap, std::uint32_t x) {
                texture.mData.mEncoding = TextureEncoding::Painted;
                texture.mData.mWrap = wrap;
                ChainBuild chain;
                chain.build(texture.mData, levelsTo1x1(Testing::sTwoTonesExtent, Testing::sTwoTonesExtent));
                EXPECT_EQ(chain.getLevelCount(), 8u) << "128 down to a texel";
                const std::span<const std::byte> texel = chain.getTexels(0).subspan(std::size_t{ x } * 4, 4);
                return Testing::Texel{ std::to_integer<std::uint8_t>(texel[0]), std::to_integer<std::uint8_t>(texel[1]),
                    std::to_integer<std::uint8_t>(texel[2]), std::to_integer<std::uint8_t>(texel[3]) };
            };

            EXPECT_TRUE(Testing::isNear(
                built(Testing::paintTwoTones(32, 96), TextureWrap::Repeat, 64), { 213, 213, 213, 255 }, 1))
                << "a texture painted half again as bright comes back";
            EXPECT_EQ(
                built(Testing::paintTwoTones(0, 128), TextureWrap::Repeat, 64), (Testing::Texel{ 255, 255, 255, 255 }))
                << "and a neutral map changes nothing";

            Testing::TestTexture tinted = Testing::paintTwoTones(96, 128);
            for (std::size_t texel = 0; texel < tinted.mBytes.size(); texel += 4)
                tinted.mBytes[texel + 2] = 255;
            EXPECT_TRUE(Testing::isNear(built(std::move(tinted), TextureWrap::Repeat, 64), { 156, 156, 255, 255 }, 1))
                << "a colour divided past one is brought down whole";

            EXPECT_NE(built(Testing::paintTwoTones(0, 64), TextureWrap::Repeat, 1),
                built(Testing::paintTwoTones(0, 64), TextureWrap::ClampS, 1))
                << "the estimate follows the wrap";
        }
    }
}
