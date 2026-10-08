#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/scene/toplevelpackpass.hpp>
#include <components/rtxvulkan/shaders/shared/toplevelpack.h>

namespace Rtx
{
    namespace
    {
        struct RtxTopLevelPackPassTest : Testing::DeviceTest
        {
        };

        /// **The rows that place an instance, packed in slot order, and no others.** 700 rows by
        /// slot, of which slots 0, 255, 256, 300, 511, 512 and 699 place one: the first and the last
        /// row of a block, the first of the next and one inside it, and the last row of the table,
        /// whose block ends short of whole. Every row carries its slot as its custom index and a
        /// transform that is not nought, and a placed one a reference naming its slot — slot 300's
        /// in its high word alone, so a test of the low word would lose it. The blocks of 256 place
        /// 2, 3 and 2, so the starts are 0, 2 and 5, and the packing is the seven rows in slot order.
        /// What follows them is left as it was.
        TEST_F(RtxTopLevelPackPassTest, theRowsThatPlaceArePackedInSlotOrderAndNoOthers)
        {
            const Device& device = getDevice();
            constexpr std::uint32_t count = 700;
            constexpr std::array<std::uint32_t, 7> placed{ 0, 255, 256, 300, 511, 512, 699 };
            constexpr std::array<std::uint32_t, 3> starts{ 0, 2, 5 };
            static_assert(Shaders::TOP_LEVEL_PACK_WORKGROUP == 256, "the blocks above are counted at 256 rows");

            const auto referenceOf = [](std::uint32_t slot) -> std::uint64_t {
                return slot == 300 ? std::uint64_t{ 1 } << 40 : std::uint64_t{ slot } + 1;
            };

            std::vector<VkAccelerationStructureInstanceKHR> rows(count);
            for (std::uint32_t slot = 0; slot < count; ++slot)
            {
                rows[slot].transform.matrix[0][3] = static_cast<float>(slot);
                rows[slot].instanceCustomIndex = slot;
                rows[slot].mask = 0xFF;
            }
            for (const std::uint32_t slot : placed)
                rows[slot].accelerationStructureReference = referenceOf(slot);

            constexpr VkBufferUsageFlags addressed = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            constexpr VkDeviceSize rowBytes = sizeof(VkAccelerationStructureInstanceKHR);
            const Buffer rowBuffer = Buffer::hostWritten(device, count * rowBytes, addressed, "test rows");
            rowBuffer.writeAt(0, std::span<const VkAccelerationStructureInstanceKHR>(rows));
            const Buffer startBuffer = Buffer::hostWritten(device, sizeof(starts), addressed, "test starts");
            startBuffer.writeAt(0, std::span<const std::uint32_t>(starts));

            const Buffer packed = Buffer::readBack(device, count * rowBytes, addressed, "test packed");
            std::memset(packed.map(), 0xAB, count * rowBytes);

            const TopLevelPackPass pass(device);
            getPool().submitAndWait([&](VkCommandBuffer commands) {
                pass.record(commands,
                    TopLevelPackPass::Packing{ .mRows = rowBuffer.addressFor(),
                        .mPacked = packed.addressFor(),
                        .mStarts = startBuffer.addressFor(),
                        .mCount = count });
                handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
            });

            const auto* written = static_cast<const std::byte*>(packed.map());
            for (std::size_t at = 0; at < placed.size(); ++at)
            {
                VkAccelerationStructureInstanceKHR row;
                std::memcpy(&row, written + at * rowBytes, sizeof(row));
                EXPECT_EQ(std::memcmp(&row, &rows[placed[at]], sizeof(row)), 0)
                    << "packed row " << at << " is not slot " << placed[at] << "'s";
            }

            for (std::size_t byte = placed.size() * rowBytes; byte < count * rowBytes; ++byte)
                ASSERT_EQ(written[byte], std::byte{ 0xAB })
                    << "a row past the placed ones was written, at byte " << byte;
        }
    }
}
