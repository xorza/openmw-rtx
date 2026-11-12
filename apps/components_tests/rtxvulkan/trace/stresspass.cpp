#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/platform/gpu.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/trace/stresspass.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxStressPassTest : Testing::DeviceTest
        {
            /// One frame of `hold`: recorded, waited out and read, as the zone measured it, in
            /// milliseconds. The loop's own reading lands in `counts`.
            double frameOf(StressPass& hold, GpuTimer& timer, Buffer& counts, const std::uint64_t frame)
            {
                timer.beginFrame(frame);
                getDevice().getPool().submitAndWait([&](VkCommandBuffer commands) {
                    hold.record(commands, counts, &timer);
                    counts.orderForHostRead(commands);
                });

                GpuZones zones;
                timer.resolve(zones);
                if (zones.spans().empty())
                    return 0.0;

                return zones.spans().front().mMs;
            }
        };

        /// The hold is the time asked on every frame, the first included, whatever the card's clock: the loop's
        /// own clock reads what was asked and at most one tick (ten microseconds, NVIDIA's real-time clock step)
        /// more, and the zone is no shorter than the loop.
        ///
        /// A cold card idles at a few hundred megahertz and steps its clock over the first frames, which is where
        /// a hold set by a count missed by a third; a clock switch stalls the card for a millisecond or so, which
        /// the zone shows and the loop's clock runs through, so the zone is only asked to be no shorter.
        ///
        /// The extension does not promise the two counters run at one rate, so from the shortest frame of the 4 ms
        /// hold to that of the 24 ms one the zone must grow by what the loop does, within 15%. A difference, since
        /// the launch and drain the zone holds past the loop cancel. Another process's slice of the card does
        /// not cancel (up to 1.7 ms seen), so the holds are 20 ms apart, where 1.7 ms moves the rate by 8.5%.
        TEST_F(RtxStressPassTest, theHoldIsTheTimeAskedOnEveryFrameWhateverTheCardsClock)
        {
            Device& device = getDevice();
            const PhysicalDevice& card = device.getPhysicalDevice();
            GpuTimer timer(device, true);

            StressPass four(device, 4.0);
            StressPass twentyFour(device, 24.0);
            // A fact of one vendor's clock, which no capability states.
            if (Platform::Gpu::vendorOf(card.getProperties().mProperties2.properties.vendorID)
                == Platform::Gpu::Vendor::Nvidia)
            {
                EXPECT_EQ(card.timestampMs(1), 1.0e-6) << "NVIDIA's clock counts nanoseconds";
            }

            // Where the loop leaves its reading, as the ring's frame slot holds it.
            Buffer counts = Buffer::readBack(
                device, sizeof(Shaders::FrameCounts), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "frame counts");

            std::array<double, 2> shortestZoneMs{};
            std::array<double, 2> heldThenMs{};
            for (StressPass* const hold : { &four, &twentyFour })
            {
                const std::size_t which = hold == &four ? 0 : 1;
                shortestZoneMs[which] = std::numeric_limits<double>::infinity();
                // The fewest ticks no shorter than asked, and the time asked exactly: a period of
                // whole nanoseconds, NVIDIA's 1 and RDNA's 10, divides a whole millisecond.
                const double asked = hold == &four ? 4.0 : 24.0;
                EXPECT_EQ(card.timestampMs(hold->getTicks()), asked);
                EXPECT_LT(card.timestampMs(hold->getTicks() - 1), asked);
                const double tenMicroseconds = 0.01 / card.timestampMs(1);

                for (std::uint64_t frame = 0; frame < 8; ++frame)
                {
                    const double zoneMs = frameOf(*hold, timer, counts, frame);
                    const std::uint32_t held = static_cast<const Shaders::FrameCounts*>(counts.map())->mHeldTicks;
                    EXPECT_GE(held, hold->getTicks()) << "frame " << frame;
                    EXPECT_LE(held, hold->getTicks() + tenMicroseconds) << "frame " << frame;
                    EXPECT_GE(zoneMs, card.timestampMs(held)) << "frame " << frame;
                    if (zoneMs < shortestZoneMs[which])
                    {
                        shortestZoneMs[which] = zoneMs;
                        heldThenMs[which] = card.timestampMs(held);
                    }
                }
            }

            const double rate = (shortestZoneMs[1] - shortestZoneMs[0]) / (heldThenMs[1] - heldThenMs[0]);
            EXPECT_NEAR(rate, 1.0, 0.15) << "the loop's clock and the queue's timestamps run at two rates";
        }
    }
}
