#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxImageArenaDeviceTest : Testing::DeviceTest
        {
        };

        constexpr ImageDescription sSquare{ .mWidth = 8,
            .mHeight = 8,
            .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };

        /// **Three images, two of which are never in use at once**: the first and the second share
        /// memory and the third, in use beside both, stands past them, so the arena is two images
        /// long — each image's size, the third's on its alignment past the first. Counted as the
        /// frame's targets, at what `bytesFor` says. The first and the third begin in nothing; the
        /// second begins in the first's memory, and so discards from its writes.
        ///
        /// **And what the second writes is what it reads**, with the first written over the same
        /// memory before it and the third beside both untouched: cleared under the layers' checks of
        /// the barriers between them, which `Use::sAliased` records.
        TEST_F(RtxImageArenaDeviceTest, imagesNeverInUseAtOnceShareMemoryAndHandOverThroughABarrier)
        {
            const Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            const std::uint32_t heap = memory.getVideoHeap();

            const std::array<ArenaImage, 3> declared{ {
                { sSquare, ArenaSpan{ .mFirst = 0, .mLast = 0 }, "first" },
                { sSquare, ArenaSpan{ .mFirst = 1, .mLast = 1 }, "second" },
                { sSquare, ArenaSpan{ .mFirst = 0, .mLast = 1 }, "beside" },
            } };

            const VkMemoryRequirements one = Image::requirementsFor(device, sSquare);
            const VkDeviceSize expected = alignUp(one.size, one.alignment) + one.size;
            EXPECT_EQ(ImageArena::bytesFor(device, declared), expected);

            // What the tests before buried is freed at the next wait; freed first, so what moves
            // below is the arena's.
            device.waitIdle();
            device.collectIdle();
            const VkDeviceSize held = memory.getHeld(heap, MemoryUse::Frame);
            {
                const ImageArena arena(device, declared);
                EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame) - held, expected);
                ASSERT_EQ(arena.size(), 3u);
                EXPECT_EQ(&arena.firstUseOf(0), &Use::sUndefined);
                EXPECT_EQ(&arena.firstUseOf(1), &Use::sAliased);
                EXPECT_EQ(&arena.firstUseOf(2), &Use::sUndefined);

                const ArenaRun run(arena, 1, 2);
                EXPECT_EQ(&run[0], &arena.get(1));
                EXPECT_EQ(&run.firstUseOf(0), &Use::sAliased);

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    arena.get(0).clear(commands, arena.firstUseOf(0),
                        VkClearColorValue{ .float32 = { 1.0f, 0.0f, 0.0f, 1.0f } }, Use::sComputeWrite);
                    arena.get(2).clear(commands, arena.firstUseOf(2),
                        VkClearColorValue{ .float32 = { 0.0f, 0.0f, 1.0f, 1.0f } }, Use::sComputeWrite);
                    arena.get(1).clear(commands, arena.firstUseOf(1),
                        VkClearColorValue{ .float32 = { 0.0f, 1.0f, 0.0f, 1.0f } }, Use::sComputeWrite);
                });

                std::vector<std::uint8_t> second;
                arena.get(1).read(VK_IMAGE_LAYOUT_GENERAL, second);
                std::vector<std::uint8_t> beside;
                arena.get(2).read(VK_IMAGE_LAYOUT_GENERAL, beside);
                ASSERT_EQ(second.size(), std::size_t{ 8 * 8 * 4 });
                ASSERT_EQ(beside.size(), second.size());
                for (std::size_t at = 0; at < second.size(); at += 4)
                {
                    EXPECT_EQ(second[at + 1], 255) << "the second image read what it did not write, at " << at;
                    EXPECT_EQ(beside[at + 2], 255) << "the image beside them was written over, at " << at;
                }
            }

            device.waitIdle();
            device.collectIdle();
            EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame), held) << "the arena's memory outlived it";
        }
    }
}
