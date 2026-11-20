#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/memoryprices.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/memorypricing.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxMemoryPricingTest : Testing::DeviceTest
        {
        };

        /// **The device prices every format at every side up to its own, and every mesh size**, each texture at no
        /// less than its texels, at an alignment that is a power of two and that its size need be no multiple of, in a
        /// kind of memory it names, and each structure larger than the one before it: what a device states, held to
        /// what every device must state, since the figures themselves are the driver's.
        TEST_F(RtxMemoryPricingTest, theDevicePricesEveryFormatSideAndMeshSize)
        {
            const MemoryPrices prices = priceMemory(getDevice());
            const std::uint32_t sides = chainLevels(getDevice().getPhysicalDevice().getTextureSide());
            ASSERT_EQ(prices.mTextures.size(), sPricedFormats.size() * sides);
            ASSERT_FALSE(prices.mKinds.empty());
            const std::uint32_t every = prices.mKinds.size() >= 32 ? ~0u : (1u << prices.mKinds.size()) - 1;

            for (std::size_t at = 0; at < prices.mTextures.size(); ++at)
            {
                const TexturePrice& price = prices.mTextures[at];
                SCOPED_TRACE(at);
                EXPECT_EQ(price.mFormat, sPricedFormats[at / sides]);
                EXPECT_EQ(price.mSide, 1u << (at % sides));
                EXPECT_EQ(price.mLevels, at % sides + 1);
                EXPECT_EQ(price.mTexelBytes, chainTexelBytes(price.mFormat, price.mSide));
                EXPECT_GE(price.mBytes, price.mTexelBytes);
                EXPECT_TRUE(std::has_single_bit(price.mAlignment));
                EXPECT_NE(price.mKinds & every, 0u);
                EXPECT_EQ(price.mKinds & ~every, 0u);
            }

            ASSERT_EQ(prices.mStructures.size(), sPricedTriangles.size());
            for (std::size_t at = 0; at < prices.mStructures.size(); ++at)
            {
                SCOPED_TRACE(at);
                EXPECT_EQ(prices.mStructures[at].mTriangles, sPricedTriangles[at]);
                EXPECT_GT(prices.mStructures[at].mBytes, 0u);
                EXPECT_GT(prices.mStructures[at].mScratchBytes, 0u);
                if (at > 0)
                    EXPECT_GE(prices.mStructures[at].mBytes, prices.mStructures[at - 1].mBytes);
            }
            EXPECT_NE(prices.mStructureKinds & every, 0u);
            EXPECT_GT(prices.mAllocationsMost, 0u);
            EXPECT_GT(prices.mGranularity, 0u);
        }

        /// **Every side is copied at least four times, in a time the device's clock took**, and the bytes are the
        /// chains' texels: what a frame's share of copies is set from.
        TEST_F(RtxMemoryPricingTest, theDeviceCopiesEverySideAndTimesIt)
        {
            const std::vector<CopyRate> rates = measureCopies(getDevice());
            ASSERT_EQ(rates.size(), sCopiedSides.size());
            for (std::size_t at = 0; at < rates.size(); ++at)
            {
                SCOPED_TRACE(at);
                EXPECT_EQ(rates[at].mSide, sCopiedSides[at]);
                EXPECT_GE(rates[at].mCopies, 4u);
                EXPECT_EQ(
                    rates[at].mBytes, rates[at].mCopies * chainTexelBytes(TextureFormat::Bc7Srgb, rates[at].mSide));
                EXPECT_GT(rates[at].mMs, 0.0);
            }
        }
    }
}
