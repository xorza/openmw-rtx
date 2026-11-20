#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/heldsubmit.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

namespace Rtx
{
    namespace
    {
        using RtxGraveyardTest = Testing::DeviceTest;

        /// **A burial off the frame's thread is held until the submit it is stamped for has run**: a staging buffer
        /// let go of on another thread while a submit is held on the queue is two more things held (its handle and its
        /// memory), stamped for the submit after that one: neither a collect then nor the held submit's end ends them,
        /// and the wait on the submit after it does.
        TEST_F(RtxGraveyardTest, aBurialOffTheFramesThreadIsHeldUntilItsSubmitHasRun)
        {
            const Device& device = getDevice();
            Graveyard& graveyard = device.getGraveyard();
            // Emptied first, of what the shard's tests before this one buried, which a wait here would end besides.
            device.waitIdle();
            device.collectIdle();
            const std::size_t before = graveyard.getHeldCount();
            ASSERT_EQ(before, 0u);

            Testing::HeldSubmit held(device);
            const LentCommands commands = getPool().lend(1);
            const std::uint64_t submitted = held.submit(getPool().begin(commands[0]));

            std::jthread([&] {
                Buffer buried = Buffer::staging(device, 64 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "buried");
            }).join();
            EXPECT_EQ(graveyard.getHeldCount(), before + 2) << "the handle and its memory";
            graveyard.collect();
            EXPECT_EQ(graveyard.getHeldCount(), before + 2) << "the submit it is stamped for has not run";

            held.release();
            device.waitFor(submitted, "test");
            EXPECT_EQ(graveyard.getHeldCount(), before + 2) << "stamped for the submit after the one held";

            Testing::HeldSubmit next(device);
            const LentCommands more = getPool().lend(1);
            const std::uint64_t after = next.submit(getPool().begin(more[0]));
            next.release();
            device.waitFor(after, "test");
            EXPECT_EQ(graveyard.getHeldCount(), before) << "ended once every submit it could ride has run";
        }
    }
}
