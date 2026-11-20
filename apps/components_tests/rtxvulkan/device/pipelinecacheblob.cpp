#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/misc/bytebuffer.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>

namespace Rtx
{
    namespace
    {
        /// **A cache that grows while it is read is asked again, and the last short answer is kept**: other threads
        /// compile into the cache between the size and the bytes, the driver answers `VK_INCOMPLETE` with as much as
        /// fits, and a save that returned there dropped the start's whole compile. A blob of 8 bytes that grows by 4
        /// under each of the first two reads comes whole at the third, 16 bytes; one that never stops growing is kept
        /// as the fourth try's short answer; a driver that refuses, or holds nothing, gives nothing.
        TEST(RtxPipelineCacheBlobTest, aGrowingCacheIsAskedAgainAndItsLastShortAnswerKept)
        {
            struct Driver
            {
                std::size_t mHeld = 8;
                std::uint32_t mGrows = 0;
                std::uint32_t mReads = 0;

                VkResult read(std::size_t& bytes, void* const into)
                {
                    if (into == nullptr)
                    {
                        bytes = mHeld;
                        return VK_SUCCESS;
                    }
                    ++mReads;
                    if (mGrows > 0)
                    {
                        --mGrows;
                        mHeld += 4;
                    }
                    const std::size_t written = std::min(bytes, mHeld);
                    std::fill_n(static_cast<std::byte*>(into), written, std::byte{ 0x5A });
                    const bool whole = bytes >= mHeld;
                    bytes = written;
                    return whole ? VK_SUCCESS : VK_INCOMPLETE;
                }
            };
            const auto readOf = [](Driver& driver) {
                return [&driver](std::size_t& bytes, void* const into) { return driver.read(bytes, into); };
            };

            Driver still;
            const std::optional<Misc::ByteBuffer> atOnce = PipelineCache::readBlob(readOf(still));
            ASSERT_TRUE(atOnce.has_value());
            EXPECT_EQ(atOnce->size(), 8u);
            EXPECT_EQ(still.mReads, 1u);

            Driver growing{ .mGrows = 2 };
            const std::optional<Misc::ByteBuffer> third = PipelineCache::readBlob(readOf(growing));
            ASSERT_TRUE(third.has_value());
            EXPECT_EQ(third->size(), 16u);
            EXPECT_EQ(growing.mReads, 3u);
            EXPECT_EQ(std::ranges::count(*third, std::byte{ 0x5A }), 16);

            Driver endless{ .mGrows = 100 };
            const std::optional<Misc::ByteBuffer> shortest = PipelineCache::readBlob(readOf(endless));
            ASSERT_TRUE(shortest.has_value());
            EXPECT_EQ(endless.mReads, PipelineCache::sReadTries);
            EXPECT_EQ(shortest->size(), 8u + 4u * (PipelineCache::sReadTries - 1))
                << "the last try's bytes, what the size before its growth held";

            Driver empty{ .mHeld = 0 };
            EXPECT_FALSE(PipelineCache::readBlob(readOf(empty)).has_value());
            EXPECT_FALSE(
                PipelineCache::readBlob([](std::size_t&, void*) { return VK_ERROR_OUT_OF_HOST_MEMORY; }).has_value());
            std::uint32_t asked = 0;
            EXPECT_FALSE(PipelineCache::readBlob([&](std::size_t& bytes, void* const into) {
                ++asked;
                bytes = 8;
                return into == nullptr ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }).has_value());
            EXPECT_EQ(asked, 2u) << "a refusal asked again";
        }
    }
}
