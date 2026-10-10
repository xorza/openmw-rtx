#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/trace/fogvolume.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>

namespace Rtx
{
    namespace
    {
        using RtxFogVolumeTest = Testing::DeviceTest;

        /// **A trace fills the columns its pixels read and no more**: those its pixels cover, one
        /// past them that the far edge interpolates against, and one past that for the integrate
        /// pass's tent, held to the grid. Counted by hand at twelve pixels a column, in a grid
        /// grown to the doll's preview, 516 by 1032: 43 columns by 86 rows. A map tile of 256 covers
        /// 22 a side (21 and a third, rounded up) and fills 24, where the whole grid was 43 by 86. A
        /// picture of a whole number of columns, 240, covers 20 and fills 22; a pixel fills 3. The
        /// preview itself and one a column narrower, 504 by 1020 (42 by 85 and two more each way),
        /// fill the grid and no more.
        TEST_F(RtxFogVolumeTest, aTraceFillsTheColumnsItsPixelsReadHeldToTheGrid)
        {
            static_assert(Shaders::FOG_VOLUME_SCALE == 12u, "the columns below are counted at twelve pixels");

            const SetLayout layout = FogVolume::describeLayout(getDevice());
            const FogVolume volume(getDevice(), layout, 516, 1032, MemoryUse::Essential, TracePast::Dropped);
            ASSERT_EQ(volume.getColumns(), 43u);
            ASSERT_EQ(volume.getRows(), 86u);

            const auto traced = [&](std::uint32_t width, std::uint32_t height) {
                const VkExtent2D filled = volume.tracedFor(VkExtent2D{ width, height });
                return std::pair(filled.width, filled.height);
            };
            EXPECT_EQ(traced(256, 256), std::pair(24u, 24u)) << "a map tile";
            EXPECT_EQ(traced(240, 240), std::pair(22u, 22u)) << "a whole number of columns";
            EXPECT_EQ(traced(1, 1), std::pair(3u, 3u)) << "one pixel";
            EXPECT_EQ(traced(256, 1032), std::pair(24u, 86u)) << "each axis on its own";
            EXPECT_EQ(traced(504, 1020), std::pair(43u, 86u)) << "two past the cover, held to the grid";
            EXPECT_EQ(traced(516, 1032), std::pair(43u, 86u)) << "the picture the grid was grown to";
        }
    }
}
