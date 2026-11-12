#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/cardmemory.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkMemoryPropertyFlags sVideo = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        constexpr VkMemoryPropertyFlags sStaging
            = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        constexpr VkMemoryPropertyFlags sReadBack = sStaging | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        constexpr VkMemoryPropertyFlags sMarkers
            = sStaging | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;

        /// **Each request is placed in the types its promise names**, on every card in hand: video memory in the types
        /// that are video memory and nothing else, staging in write-combined system memory, a read-back in cached
        /// system memory. RADV lists two of each, a second that only buffers take, and four device-coherent types
        /// after them, which only the markers' request, naming them, is placed in.
        TEST(RtxMemoryTypesTest, eachRequestIsPlacedInTheTypesItsPromiseNames)
        {
            const struct
            {
                const char* mCard;
                VkPhysicalDeviceMemoryProperties mMemory;
                std::uint32_t mVideo;
                std::uint32_t mStaging;
                std::uint32_t mReadBack;
                std::uint32_t mMarkers;
            } cards[] = {
                { "Turing", Testing::turingMemory(), 1u << 1, 1u << 2, 1u << 3, 0 },
                { "Ada", Testing::adaMemory(), 1u << 1, 1u << 2, 1u << 3, 0 },
                { "RDNA 2", Testing::rdna2Memory(), (1u << 0) | (1u << 1), 1u << 2, (1u << 5) | (1u << 6), 1u << 8 },
            };
            for (const auto& card : cards)
            {
                EXPECT_EQ(memoryTypesFor(card.mMemory, sVideo), card.mVideo) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sStaging), card.mStaging) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sReadBack), card.mReadBack) << card.mCard;
                EXPECT_EQ(memoryTypesFor(card.mMemory, sMarkers), card.mMarkers) << card.mCard;
            }
        }

        /// **Whatever order the driver lists its types in.** The library takes the first type that has what is asked,
        /// so a Turing whose window came before its system memory put staging in the window; the request names the
        /// type where it now stands. A card with nothing but host-visible video memory has no staging-only type, so
        /// staging goes wherever the request's properties are.
        TEST(RtxMemoryTypesTest, theOrderTheDriverListsItsTypesInDecidesNothing)
        {
            VkPhysicalDeviceMemoryProperties windowFirst = Testing::turingMemory();
            std::swap(windowFirst.memoryTypes[2], windowFirst.memoryTypes[4]);
            EXPECT_EQ(memoryTypesFor(windowFirst, sStaging), 1u << 4) << "staging in the window";

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

        /// **Content is every type that is video memory alone, and the heap they stand on**: one
        /// type on NVIDIA's cards, and on RADV two, the second taking buffers alone, so a structure
        /// and a texture stand in two pools and the room is the free space of both. A device with
        /// no such type is one the renderer cannot run on.
        TEST(RtxMemoryTypesTest, contentIsEveryTypeThatIsVideoMemoryAloneOnItsHeap)
        {
            const struct
            {
                const char* mCard;
                VkPhysicalDeviceMemoryProperties mMemory;
                std::uint32_t mTypes;
            } cards[] = {
                { "Turing", Testing::turingMemory(), 1u << 1 },
                { "Ada", Testing::adaMemory(), 1u << 1 },
                { "RDNA 2", Testing::rdna2Memory(), (1u << 0) | (1u << 1) },
            };
            for (const auto& card : cards)
            {
                const ContentTypes content = contentTypesOf(card.mMemory);
                EXPECT_EQ(content.mTypes, card.mTypes) << card.mCard;
                EXPECT_EQ(content.mHeap, 0u) << card.mCard;
            }

            VkPhysicalDeviceMemoryProperties shared{};
            shared.memoryHeapCount = 1;
            shared.memoryHeaps[0] = VkMemoryHeap{ 8589934592ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
            shared.memoryTypeCount = 1;
            shared.memoryTypes[0] = VkMemoryType{ sStaging, 0 };
            EXPECT_THROW(contentTypesOf(shared), Unsupported) << "no type is video memory";
        }

        /// **A refused essential allocation names what it was for and how full the card was**: the
        /// library's answer, the bytes, the use and the resource, then every heap as the harness
        /// prints them. Each use by its own name.
        TEST(RtxMemoryTypesTest, aRefusedTakeNamesItsSubjectAndTheHeaps)
        {
            MemoryReport report;
            report.mHeapCount = 1;
            report.mHeaps[0] = HeapUse{ .mSize = 8ull << 30,
                .mBudget = 0,
                .mHeld = 0,
                .mReserved = 7ull << 30,
                .mLive = 6ull << 30,
                .mBlocks = 96,
                .mDeviceLocal = true };
            EXPECT_EQ(describeRefusedTake(
                          VK_ERROR_OUT_OF_DEVICE_MEMORY, 268435456, MemoryUse::Essential, "the tables", report),
                "vmaAllocateMemory answered VK_ERROR_OUT_OF_DEVICE_MEMORY for 268435456 bytes of essential memory "
                "for \"the tables\"; video memory:\n"
                    + describeMemory(report));
            EXPECT_EQ(describeMemory(report),
                "  heap  0  device    8192.0 MiB   reserved  7168.0   live  6144.0   96 blocks\n");

            EXPECT_EQ(nameOf(MemoryUse::Frame), "frame");
            EXPECT_EQ(nameOf(MemoryUse::Essential), "essential");
            EXPECT_EQ(nameOf(MemoryUse::Structure), "structure");
            EXPECT_EQ(nameOf(MemoryUse::Texture), "texture");
        }
    }
}
