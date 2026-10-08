#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
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

            // **An upload larger than a block stays in the block only where what is left of it holds
            // the upload**, and otherwise starts a block of its own at nought. Held against the size
            // of the block the batch was lent, because the ring is the device's and keeps every block
            // it made: a test before this one in a shuffled order can leave a block of 36 MiB spare,
            // and the smallest spare that holds a hundred bytes is then larger than a block. The
            // upload after it is held to the same rule.
            const std::vector<std::byte> past(sStagingBlock + 1, std::byte{ 3 });
            const StagingRun alone = batch.stage(past);
            const StagingRun after = batch.stage(fifty);

            // The fifty end at 162, and the next run starts on the sixteen after it.
            const CommandPool& pool = getPool();
            const VkDeviceSize pastStart = 176;
            const bool pastStays = pastStart + past.size() <= pool.getStagingSize(first.mBuffer);
            EXPECT_EQ(alone.mBuffer == first.mBuffer, pastStays) << "an upload left a block that held it, or "
                                                                    "landed in one with no room for it";
            EXPECT_EQ(alone.mOffset, pastStays ? pastStart : 0u);

            const VkDeviceSize afterStart
                = (alone.mOffset + past.size() + sStagingAlignment - 1) / sStagingAlignment * sStagingAlignment;
            const bool afterStays = afterStart + fifty.size() <= pool.getStagingSize(alone.mBuffer);
            EXPECT_EQ(after.mBuffer == alone.mBuffer, afterStays);
            EXPECT_EQ(after.mOffset, afterStays ? afterStart : 0u) << "an upload landed over the one before it";

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

            // **And a one-off whose recording throws**, which went back to nobody: the buffer stayed
            // recording, and the pool never handed it out again. It goes back as the batch's does,
            // takes no staging, and is the next buffer a one-off records into.
            const std::size_t blocks = getPool().getStagingBlockCount();
            VkCommandBuffer thrown = VK_NULL_HANDLE;
            EXPECT_THROW(getPool().submitAndWait([&](VkCommandBuffer commands) {
                thrown = commands;
                vkCmdCopyBuffer(commands, source.getHandle(), target.getHandle(), 1, &whole);
                // On a condition the compiler cannot decide, or MSVC calls the flush after the
                // record unreachable, and its warning is an error.
                if (commands != VK_NULL_HANDLE)
                    throw Abandoned{};
            }),
                Abandoned);
            VkCommandBuffer next = VK_NULL_HANDLE;
            getPool().submitAndWait([&](VkCommandBuffer commands) { next = commands; });

            EXPECT_EQ(next, thrown) << "the thrown one-off's buffer was not given back";
            EXPECT_EQ(getPool().getStagingBlockCount(), blocks);
            EXPECT_EQ(*static_cast<const std::uint32_t*>(target.map()), 0u) << "a thrown one-off's copy ran";
        }

        /// **A lent buffer is taken again only once a submit made after its owner ended has run.**
        /// Given back at once, a ring's buffer went to the next caller while a frame the ring had
        /// submitted could still be running it; kept by each owner, the pool had no word on it.
        TEST_F(RtxBatchTest, aLentBufferIsTakenAgainOnlyOnceASubmitAfterItsEndHasRun)
        {
            CommandPool& pool = getPool();
            VkCommandBuffer ended = VK_NULL_HANDLE;
            {
                const LentCommands lent = pool.lend(1);
                ASSERT_EQ(lent.size(), 1u);
                ended = lent[0];
            }

            const LentCommands before = pool.lend(1);
            EXPECT_NE(before[0], ended) << "lent again before any submit after its end had run";

            // The one-off takes a buffer and gives it back after its wait, and the wait gives back
            // what retired under its value: the ended buffer, ahead of the one-off's own.
            pool.submitAndWait([](VkCommandBuffer) {});
            const LentCommands after = pool.lend(2);
            EXPECT_TRUE(after[0] == ended || after[1] == ended) << "not given back once a submit after its end ran";
        }

        /// **What the pool gives back on an idle queue is nothing a recording still holds**: the
        /// graveyard's own idle collect leans on no recording being open beside it.
        TEST_F(RtxBatchTest, anIdleCollectBesideAnOpenRecordingIsAContractBroken)
        {
            getDevice().waitIdle();
            const LentCommands lent = getPool().lend(1);
            Recording open = getPool().begin(lent[0]);
            Testing::expectAssertDies(
                [&] { getDevice().collectIdle(); }, "command buffers given back while a recording is open");
            std::move(open).end();
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
