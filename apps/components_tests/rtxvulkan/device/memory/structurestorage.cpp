#include <string_view>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/memorylimits.hpp>
#include <components/misc/result.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/structurestorage.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    namespace
    {
        /// What an acceleration structure's offset has to be a multiple of, and so the unit the
        /// storage hands out. Written here as well so the expectations below are arithmetic a reader
        /// can follow rather than a number taken from the code under test.
        constexpr VkDeviceSize sAlignment = 256;

        /// Four kilobytes: sixteen units, which is small enough to fill by hand and large enough to
        /// leave holes in.
        constexpr VkDeviceSize sBlock = 16 * sAlignment;

        struct RtxStructureStorageTest : Testing::DeviceTest
        {
        };

        /// Room is handed out in order, given back where it was, and taken up again by what fits; every block is the
        /// storage's size, and one that empties goes back while another stands.
        ///
        /// **Hand-computed throughout.** A structure of 1,024 bytes is four units and one of 2,048 is eight, so the
        /// three below fill a sixteen-unit block exactly and the fourth has nowhere to go but a block of its own.
        TEST_F(RtxStructureStorageTest, roomGivenBackIsWhereTheNextStructureThatFitsGoes)
        {
            const Device& device = getDevice();
            StructureStorage storage(
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                "test structures", sBlock);

            const StructureRoom first = storage.take(device, 1024).value();
            const StructureRoom second = storage.take(device, 2048).value();
            const StructureRoom third = storage.take(device, 1024).value();

            EXPECT_EQ(first.mBlock, 0u);
            EXPECT_EQ(second.mBlock, 0u);
            EXPECT_EQ(third.mBlock, 0u);
            EXPECT_EQ(storage.getOffset(first), 0u);
            EXPECT_EQ(storage.getOffset(second), 1024u);
            EXPECT_EQ(storage.getOffset(third), 3072u);
            EXPECT_EQ(storage.getBytes(), sBlock) << "three structures that fit one block asked for one block";
            EXPECT_EQ(storage.getLiveBytes(), sBlock) << "and stand in every unit of it";

            // The block is full to the last unit, so this one starts another — and the buffers
            // already handed out are untouched, which is the whole reason the list grows rather than
            // the buffer.
            const StructureRoom fourth = storage.take(device, 256).value();
            EXPECT_EQ(fourth.mBlock, 1u);
            EXPECT_EQ(storage.getOffset(fourth), 0u);
            EXPECT_EQ(storage.getBytes(), 2 * sBlock);
            EXPECT_NE(storage.getBuffer(first), storage.getBuffer(fourth));
            EXPECT_EQ(storage.getBuffer(first), storage.getBuffer(third)) << "one block is one buffer";

            // The eight-unit hole in the middle of the first block cools under the stamp it was given
            // back with: a structure of that size asked for before the queue passes the stamp goes
            // past the fourth, 256 bytes into the second block, because a frame in flight may still
            // trace what stood in the hole.
            storage.retire(second, device.getTimeline().getNext());
            const StructureRoom early = storage.take(device, 2048).value();
            EXPECT_EQ(early.mBlock, 1u);
            EXPECT_EQ(storage.getOffset(early), 256u) << "a room was handed out under the submit it was retired for";
            EXPECT_EQ(storage.getLiveBytes(), sBlock + 256 + 2048) << "a room cooling is still a room";

            // Once a submit carrying that stamp has run, the next of exactly that size takes the
            // hole rather than being appended past everything.
            getPool().submitAndWait([](VkCommandBuffer) {});
            const StructureRoom again = storage.take(device, 2048).value();
            EXPECT_EQ(again.mBlock, 0u);
            EXPECT_EQ(storage.getOffset(again), 1024u);
            EXPECT_EQ(storage.getBytes(), 2 * sBlock) << "reuse costs no new storage";
            EXPECT_EQ(storage.getLiveBytes(), sBlock + 256 + 2048) << "the hole given back and taken again";

            // **The totals follow a block given back.** The second block's two rooms cool and come
            // back, and the block empties: it goes to the device, the first still standing, and
            // the storage is the first block, every unit of it live.
            storage.retire(fourth, device.getTimeline().getNext());
            storage.retire(early, device.getTimeline().getNext());
            getPool().submitAndWait([](VkCommandBuffer) {});
            storage.reclaim(device.getTimeline().getKnownFinished());
            EXPECT_EQ(storage.getBytes(), sBlock);
            EXPECT_EQ(storage.getLiveBytes(), sBlock);
        }

        /// **The last block stays once it empties**, which the next structure takes without asking the device: one
        /// structure of a block's size, given back and cooled, empties the one block the storage has.
        TEST_F(RtxStructureStorageTest, theLastBlockStaysOnceItEmpties)
        {
            const Device& device = getDevice();
            StructureStorage storage(
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                "test structures", sBlock);
            const StructureRoom room = storage.take(device, sBlock).value();
            EXPECT_EQ(storage.getBytes(), sBlock);
            storage.retire(room, device.getTimeline().getNext());
            getPool().submitAndWait([](VkCommandBuffer) {});
            storage.reclaim(device.getTimeline().getKnownFinished());
            EXPECT_EQ(storage.getLiveBytes(), 0u);
            EXPECT_EQ(storage.getBytes(), sBlock);

            const StructureRoom again = storage.take(device, 1024).value();
            EXPECT_EQ(again.mBlock, room.mBlock) << "the block kept was not the one taken";
            EXPECT_EQ(storage.getBytes(), sBlock);
        }

        /// A structure larger than the storage's block gets a block that holds it, and the next block is the
        /// storage's size again.
        TEST_F(RtxStructureStorageTest, aStructureLargerThanTheBlockGetsOneThatHoldsIt)
        {
            const Device& device = getDevice();
            StructureStorage storage(
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                "test structures", sBlock);

            const StructureRoom big = storage.take(device, 5 * sBlock).value();
            EXPECT_EQ(big.mBlock, 0u);
            EXPECT_EQ(storage.getOffset(big), 0u);
            EXPECT_EQ(storage.getBytes(), 5 * sBlock);

            const StructureRoom after = storage.take(device, 1024).value();
            EXPECT_EQ(after.mBlock, 1u);
            EXPECT_EQ(storage.getBytes(), 6 * sBlock);
        }

        /// A structure the device has no room for is refused, and no smaller block is asked for in its place: a block
        /// is the storage's size or none.
        ///
        /// **No gap in any block of content's, and then room for one block.** With the ceiling at nought the
        /// structure has nowhere at all, and no block joins the list. Under room for a block of content's block size
        /// and a megabyte, a storage of such blocks makes one, apart from the pools, which its next structures share;
        /// a structure past what that block has left asks for a second, which the room does not hold.
        TEST_F(RtxStructureStorageTest, aBlockIsTheStoragesSizeOrNone)
        {
            const Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            StructureStorage storage(
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                "test structures", sContentBlockBytes);
            {
                const Testing::NoRoomForContent full(device);
                const Result<StructureRoom, MemoryRefusal> none = storage.take(device, 1024);
                ASSERT_FALSE(none.isOk());
                EXPECT_EQ(none.error(), MemoryRefusal::NoRoom);
                EXPECT_EQ(storage.getBytes(), 0u) << "a refused block joined the list";
            }

            const Testing::BudgetLimit oneBlock(memory,
                Testing::budgetAbove(memory, MemoryUse::Structure, sContentBlockBytes + (VkDeviceSize{ 1 } << 20)));
            const StructureRoom first = storage.take(device, 1024).value();
            const StructureRoom second = storage.take(device, 1024).value();
            EXPECT_EQ(first.mBlock, second.mBlock);
            EXPECT_EQ(storage.getOffset(second), 1024u);
            EXPECT_EQ(storage.getBytes(), sContentBlockBytes);

            const Result<StructureRoom, MemoryRefusal> past = storage.take(device, sContentBlockBytes);
            ASSERT_FALSE(past.isOk()) << "a second block was made past the room";
            EXPECT_EQ(storage.getBytes(), sContentBlockBytes);
        }
    }
}
