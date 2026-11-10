#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/heldsubmit.hpp>
#include <apps/components_tests/rtxvulkan/support/device/hostwritten.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxBufferTest : Testing::DeviceTest
        {
        };

        /// A buffer the host can reach is mapped once and keeps the address for its life.
        ///
        /// **What a frame pays for asking twice.** `vkMapMemory` takes a lock inside the driver and
        /// hands back an address that never moves, so a buffer the host rewrites every frame — the
        /// hit count is one — was paying a pair of driver calls a frame for a pointer it held.
        TEST_F(RtxBufferTest, aHostVisibleBufferIsMappedOnceAndKeepsTheAddress)
        {
            const Device& device = *mHarness.mDevice;

            const Buffer buffer = Buffer::readBack(device, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test");

            void* const mapped = buffer.map();
            ASSERT_NE(mapped, nullptr);
            EXPECT_EQ(buffer.map(), mapped) << "a second map moved the buffer";

            // And a write lands there, with nothing said to make it visible: the memory is coherent
            // and the mapping is the same one.
            const std::array<std::uint32_t, 2> written{ 7, 9 };
            buffer.write(std::span<const std::uint32_t>(written));

            const auto* read = static_cast<const std::uint32_t*>(mapped);
            EXPECT_EQ(read[0], 7u);
            EXPECT_EQ(read[1], 9u);
        }

        /// **Every write `Uploads` makes lands where it says, whichever way the build's writes
        /// travel**: a span at an offset, bytes lent and filled in place, and runs out of a source
        /// at offsets of their own — and nothing anywhere else, which the words left at nought say.
        /// Read back on the queue, since the host never reads a host-written buffer. Under staged
        /// writes, a copy left unordered is the contract `Uploads` holds its caller to.
        TEST_F(RtxBufferTest, uploadsLandWhereTheySay)
        {
            const Device& device = *mHarness.mDevice;
            CommandPool& pool = getPool();

            constexpr std::size_t sWords = 12;
            const Buffer written = Buffer::hostWritten(device, sWords * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            pool.submitAndWait([&](VkCommandBuffer commands) { written.clear(commands); });

            constexpr std::array<std::uint32_t, 2> first{ 1, 2 };
            constexpr std::array<std::uint32_t, 4> source{ 10, 11, 12, 13 };
            Testing::flushWriting(pool, [&](Uploads& uploads) {
                uploads.write(written, 0, std::span<const std::uint32_t>(first));

                const std::span<std::byte> lent
                    = uploads.lend(written, 3 * sizeof(std::uint32_t), 2 * sizeof(std::uint32_t));
                ASSERT_EQ(lent.size(), 2 * sizeof(std::uint32_t));
                constexpr std::array<std::uint32_t, 2> filled{ 3, 4 };
                std::memcpy(lent.data(), filled.data(), lent.size());

                // Words 10 and 11 of the source to words 6 and 7, and word 13 to word 10.
                constexpr std::array<CopyRun, 2> runs{ CopyRun{ .mFrom = 0, .mTo = 24, .mSize = 8 },
                    CopyRun{ .mFrom = 12, .mTo = 40, .mSize = 4 } };
                uploads.writeRuns(written, std::as_bytes(std::span(source)), runs);
            });

            const Buffer read
                = Buffer::readBack(device, sWords * sizeof(std::uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            pool.submitAndWait([&](VkCommandBuffer commands) {
                written.copyTo(commands, read, sWords * sizeof(std::uint32_t));
                read.orderForHostRead(commands);
            });
            std::array<std::uint32_t, sWords> landed{};
            std::memcpy(landed.data(), read.map(), sizeof(landed));
            EXPECT_EQ(landed, (std::array<std::uint32_t, sWords>{ 1, 2, 0, 3, 4, 0, 10, 11, 0, 0, 13, 0 }));

            // A run reaching past its source, the source's last word and one beyond it, is refused by
            // either transport before a byte is read.
            Testing::expectAssertDies(
                [&] {
                    Batch past(pool);
                    Uploads uploads(past);
                    constexpr std::array<CopyRun, 1> beyond{ CopyRun{ .mFrom = 12, .mTo = 0, .mSize = 8 } };
                    uploads.writeRuns(written, std::as_bytes(std::span(source)), beyond);
                },
                "a run past the end of what it is written from");

            if constexpr (sHostWrites == HostWrites::Staged)
                Testing::expectAssertDies(
                    [&] {
                        Batch unordered(pool);
                        Uploads uploads(unordered);
                        uploads.write(written, 0, std::span<const std::uint32_t>(first));
                    },
                    "staged writes nobody ordered ahead of what reads them");
        }

        /// A buffer moved out of takes its mapping, its size, its address and its stamp with it, and
        /// the husk is a default-made buffer: by construction and by assignment alike.
        ///
        /// **Which is what a table growing does**: `GrowableBuffer::growTo` hands the displaced
        /// buffer to a graveyard, and the new one is moved into the member the old one was in.
        TEST_F(RtxBufferTest, aMovedBufferTakesItsMappingWithIt)
        {
            const Device& device = *mHarness.mDevice;

            Buffer first = Buffer::readBack(
                device, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, "test");
            void* const mapped = first.map();
            const VkDeviceAddress address = first.getDeviceAddress();
            ASSERT_NE(address, 0u);
            first.nameForNext();
            const std::uint64_t named = first.getNamedUntil();
            ASSERT_NE(named, 0u);

            Buffer second = std::move(first);
            EXPECT_EQ(second.map(), mapped) << "the mapping did not come across";
            EXPECT_EQ(second.getSize(), 64u);
            EXPECT_EQ(second.getKind(), BufferKind::ReadBack);
            EXPECT_EQ(second.getDeviceAddress(), address);
            EXPECT_EQ(second.getNamedUntil(), named);
            EXPECT_TRUE(first.isEmpty());
            EXPECT_EQ(first.getSize(), 0u);
            EXPECT_EQ(first.getKind(), BufferKind::DeviceLocal);
            EXPECT_EQ(first.getDeviceAddress(), 0u);
            EXPECT_EQ(first.getNamedUntil(), 0u);

            Buffer third;
            third = std::move(second);
            EXPECT_EQ(third.getDeviceAddress(), address);
            EXPECT_EQ(third.getNamedUntil(), named);
            EXPECT_TRUE(second.isEmpty());
            EXPECT_EQ(second.getSize(), 0u);
            EXPECT_EQ(second.getDeviceAddress(), 0u);
            EXPECT_EQ(second.getNamedUntil(), 0u);
        }

        /// A growable buffer is made in the memory its owner named, is never left holding nothing,
        /// keeps what fits, and past what it holds grows to the need or to twice what it held.
        ///
        /// Each step by hand: nought asked of nothing makes the one byte Vulkan allows; 16 past 1
        /// is 16 to `growTo`; 8 fits in 16; 20 to `outgrow` is max(20, 2 × 16) = 32; 32 fits; 33
        /// is max(33, 64) = 64; and 200 is past twice 64, so it is 200.
        TEST_F(RtxBufferTest, aGrowableBufferKeepsWhatFitsAndDoublesPastIt)
        {
            const Device& device = *mHarness.mDevice;
            GrowableBuffer grown(device, BufferKind::ReadBack, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
            EXPECT_TRUE(grown.get().isEmpty());

            struct Step
            {
                bool mDoubles;
                VkDeviceSize mAsked;
                bool mMade;
                VkDeviceSize mSize;
            };
            constexpr std::array<Step, 7> steps{ {
                { false, 0, true, 1 },
                { false, 16, true, 16 },
                { false, 8, false, 16 },
                { true, 20, true, 32 },
                { true, 32, false, 32 },
                { true, 33, true, 64 },
                { true, 200, true, 200 },
            } };
            for (const Step& step : steps)
            {
                const bool made = step.mDoubles ? grown.outgrow(step.mAsked) : grown.growTo(step.mAsked);
                EXPECT_EQ(made, step.mMade) << "asked " << step.mAsked;
                EXPECT_EQ(grown.get().getSize(), step.mSize) << "asked " << step.mAsked;
                EXPECT_EQ(grown.get().getKind(), BufferKind::ReadBack);
            }
        }

        /// **A table grown on the queue keeps what it held, and what is staged after lands in the
        /// new buffer**: four words written, the table grown to eight in one batch — the copy, and
        /// then two words staged at its old end — and once the batch has run, all six read back in
        /// place. The old buffer is the batch's to keep until then.
        TEST_F(RtxBufferTest, aTableGrownOnTheQueueKeepsWhatItHeld)
        {
            const Device& device = *mHarness.mDevice;
            GrowableBuffer table(device, BufferKind::ReadBack,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "kept");
            ASSERT_TRUE(table.growTo(4 * sizeof(std::uint32_t)));
            const std::array<std::uint32_t, 4> held{ 1, 2, 3, 4 };
            table.get().write(std::span<const std::uint32_t>(held));

            // The old buffer is the copy's source, so the batch holds it unburied until it ends.
            const std::size_t buried = device.getGraveyard().getHeldCount();
            Batch batch(getPool());
            table.outgrowKeeping(batch, 8 * sizeof(std::uint32_t));
            EXPECT_EQ(device.getGraveyard().getHeldCount(), buried) << "the old buffer buried before its copy rode";
            const std::array<std::uint32_t, 2> staged{ 5, 6 };
            stageInto(batch, table.get(), 4 * sizeof(std::uint32_t), std::as_bytes(std::span(staged)));
            batch.flush();
            table.get().waitIdle("the growth read back");

            const auto* read = static_cast<const std::uint32_t*>(table.get().map());
            for (std::uint32_t at = 0; at < 6; ++at)
                EXPECT_EQ(read[at], at + 1) << "word " << at;
        }

        /// A copy names both of its ends for the submit it rides, and a host write of either waits
        /// for that submit and no longer.
        ///
        /// **What `isIdle` was not told.** A hand-out by address or by descriptor named its buffer,
        /// and a copy — which takes handles — named nothing, so a table written on the queue and
        /// then from the host was two writers in an order nobody had fixed, and the assert that
        /// guards a host write had nothing to fire on. The hold is what makes the queue's side of
        /// this a state rather than a race: the copy is on the queue for as long as the test says.
        TEST_F(RtxBufferTest, aCopyNamesBothEndsAndAHostWriteWaitsForIt)
        {
            const Device& device = *mHarness.mDevice;
            CommandPool& pool = getPool();

            constexpr VkBufferUsageFlags copyable = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            const Buffer source = Buffer::staging(device, 64, copyable, "test");
            const Buffer target = Buffer::readBack(device, 64, copyable, "test");

            std::array<std::uint32_t, 16> counted{};
            for (std::size_t at = 0; at < counted.size(); ++at)
                counted[at] = static_cast<std::uint32_t>(at + 1);
            source.write(std::span<const std::uint32_t>(counted));

            EXPECT_TRUE(source.isIdle()) << "a buffer nothing has named";
            EXPECT_TRUE(target.isIdle());

            Testing::HeldSubmit hold(device);
            const LentCommands lent = pool.lend(1);
            Recording recording = pool.begin(lent[0]);
            const VkCommandBuffer commands = recording.get();
            source.copyTo(commands, target, 64);

            const std::uint64_t next = device.getTimeline().getNext();
            EXPECT_EQ(source.getNamedUntil(), next) << "the copy's source was not named";
            EXPECT_EQ(target.getNamedUntil(), next) << "the copy's destination was not named";
            EXPECT_TRUE(target.isIdle()) << "named for a submit nobody has made, which a host write lands ahead of";

            EXPECT_EQ(hold.submit(std::move(recording)), next);
            EXPECT_FALSE(source.isIdle()) << "the copy is on the queue";
            EXPECT_FALSE(target.isIdle()) << "the copy is on the queue";

            hold.release();
            target.waitIdle("test");
            EXPECT_TRUE(target.isIdle());
            EXPECT_TRUE(source.isIdle()) << "one submit carried both ends";

            const auto* landed = static_cast<const std::uint32_t*>(target.map());
            for (std::size_t at = 0; at < counted.size(); ++at)
                EXPECT_EQ(landed[at], counted[at]) << at;

            // And idle is a state a wait need not leave for: nothing names the buffer now.
            target.waitIdle("test");
            EXPECT_TRUE(target.isIdle());

            // **A host-written buffer is written through `Uploads` alone**, which knows how the
            // build's writes travel: in place, a staged build's would land in memory the host cannot
            // reach, and a mapped one's would skip the idle check the uploads make.
            const Buffer written = Buffer::hostWritten(device, 64, copyable, "test");
            Testing::expectAssertDies([&] { static_cast<void>(written.writable<std::uint32_t>(0, 1)); },
                "a host-written buffer written past `Uploads`");
        }

        /// **A fill, an inline update and a barrier each name their buffer**, as a copy does: each
        /// puts the buffer on the queue with the submit it rides, so a host write meanwhile is the
        /// hazard `isIdle` guards, and until each named it nothing did.
        TEST_F(RtxBufferTest, aFillAnUpdateAndABarrierNameTheBufferAsACopyDoes)
        {
            const Device& device = *mHarness.mDevice;
            CommandPool& pool = getPool();

            constexpr VkBufferUsageFlags writable = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            const Buffer filled = Buffer::staging(device, 64, writable, "test");
            const Buffer updated = Buffer::staging(device, 64, writable, "test");
            const Buffer ordered = Buffer::staging(device, 64, writable, "test");

            Testing::HeldSubmit hold(device);
            const LentCommands lent = pool.lend(1);
            Recording recording = pool.begin(lent[0]);
            const VkCommandBuffer commands = recording.get();
            filled.clear(commands);
            constexpr std::array<std::byte, 4> word{};
            updated.updateInline(commands, Use::sBufferComputeRead, word);
            ordered.transition(commands, Use::sBufferComputeWrite, Use::sBufferComputeRead);

            const std::uint64_t next = device.getTimeline().getNext();
            EXPECT_EQ(filled.getNamedUntil(), next) << "the fill did not name its buffer";
            EXPECT_EQ(updated.getNamedUntil(), next) << "the inline update did not name its buffer";
            EXPECT_EQ(ordered.getNamedUntil(), next) << "the barrier did not name its buffer";

            EXPECT_EQ(hold.submit(std::move(recording)), next);
            EXPECT_FALSE(filled.isIdle()) << "the fill is on the queue";

            hold.release();
            filled.waitIdle("test");
            EXPECT_TRUE(filled.isIdle());
            EXPECT_TRUE(updated.isIdle());
            EXPECT_TRUE(ordered.isIdle());
        }
    }
}
