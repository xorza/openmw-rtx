#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC and RGTC formats, which Windows's gl.h lacks.
#include <osg/ref_ptr>

#include <rgbcx/rgbcx.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/firsttexel.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/misc/bytebuffer.hpp>
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

        /// Adds one level to `texture`, painted from `bytes` or left blank where none are given.
        void addLevel(Testing::TestTexture& texture, std::uint32_t width, std::uint32_t height,
            std::initializer_list<std::uint8_t> bytes = {})
        {
            texture.mLevels.push_back(MipLevel{ static_cast<std::uint32_t>(texture.mBytes.size()), width, height });
            if (bytes.size() > 0)
                texture.mBytes.insert(texture.mBytes.end(), bytes);
            else
                texture.mBytes.resize(texture.mBytes.size() + std::size_t{ width } * height * 4);
        }

        /// One channel of one texel of a built level.
        std::uint32_t channelAt(
            const ChainBuild& chain, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::size_t channel)
        {
            const MipLevel& which = chain.getLevel(level);
            return std::to_integer<std::uint32_t>(
                chain.getTexels(level)[(std::size_t{ y } * which.mWidth + x) * 4 + channel]);
        }

        /// **Every level built is the box of the one above it.** Four quads of one value apiece — 100,
        /// 200, 0 and 40 — so the second level is those four numbers and the third is their mean,
        /// `(100 + 200 + 0 + 40) / 4 = 85`. Opaque throughout, which is what makes the weighing below a
        /// separate question from this one. The finest level is the file's own.
        TEST(RtxChainBuildTest, everyLevelBuiltIsTheBoxOfTheOneAboveIt)
        {
            Testing::TestTexture four;
            for (const std::uint8_t value : { 100, 200, 100, 200, 0, 40, 0, 40 })
                for (int twice = 0; twice < 2; ++twice)
                    for (const std::uint8_t byte : { value, value, value, std::uint8_t{ 255 } })
                        four.mBytes.push_back(byte);
            four.mLevels.push_back(MipLevel{ 0, 4, 4 });
            four.describe(4, 4, "four");

            ChainBuild chain;
            chain.build(four.mData, levelsTo1x1(4, 4));
            ASSERT_EQ(chain.getLevelCount(), 3u) << "4 by 4 runs down to one texel in three levels";
            EXPECT_EQ(channelAt(chain, 0, 0, 0, 0), 100u);
            EXPECT_EQ(channelAt(chain, 0, 3, 3, 0), 40u);
            EXPECT_EQ(chain.getLevel(1).mWidth, 2u);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 100u);
            EXPECT_EQ(channelAt(chain, 1, 1, 0, 0), 200u);
            EXPECT_EQ(channelAt(chain, 1, 0, 1, 0), 0u);
            EXPECT_EQ(channelAt(chain, 1, 1, 1, 0), 40u);
            EXPECT_EQ(chain.getLevel(2).mWidth, 1u);
            EXPECT_EQ(channelAt(chain, 2, 0, 0, 0), 85u) << "the mean of the four quads";
            EXPECT_EQ(channelAt(chain, 2, 0, 0, 3), 255u) << "nothing was transparent, so nothing faded";
        }

        /// **A colour is weighed by the alpha carrying it, and the alpha is not.** A punch-through block
        /// stores black where it painted nothing, so an even mean draws a dark rim round every leaf card
        /// one level down: two white texels at full alpha beside two black ones at none come to white at
        /// half alpha, where an even mean would come to 127. A quad with nothing painted in it still has
        /// the even mean, 40; and data's alpha is a channel like the others, its box even, the white and
        /// the black meeting at 127.5, which rounds to 128.
        TEST(RtxChainBuildTest, aColourIsWeighedByTheAlphaCarryingItAndTheAlphaIsNot)
        {
            Testing::TestTexture pair;
            addLevel(pair, 2, 2, { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 });
            pair.describe(2, 2, "pair");
            ChainBuild chain;
            chain.build(pair.mData, 2);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 255u) << "the black the transparent texels stored was averaged in";
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 3), 128u) << "half of it was painted";

            Testing::TestTexture empty;
            addLevel(empty, 2, 2, { 60, 60, 60, 0, 20, 20, 20, 0, 60, 60, 60, 0, 20, 20, 20, 0 });
            empty.describe(2, 2, "empty");
            chain.build(empty.mData, 2);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 40u);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 3), 0u);

            Testing::TestTexture data;
            addLevel(data, 2, 2, { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 });
            data.describe(2, 2, "data");
            data.mData.mEncoding = TextureEncoding::Data;
            chain.build(data.mData, 2);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 128u) << "data was weighed by its alpha";
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 3), 128u);
        }

        /// **An odd extent is halved by the box of its own width**, three taps a texel, so its last texel is
        /// read. Five texels along a line, the last of them 250 and the rest nought: level one is two texels,
        /// `(2, 2, 1) / 5` over texels 0 to 2 and `(1, 2, 2) / 5` over 2 to 4, so nought and
        /// `250 * 2 / 5 = 100`; level two is their mean, 50. Halved as two, texel 4 was never read.
        TEST(RtxChainBuildTest, anOddExtentIsHalvedByTheBoxOfItsOwnWidth)
        {
            Testing::TestTexture line;
            for (const std::uint8_t value : { 0, 0, 0, 0, 250 })
                for (const std::uint8_t byte : { value, value, value, std::uint8_t{ 255 } })
                    line.mBytes.push_back(byte);
            line.mLevels.push_back(MipLevel{ 0, 5, 1 });
            line.describe(5, 1, "line");

            ChainBuild chain;
            chain.build(line.mData, levelsTo1x1(5, 1));
            ASSERT_EQ(chain.getLevelCount(), 3u) << "five texels run down to one in three levels";
            ASSERT_EQ(chain.getLevel(1).mWidth, 2u);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 0u);
            EXPECT_EQ(channelAt(chain, 1, 1, 0, 0), 100u) << "the last texel of an odd extent was dropped";
            EXPECT_EQ(channelAt(chain, 2, 0, 0, 0), 50u);
            EXPECT_EQ(channelAt(chain, 2, 0, 0, 3), 255u) << "the weights did not sum to one";
        }

        /// **A display-encoded texture is averaged in light and written back encoded**: half of nothing
        /// and half of white meet at 188 in light and at 128 in bytes (`Rtx::toLinear`).
        TEST(RtxChainBuildTest, aDisplayEncodedTextureIsAveragedInLight)
        {
            Testing::TestTexture pair;
            addLevel(pair, 2, 2, { 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255 });
            pair.describe(2, 2, "pair", TextureFormat::Rgba8Srgb);
            ChainBuild chain;
            chain.build(pair.mData, 2);
            EXPECT_EQ(channelAt(chain, 1, 0, 0, 0), 188u);
        }

        /// **A chain built again is the texture it was handed and nothing of the one before, and costs the
        /// heap nothing where it already grew for its shape**: a 4x4 built to three levels, a 2x2 after it
        /// to two, and the 4x4 again to three with no allocation, as a thread that makes chain after chain
        /// keeps one build.
        TEST(RtxChainBuildTest, aChainBuiltAgainIsTheNewTextureAndKeepsTheRoomOfTheLast)
        {
            Testing::TestTexture single;
            addLevel(single, 4, 4);
            single.describe(4, 4, "single");
            Testing::TestTexture small;
            addLevel(small, 2, 2);
            small.describe(2, 2, "small");

            ChainBuild chain;
            chain.build(single.mData, 3);
            ASSERT_EQ(chain.getLevelCount(), 3u);
            chain.build(small.mData, 2);
            EXPECT_EQ(chain.getLevelCount(), 2u) << "the last texture's levels came through";

            const std::size_t before = Testing::getAllocationCount();
            chain.build(single.mData, 3);
            const std::size_t spent = Testing::getAllocationCount() - before;
            EXPECT_EQ(spent, 0u) << "a rebuild reached the heap " << spent << " times";
            EXPECT_EQ(chain.getLevelCount(), 3u);
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
            Misc::ByteBuffer encoded;
            chain.encodeBc7(TextureEncoding::Normal, encoded);
            ASSERT_EQ(encoded.size(), 32u);
            EXPECT_EQ(encoded.capacity(), 32u) << "the chain's room taken once, and no more";
            for (std::size_t block = 0; block < 2; ++block)
                EXPECT_EQ(std::to_integer<unsigned>(encoded[block * 16]) & 0x3fu, 0x20u) << "block " << block;
            EXPECT_TRUE(Testing::isNear(
                Testing::firstTexelOf(TextureFormat::Bc7Unorm, std::span(encoded).subspan(16)), texelOf(1, 0), 2));
        }

        /// **A normal map's chain is the same streamed through a ring of rows as built whole**: a 23x23
        /// map of noise, odd so every box reads three rows and its windows cross the reader's bands of four,
        /// makes the five levels down to a texel whose bytes, every level's, hash to what the build that kept
        /// every finest mean made.
        TEST(RtxChainBuildTest, aNormalMapsChainStreamedThroughItsRowsIsTheChainBuiltWhole)
        {
            constexpr std::uint32_t side = 23;
            std::vector<std::uint8_t> bytes(std::size_t{ side } * side * 4);
            std::uint32_t state = 12345;
            for (std::uint8_t& byte : bytes)
            {
                state = state * 1664525u + 1013904223u;
                byte = static_cast<std::uint8_t>(state >> 24);
            }
            const osg::ref_ptr<osg::Image> image = makeChained(GL_RGBA8, GL_RGBA, side, { bytes.size() }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Normal, levels, texels);

            ChainBuild chain;
            chain.build(file, levelsTo1x1(side, side));
            ASSERT_EQ(chain.getLevelCount(), 5u);
            std::uint64_t hash = 14695981039346656037u;
            for (std::uint32_t level = 0; level < chain.getLevelCount(); ++level)
                for (const std::byte byte : chain.getTexels(level))
                {
                    hash ^= std::to_integer<std::uint8_t>(byte);
                    hash *= 1099511628211u;
                }
            EXPECT_EQ(hash, 0xfa13c554e764e17du) << std::hex << hash;
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

            Misc::ByteBuffer opaque;
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

        /// **A colour's chain weighs it by its coverage, read in the file's own channel order**: a BGRA8
        /// colour whose one solid texel is pure red, beside three holes of white, is built again red
        /// under a quarter's alpha, laid red, green, blue, as every built level is.
        TEST(RtxChainBuildTest, aColoursChainWeighsItByCoverageReadInTheFilesOwnChannelOrder)
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
            const std::span<const std::byte> built = chain.getTexels(1);
            ASSERT_EQ(built.size(), 4u);
            EXPECT_EQ(std::to_integer<int>(built[0]), 255) << "red, the solid texel's alone";
            EXPECT_EQ(std::to_integer<int>(built[1]), 0) << "green";
            EXPECT_EQ(std::to_integer<int>(built[2]), 0) << "blue";
            EXPECT_EQ(std::to_integer<int>(built[3]), 64) << "a quarter's coverage";
        }

        /// **A BC2 chain's alpha is written whole, into a buffer that held other bytes**: a 4x4 BC2 file
        /// of white under sixteen alphas of nibble 8, 136 of 255, is made again under the same alpha, whose
        /// nibble is (136 × 15 + 127) / 255 = 8: every one of the eight alpha bytes 0x88, though the
        /// buffer encoded into held 0xFF where they land.
        TEST(RtxChainBuildTest, aBc2ChainsAlphaIsWrittenWhole)
        {
            const std::vector<std::uint8_t> bytes{
                0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0xff, 0xff, 0, 0, 0, 0, 0, 0, //
                0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0xff, 0xff, 0, 0, 0, 0, 0, 0, //
            };
            const osg::ref_ptr<osg::Image> image
                = makeChained(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 4, { 16, 16 }, bytes);
            std::vector<MipLevel> levels;
            std::vector<std::byte> texels;
            const TextureData file = describe(*image, TextureEncoding::Data, levels, texels);
            ASSERT_TRUE(ChainBuild::rebuilds(file));

            ChainBuild chain;
            chain.build(file, 2);
            Misc::ByteBuffer built(16, std::byte{ 0xff });
            built.clear();
            chain.encode(file, 1, built);
            ASSERT_EQ(built.size(), 16u);
            for (std::size_t at = 0; at < 8; ++at)
                EXPECT_EQ(std::to_integer<int>(built[at]), 0x88) << "alpha byte " << at;
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
            Misc::ByteBuffer built;
            chain.encode(file, 1, built);
            ASSERT_EQ(built.size(), 16u);
            EXPECT_EQ(built.capacity(), 16u) << "the chain's room taken once, and no more";

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
        /// file of two levels, its finest scattered colours and its second one colour, is read where it lies in
        /// red, green, blue and alpha, and encoded a block a level, 2 × 16 = 32 bytes, its second level, the
        /// file's (60, 120, 180) blue first, within BC7's step of (180, 120, 60) in either encoding. The scatter,
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

            Misc::ByteBuffer colour;
            ChainBuild::encodeOwnBc7(file, colour);
            TextureData asData = file;
            asData.mEncoding = TextureEncoding::Data;
            Misc::ByteBuffer data;
            ChainBuild::encodeOwnBc7(asData, data);
            ASSERT_EQ(colour.size(), 32u);
            ASSERT_EQ(data.size(), 32u);
            EXPECT_NE(Misc::ByteBuffer(colour.begin(), colour.begin() + 16),
                Misc::ByteBuffer(data.begin(), data.begin() + 16))
                << "the scatter weighed one way";
            for (const Misc::ByteBuffer* encoded : { &colour, &data })
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
