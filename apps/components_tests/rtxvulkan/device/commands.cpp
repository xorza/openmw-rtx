#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxBatchTest : Testing::DeviceTest
        {
        };

        /// One block takes upload after upload, and a new one is taken only where the last cannot
        /// hold what is asked of it.
        ///
        /// **What a buffer apiece cost.** A cell that arrives with two hundred textures uploads four
        /// hundred times — the levels and the shading map of each — and each of those was a
        /// `vkCreateBuffer`, a `vkAllocateMemory` and a `vkMapMemory` on the frame the cell landed.
        /// The bytes are alive just as long either way: the batch has held every one of them until
        /// the submit since it was written.
        ///
        /// **Appended and never rewound**, because nothing has run when a batch is being recorded:
        /// a block reused inside one would have the copy of an earlier upload reading what a later
        /// one wrote over it.
        TEST_F(RtxBatchTest, oneBlockTakesUploadAfterUploadAndAnotherOnlyWhereOneWillNotFit)
        {
            Batch batch(getPool());

            const std::vector<std::byte> hundred(100, std::byte{ 1 });
            const std::vector<std::byte> fifty(50, std::byte{ 2 });

            const StagingRun first = batch.stage(hundred);
            const StagingRun second = batch.stage(fifty);

            EXPECT_EQ(first.mOffset, 0u);
            EXPECT_EQ(second.mBuffer, first.mBuffer) << "a second upload took a buffer of its own";

            // A hundred rounded up to the sixteen a copy offset has to start on.
            EXPECT_EQ(second.mOffset, 112u);

            // **An upload larger than a block takes a block that holds it**: one of its own exactly
            // its size, or one the pool already had and larger, which a test before this one in a
            // shuffled order can have left free. Either way the upload after it lands clear of it.
            const std::vector<std::byte> past(sStagingBlock + 1, std::byte{ 3 });
            const StagingRun alone = batch.stage(past);
            const StagingRun after = batch.stage(fifty);

            EXPECT_NE(alone.mBuffer, first.mBuffer) << "an upload landed in a block with no room for it";
            EXPECT_EQ(alone.mOffset, 0u);
            EXPECT_TRUE(after.mBuffer != alone.mBuffer || after.mOffset >= alone.mOffset + past.size())
                << "an upload landed over the one before it";

            batch.flush();
        }

        /// A batch that leaves during unwinding submits nothing.
        ///
        /// **What a constructor that fails half way leaves behind.** `Texture` records the upload of
        /// its primary image and then makes a second one; where that allocation throws, the image is
        /// destroyed by the unwinding and a destructor that submitted would carry a copy naming a
        /// handle that has gone. The recording goes back to the pool instead.
        TEST_F(RtxBatchTest, aBatchAbandonedByAnExceptionSubmitsNothing)
        {
            const std::uint32_t value = 0x5eaf00d;
            const Buffer source = Buffer::staging(getDevice(), sizeof(value), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test");
            source.write(std::span(&value, 1));

            const Buffer target = Buffer::readBack(getDevice(), sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            *static_cast<std::uint32_t*>(target.map()) = 0;

            struct Abandoned
            {
            };

            // Outside the macro, whose arguments a comma between braces would split.
            const VkBufferCopy whole{ .srcOffset = 0, .dstOffset = 0, .size = sizeof(std::uint32_t) };

            EXPECT_THROW(
                {
                    Batch batch(getPool());

                    vkCmdCopyBuffer(batch.getCommands(), source.getHandle(), target.getHandle(), 1, &whole);

                    throw Abandoned{};
                },
                Abandoned);

            // The pool is asked for a submit of its own, so anything the batch had left behind would
            // have run by the time this returns.
            getPool().submitAndWait([](VkCommandBuffer) {});

            EXPECT_EQ(*static_cast<const std::uint32_t*>(target.map()), 0u)
                << "an abandoned batch's copy reached the device";

            // **And one abandoned on purpose**, which is what a placement that came to nothing does:
            // what it began goes back, and nothing reaches the next submit either.
            {
                Batch batch(getPool());
                vkCmdCopyBuffer(batch.getCommands(), source.getHandle(), target.getHandle(), 1, &whole);
                batch.abandon();
            }
            getPool().submitAndWait([](VkCommandBuffer) {});

            EXPECT_EQ(*static_cast<const std::uint32_t*>(target.map()), 0u) << "a batch given up on still ran";
        }

        /// A staged write names its destination for the submit the batch rides.
        ///
        /// **The other way a table is written on the queue**, and the one an arrival's rows take
        /// into the first copy of the skin tables: a placement then writes that copy from the host,
        /// and unnamed, the staged copy and the host write were two writers nobody had ordered.
        TEST_F(RtxBatchTest, aStagedWriteNamesItsDestination)
        {
            const Buffer target = Buffer::readBack(getDevice(), sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            *static_cast<std::uint32_t*>(target.map()) = 0;

            const std::uint32_t staged = 0xf00d;
            Batch batch(getPool());
            stageInto(batch, target, 0, std::as_bytes(std::span(&staged, 1)));

            EXPECT_EQ(target.getNamedUntil(), getDevice().getTimeline().getNext())
                << "the staged copy's destination was not named";

            batch.flush();
            EXPECT_TRUE(target.isIdle()) << "the batch was waited for";
            EXPECT_EQ(*static_cast<const std::uint32_t*>(target.map()), staged);
        }

        /// A block a batch gave back is the next batch's, once the submit that read it has run —
        /// and not before: a batch handed over and not yet carried is still going to be read.
        ///
        /// **What a block per batch cost.** An arrival made its staging and the graveyard destroyed
        /// it a frame later, a `vkCreateBuffer` and a bind per cell crossing for bytes that were
        /// alive as long either way. The ring settles at the busiest stretch — here, two batches
        /// in flight at once — and a batch after that allocates nothing.
        TEST_F(RtxBatchTest, aBlockGivenBackIsTakenAgainOnceTheSubmitThatReadItHasRun)
        {
            CommandPool& pool = getPool();
            const Buffer target = Buffer::readBack(getDevice(), 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            const std::vector<std::byte> some(64, std::byte{ 1 });
            const auto blocks = [&] { return pool.getStagingBlockCount(); };

            // A copy out of a block, so the batch has something to carry: a block is read by the
            // submit the batch rides, and a batch that recorded nothing rides none.
            const auto copyOut = [&](Batch& batch) { stageInto(batch, target, 0, some); };

            // The pool is the device's and every test before this one left blocks in it, so every
            // free one is taken first, by batches handed over and not carried, until one is made:
            // from here on every block is still to be read.
            const std::size_t had = blocks();
            while (blocks() == had)
            {
                Batch taking(pool);
                copyOut(taking);
                taking.defer();
            }
            const std::size_t full = blocks();

            {
                Batch beside(pool);
                copyOut(beside);
                beside.defer();
            }
            EXPECT_EQ(blocks(), full + 1) << "a block still to be read was taken again";

            // Carried and waited for, so every block is free again, and a batch after the busiest
            // stretch makes none however many times it comes.
            pool.finishDeferred();
            for (int round = 0; round < 3; ++round)
            {
                Batch again(pool);
                copyOut(again);
                again.flush();
            }
            EXPECT_EQ(blocks(), full + 1) << "a batch after the busiest stretch allocated";
        }
    }
}
