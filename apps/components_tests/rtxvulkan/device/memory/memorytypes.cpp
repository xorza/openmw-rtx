#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/cardmemory.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkMemoryPropertyFlags sVideo = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        constexpr VkMemoryPropertyFlags sStaging
            = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        constexpr VkMemoryPropertyFlags sReadBack = sStaging | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

        /// **Each request is placed in the types its promise names**, on every card in hand: video
        /// memory in the types that are video memory and nothing else, the tables the host writes
        /// in the window, staging in write-combined system memory, a read-back in cached system
        /// memory. RADV lists two of each, a second that only buffers take, and four of AMD's
        /// device-coherent types after them, none of which any request is placed in.
        TEST(RtxMemoryTypesTest, eachRequestIsPlacedInTheTypesItsPromiseNames)
        {
            const struct
            {
                const char* mCard;
                VkPhysicalDeviceMemoryProperties mMemory;
                std::uint32_t mVideo;
                std::uint32_t mHostWritten;
                std::uint32_t mStaging;
                std::uint32_t mReadBack;
            } cards[] = {
                { "Turing", Testing::turingMemory(), 1u << 1, 1u << 4, 1u << 2, 1u << 3 },
                { "Ada", Testing::adaMemory(), 1u << 1, 1u << 4, 1u << 2, 1u << 3 },
                { "RDNA 2", Testing::rdna2Memory(), (1u << 0) | (1u << 1), (1u << 3) | (1u << 4), 1u << 2,
                    (1u << 5) | (1u << 6) },
            };
            for (const auto& card : cards)
            {
                EXPECT_EQ(memoryTypesFor(card.mMemory, sVideo), card.mVideo) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sHostWritten), card.mHostWritten) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sStaging), card.mStaging) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sReadBack), card.mReadBack) << card.mCard;
            }
        }

        /// **Whatever order the driver lists its types in.** The library takes the first type that
        /// has what is asked, so a Turing whose window came before its system memory put staging in
        /// the window; the request names the type where it now stands. And a card with nothing but
        /// host-visible video memory, as an integrated one is, has no type that is staging alone,
        /// so staging goes wherever the request's properties are.
        TEST(RtxMemoryTypesTest, theOrderTheDriverListsItsTypesInDecidesNothing)
        {
            VkPhysicalDeviceMemoryProperties windowFirst = Testing::turingMemory();
            std::swap(windowFirst.memoryTypes[2], windowFirst.memoryTypes[4]);
            EXPECT_EQ(memoryTypesFor(windowFirst, sStaging), 1u << 4) << "staging in the window";
            EXPECT_EQ(memoryTypesFor(windowFirst, sHostWritten), 1u << 2);

            VkPhysicalDeviceMemoryProperties shared{};
            shared.memoryHeapCount = 1;
            shared.memoryHeaps[0] = VkMemoryHeap{ 8589934592ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
            shared.memoryTypeCount = 2;
            shared.memoryTypes[0] = VkMemoryType{ sVideo | sStaging, 0 };
            shared.memoryTypes[1] = VkMemoryType{ sVideo | sReadBack, 0 };
            EXPECT_EQ(memoryTypesFor(shared, sStaging), (1u << 0) | (1u << 1));
            EXPECT_EQ(memoryTypesFor(shared, sReadBack), 1u << 1);
            EXPECT_EQ(memoryTypesFor(shared, sVideo), (1u << 0) | (1u << 1));

            VkPhysicalDeviceMemoryProperties none = Testing::turingMemory();
            none.memoryTypeCount = 2;
            EXPECT_EQ(memoryTypesFor(none, sReadBack), 0u) << "no type has what is asked";
        }
    }
}
