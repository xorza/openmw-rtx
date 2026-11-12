#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/image/blockdecode.hpp>

namespace Rtx
{
    namespace
    {
        std::vector<std::byte> bytesOf(std::initializer_list<int> values)
        {
            std::vector<std::byte> bytes;
            for (const int value : values)
                bytes.push_back(static_cast<std::byte>(value));
            return bytes;
        }

        /// **Each kind of block a file carries reads as the texels it holds**, worked by hand: a BC1
        /// of red over blue in four colours; a BC2 whose endpoints come in BC1's three-colour order
        /// and still read in four, its alpha a nibble a texel; a BC4 read as grey; a BC5 as red and
        /// green. A level of 6 by 5 takes four blocks and keeps the texels inside it, and a format
        /// this does not decode, or blocks short of the level, writes nothing.
        TEST(RtxBlockDecodeTest, eachBlockReadsAsTheTexelsItHolds)
        {
            // Red 0xF800 over blue 0x001F; indices 0 for the first row, 1 for the second, 2 (two
            // thirds red) for the third, 3 for the fourth.
            const std::vector<std::byte> bc1 = bytesOf({ 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x55, 0xAA, 0xFF });
            std::vector<std::uint8_t> texels(16 * 4);
            ASSERT_TRUE(decodeBlocks(TextureFormat::Bc1RgbaSrgb, bc1, 4, 4, texels));
            EXPECT_EQ((std::array{ texels[0], texels[1], texels[2], texels[3] }),
                (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }));
            EXPECT_EQ((std::array{ texels[16], texels[17], texels[18], texels[19] }),
                (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 }));
            EXPECT_EQ(texels[32], 170) << "two thirds of 255";
            EXPECT_EQ(texels[34], 85);

            // The same colour behind an alpha of 0xF for the first texel and 0x0 for the second, and
            // its endpoints swapped, which BC1 would read in three colours with a transparent fourth.
            const std::vector<std::byte> bc2
                = bytesOf({ 0x0F, 0, 0, 0, 0, 0, 0, 0, 0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF });
            ASSERT_TRUE(decodeBlocks(TextureFormat::Bc2Unorm, bc2, 4, 4, texels));
            EXPECT_EQ((std::array{ texels[0], texels[1], texels[2], texels[3] }),
                (std::array<std::uint8_t, 4>{ 170, 0, 85, 255 }))
                << "index 3 in four colours: two thirds of the second endpoint, red";
            EXPECT_EQ(texels[7], 0);

            const std::vector<std::byte> bc4 = bytesOf({ 200, 100, 0, 0, 0, 0, 0, 0 });
            ASSERT_TRUE(decodeBlocks(TextureFormat::Bc4Unorm, bc4, 4, 4, texels));
            EXPECT_EQ((std::array{ texels[0], texels[1], texels[2], texels[3] }),
                (std::array<std::uint8_t, 4>{ 200, 200, 200, 255 }));

            const std::vector<std::byte> bc5 = bytesOf({ 200, 100, 0, 0, 0, 0, 0, 0, 30, 10, 0, 0, 0, 0, 0, 0 });
            ASSERT_TRUE(decodeBlocks(TextureFormat::Bc5Unorm, bc5, 4, 4, texels));
            EXPECT_EQ((std::array{ texels[0], texels[1], texels[2], texels[3] }),
                (std::array<std::uint8_t, 4>{ 200, 30, 0, 255 }));

            // Two blocks across and two down, the second column's red and the second row's blue.
            std::vector<std::byte> four = bc1;
            for (const std::byte* block : { bc1.data(), bc1.data(), bc1.data() })
                four.insert(four.end(), block, block + 8);
            std::vector<std::uint8_t> level(6 * 5 * 4, 7);
            ASSERT_TRUE(decodeBlocks(TextureFormat::Bc1RgbaUnorm, four, 6, 5, level));
            EXPECT_EQ(level[(0 * 6 + 5) * 4], 255) << "the sixth texel of the first row, in the second block";
            EXPECT_EQ(level[(4 * 6 + 0) * 4], 255) << "the fifth row, the first of the lower blocks, index 0";
            EXPECT_EQ(level[(1 * 6 + 5) * 4 + 2], 255) << "the second row's last texel, index 1, blue";

            std::vector<std::uint8_t> untouched(16 * 4, 7);
            EXPECT_FALSE(decodeBlocks(TextureFormat::Rgba8Srgb, bc1, 4, 4, untouched));
            EXPECT_FALSE(decodeBlocks(TextureFormat::Unnamed, bc1, 4, 4, untouched));
            EXPECT_FALSE(decodeBlocks(TextureFormat::Bc3Unorm, bc1, 4, 4, untouched)) << "half a BC3 block";
            EXPECT_EQ(untouched, std::vector<std::uint8_t>(16 * 4, 7));
        }
    }
}
