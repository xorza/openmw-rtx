#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/upscale/featurebytes.hpp>

namespace Rtx
{
    namespace
    {
        /// **A feature's bytes are what was measured at its extents, and the most any measured held where its own
        /// never were**, which is what room is made by before a feature that never stood is made: nought before the
        /// first; 300 for a mode measured at 300 and 500 for one never measured beside another at 500; a measurement
        /// again at the same extents replaces the first; and one at another output drops every one before it, whose
        /// extents no mode traces at now.
        TEST(RtxFeatureBytesTest, aFeaturesBytesAreItsOwnMeasuredOrTheMostAnyHeld)
        {
            constexpr VkExtent2D output{ 3840, 2160 };
            constexpr VkExtent2D quality{ 2560, 1440 };
            constexpr VkExtent2D performance{ 1920, 1080 };
            constexpr VkExtent2D native{ 3840, 2160 };

            FeatureBytes bytes;
            EXPECT_FALSE(bytes.holds(quality, output));
            EXPECT_EQ(bytes.at(quality, output), 0u);

            bytes.remember(performance, output, 300);
            bytes.remember(quality, output, 500);
            EXPECT_TRUE(bytes.holds(performance, output));
            EXPECT_EQ(bytes.at(performance, output), 300u);
            EXPECT_EQ(bytes.at(quality, output), 500u);
            EXPECT_FALSE(bytes.holds(native, output));
            EXPECT_EQ(bytes.at(native, output), 500u) << "the most known, for a feature never made";

            bytes.remember(quality, output, 450);
            EXPECT_EQ(bytes.at(quality, output), 450u);
            EXPECT_EQ(bytes.at(native, output), 450u);

            constexpr VkExtent2D wider{ 5120, 2160 };
            EXPECT_FALSE(bytes.holds(quality, wider)) << "the render extent alone names no measurement";
            EXPECT_EQ(bytes.at(quality, wider), 450u);
            bytes.remember(quality, wider, 700);
            EXPECT_FALSE(bytes.holds(quality, output));
            EXPECT_FALSE(bytes.holds(performance, output));
            EXPECT_EQ(bytes.at(performance, output), 700u);
        }
    }
}
