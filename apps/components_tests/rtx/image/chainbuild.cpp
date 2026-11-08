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

#include <rgbcx/rgbcx.h>

#include <apps/components_tests/rtx/support/firsttexel.hpp>
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

        /// **A normal map's chain is the box of its finest level, its height no part of it**: a 2x2
        /// RGBA8 map whose file's second level is the premultiplied black, (0, 0, 0) under an alpha
        /// of 0, is made again as the plain mean of its four normals, `(128 + 128 + 120 + 112) / 4 =
        /// 122`, `(128 + 136 + 128 + 128) / 4 = 130`, `(255 + 255 + 247 + 251) / 4 = 252`, with the
        /// heights' mean `(0 + 0 + 100 + 200) / 4 = 75` beside it.
        TEST(RtxChainBuildTest, aNormalMapsChainIsTheBoxOfItsFinestLevelAndNotTheFilesOwn)
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
            EXPECT_FALSE(chain.holdsCut(file));
            std::vector<std::byte> built;
            chain.encode(file, built);
            ASSERT_EQ(built.size(), 4u);
            EXPECT_EQ(std::to_integer<int>(built[0]), 122);
            EXPECT_EQ(std::to_integer<int>(built[1]), 130);
            EXPECT_EQ(std::to_integer<int>(built[2]), 252);
            EXPECT_EQ(std::to_integer<int>(built[3]), 75);
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
            chain.encode(file, built);
            ASSERT_EQ(built.size(), 4u);
            EXPECT_EQ(std::to_integer<int>(built[0]), 0) << "blue";
            EXPECT_EQ(std::to_integer<int>(built[1]), 0) << "green";
            EXPECT_EQ(std::to_integer<int>(built[2]), 255) << "red, the solid texel's alone";
            EXPECT_EQ(std::to_integer<int>(built[3]), 64) << "a quarter's coverage";
        }

        /// **A block-compressed chain is encoded in the file's own format**: a BC3 map of one block
        /// whose sixteen texels are one normal and one height decodes, made again, to that texel within
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
            const TextureData file = describe(*image, TextureEncoding::Normal, levels, texels);
            ASSERT_EQ(file.mLevels.size(), 2u);
            ASSERT_TRUE(ChainBuild::rebuilds(file));

            ChainBuild chain;
            chain.build(file, 2);
            std::vector<std::byte> built;
            chain.encode(file, built);
            ASSERT_EQ(built.size(), 16u);

            std::array<std::uint8_t, 64> decoded{};
            rgbcx::unpack_bc3(built.data(), decoded.data());
            for (std::size_t channel = 0; channel < 4; ++channel)
                EXPECT_NEAR(decoded[channel], solid[channel], 4) << "channel " << channel;
        }

        /// What is made and what is not: a BC1 file with a hole in it holds a cut, a BC5 file is no
        /// format a host reader decodes, and a file of one texel has no level under it; a file of one
        /// level and more than a texel has its chain made.
        TEST(RtxChainBuildTest, aCutABc5FileAndAFileOfOneTexelAreNotMadeAndAFileOfOneLevelIs)
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

            const osg::ref_ptr<osg::Image> two = makeChained(GL_COMPRESSED_RED_GREEN_RGTC2_EXT,
                GL_COMPRESSED_RED_GREEN_RGTC2_EXT, 4, { 16, 16 }, std::vector<std::uint8_t>(32, 0));
            EXPECT_FALSE(ChainBuild::rebuilds(describe(*two, TextureEncoding::Normal, levels, texels)));

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
    }
}
