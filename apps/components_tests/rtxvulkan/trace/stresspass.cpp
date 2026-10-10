#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
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

        /// The hold is the time asked on every frame, the first included, whatever the card's clock:
        /// the loop's own clock reads what was asked and no more than one tick past it, and the
        /// queue was held at least that long.
        ///
        /// **In the clock's own ticks, at the queue's timestamp period**, which the zone reads in
        /// as well: a zone no shorter than the loop is the two counters running at one rate, and on
        /// NVIDIA's, which counts nanoseconds, a tick is a millionth of a millisecond.
        ///
        /// **Off a cold card, deliberately.** The card idles at a few hundred megahertz and steps
        /// its clock over the first frames of load, which is where a hold set by a count missed by
        /// a third. A clock switch stalls the card for a millisecond or so, which the zone shows
        /// and the loop's clock runs through; so the zone is asked only to be no shorter than the
        /// loop. Ten microseconds past the asked time is the tick the loop leaves on: NVIDIA's
        /// real-time clock advances by the microsecond.
        ///
        /// **And the two counters at one rate, which the extension does not promise**: from the
        /// shortest frame of the four-millisecond hold to the shortest of the twenty-four-millisecond
        /// one, the zone grows by what the loop does, within 15%. A difference, because the launch
        /// and the drain the zone holds past the loop are the same on both and cancel. Another
        /// process's slice of the card does not cancel, and does not grow with the hold: on a
        /// desktop it stood on every frame of a hold, up to 1.7 ms of it, and beside the other GPU
        /// shard the two holds' shortest frames differed by up to 0.8 ms. So the holds are twenty
        /// milliseconds apart, where 1.7 ms moves the rate by 8.5%, under the 15% asked. A loop whose
        /// clock ran slower than the timestamp period says would hold the queue longer than asked,
        /// which the lower bounds above let through.
        TEST_F(RtxStressPassTest, theHoldIsTheTimeAskedOnEveryFrameWhateverTheCardsClock)
        {
            Device& device = getDevice();
            GpuTimer timer(device, true);

            StressPass four(device, 4.0);
            StressPass twentyFour(device, 24.0);
            if (device.getPhysicalDevice().getProperties().mProperties2.properties.vendorID == 0x10de)
            {
                EXPECT_EQ(four.getTickMs(), 1.0e-6) << "NVIDIA's clock counts nanoseconds";
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
                // The asked time in ticks, at the pass's own rate: 4 ms and 24 ms.
                EXPECT_NEAR(hold->getTicks() * hold->getTickMs(), hold == &four ? 4.0 : 24.0, hold->getTickMs());
                const double tenMicroseconds = 0.01 / hold->getTickMs();

                for (std::uint64_t frame = 0; frame < 8; ++frame)
                {
                    const double zoneMs = frameOf(*hold, timer, counts, frame);
                    const std::uint32_t held = static_cast<const Shaders::FrameCounts*>(counts.map())->mHeldTicks;
                    EXPECT_GE(held, hold->getTicks()) << "frame " << frame;
                    EXPECT_LE(held, hold->getTicks() + tenMicroseconds) << "frame " << frame;
                    EXPECT_GE(zoneMs, held * hold->getTickMs()) << "frame " << frame;
                    if (zoneMs < shortestZoneMs[which])
                    {
                        shortestZoneMs[which] = zoneMs;
                        heldThenMs[which] = held * hold->getTickMs();
                    }
                }
            }

            const double rate = (shortestZoneMs[1] - shortestZoneMs[0]) / (heldThenMs[1] - heldThenMs[0]);
            EXPECT_NEAR(rate, 1.0, 0.15) << "the loop's clock and the queue's timestamps run at two rates";
        }
    }
}
