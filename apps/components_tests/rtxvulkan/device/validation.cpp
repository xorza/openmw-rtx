#include <array>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/validation.hpp>

namespace Rtx
{
    namespace
    {
        VkDebugUtilsObjectNameInfoEXT named(const VkObjectType type, const char* name)
        {
            return VkDebugUtilsObjectNameInfoEXT{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .pNext = nullptr,
                .objectType = type,
                .objectHandle = 1,
                .pObjectName = name };
        }

        /// **A hazard is a runtime's own only where every image and buffer it names is the runtime's**: NGX's resource
        /// alone, or its image beside its buffer, after NGX's prefix was disowned; not before, not beside one of the
        /// renderer's images, not on a name another prefix starts, not on an unnamed one, and not where nothing is
        /// named, which says nothing of whose the hazard is.
        TEST(RtxValidationLogTest, aHazardOnARuntimesOwnResourcesAloneIsDisowned)
        {
            ValidationLog log(false);
            const auto ngxImage = named(VK_OBJECT_TYPE_IMAGE, "nv.ngx.dlssd.resource");
            const auto ngxBuffer = named(VK_OBJECT_TYPE_BUFFER, "nv.ngx.dlssd.scratch");
            const auto ours = named(VK_OBJECT_TYPE_IMAGE, "rr-output");
            const auto commands = named(VK_OBJECT_TYPE_COMMAND_BUFFER, "frame");

            EXPECT_FALSE(log.isDisowned(std::array{ ngxImage })) << "nothing disowned yet";

            log.disown("nv.ngx.");
            EXPECT_TRUE(log.isDisowned(std::array{ ngxImage }));
            EXPECT_TRUE(log.isDisowned(std::array{ ngxImage, ngxBuffer }));
            EXPECT_TRUE(log.isDisowned(std::array{ commands, ngxImage }));
            EXPECT_FALSE(log.isDisowned(std::array{ ngxImage, ours })) << "one of the renderer's images among them";
            EXPECT_FALSE(log.isDisowned(std::array{ named(VK_OBJECT_TYPE_IMAGE, "nv.other") }));
            EXPECT_FALSE(log.isDisowned(std::array{ named(VK_OBJECT_TYPE_IMAGE, nullptr) }));
            EXPECT_FALSE(log.isDisowned(std::array{ commands })) << "no image or buffer named";
            EXPECT_FALSE(log.isDisowned(std::span<const VkDebugUtilsObjectNameInfoEXT>()));
        }
    }
}
