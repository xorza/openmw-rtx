#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/trace/fogvolume.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxFogVolumeTest : Testing::DeviceTest
        {
        };

        /// **The air's pair turns once a trace, and nothing else turns it.** A trace writes one half
        /// and reads the other as its history, so the next trace reads what this one wrote. The
        /// pair followed the ring's frame slot before, which a frame closed untraced also advances:
        /// the trace after it wrote over the last trace's air and read the air of the trace before.
        TEST_F(RtxFogVolumeTest, theAirTurnsOnceATraceAndOnlyThen)
        {
            const SetLayout layout = FogVolume::describeLayout(getDevice());
            FogVolume air(getDevice(), layout, 64, 64);

            const VkDescriptorSet first = air.getSet();
            EXPECT_EQ(air.getSet(), first) << "asked twice within one trace";

            air.turn();
            const VkDescriptorSet second = air.getSet();
            EXPECT_NE(second, first) << "a trace writing the half the last one wrote";

            air.turn();
            EXPECT_EQ(air.getSet(), first) << "a pair of more than two";
        }
    }
}
