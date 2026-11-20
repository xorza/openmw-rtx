#include <string>

#include <gtest/gtest.h>

#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/memoryprices.hpp>
#include <components/rtx/renderer/nullrenderer.hpp>

namespace Rtx
{
    namespace
    {
        /// **A chain's texels are every level's down to one texel**, a block format's levels rounded up to whole
        /// blocks: a BC1 square of 8 is four blocks, then one, one and one, 7 × 8 bytes; an RGBA8 square of 4 is
        /// 16 + 4 + 1 texels, 84 bytes; a single texel is one block, or four bytes. The levels are the side's
        /// halvings and itself.
        TEST(RtxMemoryPricesTest, aChainsTexelsAreEveryLevelsDownToOneTexel)
        {
            EXPECT_EQ(chainTexelBytes(TextureFormat::Bc1RgbaSrgb, 8), 56u);
            EXPECT_EQ(chainTexelBytes(TextureFormat::Bc3Srgb, 8), 112u);
            EXPECT_EQ(chainTexelBytes(TextureFormat::Rgba8Srgb, 4), 84u);
            EXPECT_EQ(chainTexelBytes(TextureFormat::Bc1RgbaSrgb, 1), 8u);
            EXPECT_EQ(chainTexelBytes(TextureFormat::Rgba8Srgb, 1), 4u);

            EXPECT_EQ(chainLevels(1), 1u);
            EXPECT_EQ(chainLevels(2), 2u);
            EXPECT_EQ(chainLevels(64), 7u);
            EXPECT_EQ(chainLevels(4096), 13u);
        }

        /// **The report is a line a price**, what the device asks beside what the texels are and the difference, a
        /// triangle's share of a structure, and the kinds of memory by their order.
        TEST(RtxMemoryPricesTest, theReportIsALineAPrice)
        {
            MemoryPrices prices;
            prices.mKinds
                = { MemoryKind{ .mHeap = 1, .mHeapBytes = 64ull << 30, .mOnDevice = false, .mHostVisible = true },
                      MemoryKind{ .mHeap = 0, .mHeapBytes = 16ull << 30, .mOnDevice = true, .mHostVisible = false } };
            prices.mStructureKinds = 0b10;
            prices.mAllocationsMost = 4096;
            prices.mGranularity = 1024;
            prices.mTextures = { TexturePrice{ .mFormat = TextureFormat::Bc1RgbaSrgb,
                .mSide = 64,
                .mLevels = 7,
                .mTexelBytes = 2768,
                .mBytes = 65536,
                .mAlignment = 65536,
                .mKinds = 0b11 } };
            prices.mStructures = { StructurePrice{ .mTriangles = 1024, .mBytes = 65536, .mScratchBytes = 4096 } };

            const std::string name(nameOf(TextureFormat::Bc1RgbaSrgb));
            EXPECT_EQ(describe(prices),
                "kinds of memory\n"
                "   0  heap 1, 65536 MiB, mapped by the host\n"
                "   1  heap 0, 16384 MiB, the device's own\n"
                "  a structure's buffer takes kinds 1\n"
                "  4096 allocations at most, 1024 bytes between a buffer and an image\n"
                "\ntextures, a square with its whole chain: the device's bytes, the texels', what is over, the "
                "alignment, the kinds\n"
                "  " + name + std::string(18 - name.size(), ' ')
                    + "    64   7 levels       65536        2768     62768    65536  0, 1\n"
                "\nstructures as first built, by triangles: the structure's bytes, a triangle's share, the build's "
                "scratch\n"
                "     1024       65536      64.0        4096\n");
        }

        /// **With no device a texture costs its texels**, every priced format at every side to 4096, 13 a format, in
        /// the order the formats are priced in.
        TEST(RtxMemoryPricesTest, withNoDeviceATextureCostsItsTexels)
        {
            const MemoryPrices prices = NullRenderer(RendererOptions{}).priceMemory();
            ASSERT_EQ(prices.mTextures.size(), sPricedFormats.size() * 13);
            EXPECT_EQ(prices.mTextures[0],
                (TexturePrice{ .mFormat = sPricedFormats[0],
                    .mSide = 1,
                    .mLevels = 1,
                    .mTexelBytes = 8,
                    .mBytes = 8,
                    .mAlignment = 1,
                    .mKinds = 0 }));
            EXPECT_EQ(prices.mTextures[12].mSide, 4096u);
            EXPECT_EQ(prices.mTextures[13].mFormat, sPricedFormats[1]);
            EXPECT_TRUE(prices.mStructures.empty());
        }
    }
}
