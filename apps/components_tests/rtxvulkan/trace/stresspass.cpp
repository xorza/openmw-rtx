#include <cstdint>

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
                    hold.record(commands, timer, counts);
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
        TEST_F(RtxStressPassTest, theHoldIsTheTimeAskedOnEveryFrameWhateverTheCardsClock)
        {
            Device& device = getDevice();
            GpuTimer timer(device);

            StressPass four(device, 4.0);
            StressPass eight(device, 8.0);
            if (device.getPhysicalDevice().getProperties().mProperties2.properties.vendorID == 0x10de)
            {
                EXPECT_EQ(four.getTickMs(), 1.0e-6) << "NVIDIA's clock counts nanoseconds";
            }

            // Where the loop leaves its reading, as the ring's frame slot holds it.
            Buffer counts = Buffer::readBack(
                device, sizeof(Shaders::FrameCounts), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "frame counts");

            for (StressPass* const hold : { &four, &eight })
            {
                // The asked time in ticks, at the pass's own rate: 4 ms and 8 ms.
                EXPECT_NEAR(hold->getTicks() * hold->getTickMs(), hold == &four ? 4.0 : 8.0, hold->getTickMs());
                const double tenMicroseconds = 0.01 / hold->getTickMs();

                for (std::uint64_t frame = 0; frame < 8; ++frame)
                {
                    const double zoneMs = frameOf(*hold, timer, counts, frame);
                    const std::uint32_t held = static_cast<const Shaders::FrameCounts*>(counts.map())->mHeldTicks;
                    EXPECT_GE(held, hold->getTicks()) << "frame " << frame;
                    EXPECT_LE(held, hold->getTicks() + tenMicroseconds) << "frame " << frame;
                    EXPECT_GE(zoneMs, held * hold->getTickMs()) << "frame " << frame;
                }
            }
        }
    }
}
