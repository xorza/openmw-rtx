#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/instanceobstacle.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/loaderlayers.hpp>
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

            // The validation layer is installed beside the tests and not asked for here, so the
            // list holds what loaded and not what is installed.
            const std::span<const InsertedLayer> inserted = instance.getLayers().getInserted();
            EXPECT_EQ(instance.getLayers().isHeard(), instance.hasDebugUtils());
            EXPECT_TRUE(std::none_of(inserted.begin(), inserted.end(),
                [](const InsertedLayer& layer) { return layer.mName == "VK_LAYER_KHRONOS_validation"; }));
        }

        void say(LoaderLayers& layers, const char* idName, const char* message)
        {
            VkDebugUtilsMessengerCreateInfoEXT messenger = layers.makeMessengerCreateInfo();
            const VkDebugUtilsMessengerCallbackDataEXT data{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
                .pNext = nullptr,
                .flags = 0,
                .pMessageIdName = idName,
                .messageIdNumber = 0,
                .pMessage = message,
                .queueLabelCount = 0,
                .pQueueLabels = nullptr,
                .cmdBufLabelCount = 0,
                .pCmdBufLabels = nullptr,
                .objectCount = 0,
                .pObjects = nullptr,
            };
            EXPECT_EQ(messenger.pfnUserCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
                          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data, messenger.pUserData),
                VK_FALSE);
        }

        /// **The loader's insertions, in its order, each name apart from its library**, where a path
        /// has parentheses of its own. A layer's message is not the loader's, and a loader that said
        /// nothing is not one that inserted nothing.
        TEST(RtxInstanceTest, theLayersAreWhatTheLoaderSaysItInserted)
        {
            LoaderLayers layers;
            say(layers, "VUID-VkInstanceCreateInfo-pNext", "Insert instance layer \"VK_LAYER_FAKE\" (fake.dll)");
            EXPECT_FALSE(layers.isHeard());
            EXPECT_EQ(layers.summarize(), "not reported by the loader");
            EXPECT_EQ(layers.describe(), "\nlayers the loader inserted\n  (the loader reported nothing)\n");

            say(layers, "Loader Message", "Searching for driver manifest files");
            EXPECT_TRUE(layers.isHeard());
            EXPECT_EQ(layers.summarize(), "none");
            EXPECT_EQ(layers.describe(), "\nlayers the loader inserted\n  (none)\n");

            say(layers, "Loader Message",
                "Insert instance layer \"VK_LAYER_RTSS\" (C:\\Program Files (x86)\\RTSS\\RTSSVkLayer64.dll)");
            say(layers, "Loader Message", "Insert instance layer \"VK_LAYER_OBS_hook\" (graphics-hook64.dll)");
            ASSERT_EQ(layers.getInserted().size(), 2);
            EXPECT_EQ(layers.getInserted()[0].mName, "VK_LAYER_RTSS");
            EXPECT_EQ(layers.getInserted()[0].mLibrary, "C:\\Program Files (x86)\\RTSS\\RTSSVkLayer64.dll");
            EXPECT_EQ(layers.getInserted()[1].mName, "VK_LAYER_OBS_hook");
            EXPECT_EQ(layers.getInserted()[1].mLibrary, "graphics-hook64.dll");
            EXPECT_EQ(layers.summarize(), "VK_LAYER_RTSS, VK_LAYER_OBS_hook");

            // The names padded to the longer, 17 characters, and two spaces before each library.
            EXPECT_EQ(layers.describe(),
                "\nlayers the loader inserted\n"
                "  VK_LAYER_RTSS      C:\\Program Files (x86)\\RTSS\\RTSSVkLayer64.dll\n"
                "  VK_LAYER_OBS_hook  graphics-hook64.dll\n");

            // A spelling the loader changed, kept whole: 1.3.204's had no quotes.
            say(layers, "Loader Message", "Insert instance layer VK_LAYER_OLD (old.dll)");
            ASSERT_EQ(layers.getInserted().size(), 3);
            EXPECT_EQ(layers.getInserted()[2].mName, "VK_LAYER_OLD (old.dll)");
            EXPECT_EQ(layers.getInserted()[2].mLibrary, "");
        }

        /// **Cut at a name to what an annotation holds**, with how many the cut left out. Ten names
        /// of 30 characters: k of them take 30 + 32 (k - 1) = 32 k - 2, and the rest " and 3 more",
        /// 11, so seven take 233 and eight would take 265, past 255.
        TEST(RtxInstanceTest, theLayersSummaryFitsAnAnnotation)
        {
            LoaderLayers layers;
            std::vector<std::string> names;
            for (char letter = 'A'; letter < 'A' + 10; ++letter)
            {
                names.push_back("VK_LAYER_" + std::string(21, letter));
                say(layers, "Loader Message", ("Insert instance layer \"" + names.back() + "\" (x.dll)").c_str());
            }

            std::string expected = names[0];
            for (std::size_t index = 1; index < 7; ++index)
                expected += ", " + names[index];
            expected += " and 3 more";
            EXPECT_EQ(expected.size(), 233);
            EXPECT_EQ(layers.summarize(), expected);

            // One name past the limit alone leaves nothing to show but the count.
            LoaderLayers one;
            say(one, "Loader Message",
                ("Insert instance layer \"VK_LAYER_" + std::string(300, 'Z') + "\" (z.dll)").c_str());
            EXPECT_EQ(one.summarize(), "1 layer");
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
