#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/instanceobstacle.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/validation.hpp>

namespace Rtx
{
    namespace
    {
        /// Object names are what make a capture readable, and a capture is most wanted on a run that
        /// is not carrying the layers — so the two are enabled independently. Needs its own instance:
        /// the shared harness always asks for validation.
        TEST(RtxInstanceTest, objectNamesDoNotNeedTheValidationLayers)
        {
            if (const std::string obstacle = Testing::findInstanceObstacle(); !obstacle.empty())
                GTEST_SKIP() << obstacle;

            // Its own instance rather than the harness's, because what is being asserted is what an
            // unvalidated one carries — and the harness's comes with a device this does not need.
            const Instance instance{ ValidationOptions{}, std::span<const char* const>{} };

            EXPECT_EQ(instance.getValidationLog(), nullptr);
            EXPECT_EQ(instance.hasDebugUtils(), sDebugNames);
        }

        /// **An error the loader raises is not a validation error**, and one the layers raise is: the
        /// loader's has the general type alone, and a machine with no device met it before the
        /// device's selection could say so. Both through the messenger's own callback, with the
        /// same severity, text and thread.
        TEST(RtxInstanceTest, onlyTheLayersErrorsAreValidationErrors)
        {
            ValidationLog log(false);
            const VkDebugUtilsMessengerCreateInfoEXT messenger = makeMessengerCreateInfo(log);
            const VkDebugUtilsMessengerCallbackDataEXT data{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
                .pNext = nullptr,
                .flags = 0,
                .pMessageIdName = "Loader Message",
                .messageIdNumber = 0,
                .pMessage = "Failed to detect any valid GPUs in the current config",
                .queueLabelCount = 0,
                .pQueueLabels = nullptr,
                .cmdBufLabelCount = 0,
                .pCmdBufLabels = nullptr,
                .objectCount = 0,
                .pObjects = nullptr,
            };

            messenger.pfnUserCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data, messenger.pUserData);
            std::vector<std::string> taken;
            log.takeErrorsOnThisThread(taken);
            EXPECT_TRUE(taken.empty());

            messenger.pfnUserCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, messenger.pUserData);
            log.takeErrorsOnThisThread(taken);
            EXPECT_EQ(taken,
                (std::vector<std::string>{ "Loader Message: Failed to detect any valid GPUs in the current config" }));
        }
    }
}
