#include <gtest/gtest.h>

#include <components/rtxvulkan/scene/sceneacceleration.hpp>

namespace Rtx
{
    namespace
    {
        /// **Where an instance stands is no shape, and every other field of its row is.** A row
        /// moved by a whole transform keeps the shape, so the top level updates; each of the five
        /// fields an update cannot follow changes it alone, and a row that came or went changes it.
        TEST(RtxSceneAccelerationTest, onlyWhereAnInstanceStandsLeavesTheTopLevelsShape)
        {
            const VkAccelerationStructureInstanceKHR was{
                .transform = { .matrix = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } },
                .instanceCustomIndex = 7,
                .mask = 0x01,
                .instanceShaderBindingTableRecordOffset = 2,
                .flags = 0,
                .accelerationStructureReference = 0x1000,
            };

            VkAccelerationStructureInstanceKHR moved = was;
            moved.transform.matrix[0][3] = 250.0f;
            moved.transform.matrix[1][1] = -1.0f;
            EXPECT_FALSE(SceneAcceleration::changesShape(was, moved));
            EXPECT_FALSE(SceneAcceleration::changesShape(was, was));

            VkAccelerationStructureInstanceKHR other = was;
            other.accelerationStructureReference = 0x2000;
            EXPECT_TRUE(SceneAcceleration::changesShape(was, other)) << "another structure";
            other = was;
            other.instanceCustomIndex = 8;
            EXPECT_TRUE(SceneAcceleration::changesShape(was, other)) << "another index";
            other = was;
            other.mask = 0x02;
            EXPECT_TRUE(SceneAcceleration::changesShape(was, other)) << "another mask";
            other = was;
            other.instanceShaderBindingTableRecordOffset = 3;
            EXPECT_TRUE(SceneAcceleration::changesShape(was, other)) << "another kind";
            other = was;
            other.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            EXPECT_TRUE(SceneAcceleration::changesShape(was, other)) << "another facing";

            EXPECT_TRUE(SceneAcceleration::changesShape(was, VkAccelerationStructureInstanceKHR{}))
                << "a row that went";
            EXPECT_TRUE(SceneAcceleration::changesShape(VkAccelerationStructureInstanceKHR{}, was))
                << "a row that came";
        }
    }
}
