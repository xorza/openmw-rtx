#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include <components/rtx/common/extensionfilter.hpp>

namespace Rtx
{
    namespace
    {
        /// A name NGX asks for is not one of the renderer's optional extensions. The filter must keep
        /// it from the device's own list, which is the regression this test was written for; the
        /// old filter asked the optional list and removed every NGX extension.
        TEST(RtxExtensionFilterTest, anExtensionTheDeviceOffersSurvivesEvenWhenItIsNotAnOptionalOption)
        {
            const std::vector<std::string> requested{ "VK_NVX_binary_import", "VK_KHR_swapchain", "VK_EXT_missing" };
            const std::vector<std::string_view> offered{ "VK_KHR_swapchain", "VK_NVX_binary_import" };

            EXPECT_EQ(filterOfferedExtensions(requested, offered),
                (std::vector<std::string>{ "VK_NVX_binary_import", "VK_KHR_swapchain" }));
        }

        TEST(RtxExtensionFilterTest, nothingOfferedLeavesNothing)
        {
            const std::vector<std::string> requested{ "VK_KHR_swapchain" };
            EXPECT_TRUE(filterOfferedExtensions(requested, {}).empty());
        }
    }
}
