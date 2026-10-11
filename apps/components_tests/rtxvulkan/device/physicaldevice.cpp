#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/cardmemory.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/requirements.hpp>

namespace Rtx
{
    namespace
    {
        /// A queue family that can do everything this renderer submits, and can time it.
        constexpr VkQueueFamilyProperties sWholeQueue{
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
            .queueCount = 16,
            .timestampValidBits = 64,
            .minImageTransferGranularity = {},
        };

        /// The extensions this build requires, as a device would list them back.
        std::vector<std::string> everyRequiredExtension()
        {
            std::vector<std::string> names;
            for (const RequiredExtension& required : getRequiredDeviceExtensions())
                names.emplace_back(required.mName);

            return names;
        }

        /// An RTX 2060 as the Vulkan Hardware Database reports it: report 46422, driver 590.48.01 on
        /// Linux, Vulkan 1.4.325.
        ///
        /// **A card nobody here owns, described from what it says of itself.** Three heaps — six
        /// gigabytes of video memory, twenty-five of system memory, and a 246 MiB window the host
        /// writes into. Its driver predates `VK_KHR_shader_fma`, so the card as reported is refused;
        /// a `Card` lists every required extension, as any driver from 595 does, and
        /// `aCardShortOfOneThingIsNamedForThatThing` takes that one out again.
        void describeTuring(DeviceProperties& properties)
        {
            properties.mProperties2.properties.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 325);
            properties.mVulkan12.driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            std::ranges::copy(std::string_view("590.48.01"), properties.mVulkan12.driverInfo);

            properties.mMemory = Testing::turingMemory();
        }

        /// This box's RTX 4090 Laptop, as `openmw-rtxtool info` reports it.
        ///
        /// **One heap of video memory, host-visible throughout**, which is what resizable BAR makes
        /// of a card — so the aperture the profile measures is the whole of it.
        void describeAda(DeviceProperties& properties)
        {
            properties.mProperties2.properties.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 341);
            properties.mVulkan12.driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;

            properties.mMemory = Testing::adaMemory();
        }

        /// An RX 6800 under RADV, as Mesa's drm-shim has it report itself for `NAVI21`.
        void describeRdna2(DeviceProperties& properties)
        {
            properties.mProperties2.properties.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 354);
            properties.mVulkan12.driverID = VK_DRIVER_ID_MESA_RADV;
            std::ranges::copy(std::string_view("Mesa 26.2.3"), properties.mVulkan12.driverInfo);
            properties.mMemory = Testing::rdna2Memory();
        }

        /// Everything a qualifying device answers, so a case below changes one thing and asks what
        /// the profile makes of it.
        struct Card
        {
            explicit Card(void (*describe)(DeviceProperties&))
                : mExtensions(everyRequiredExtension())
                , mQueues{ sWholeQueue }
            {
                describe(mProperties);
                requestRequiredFeatures(mFeatures);

                // What every card of the targets reports, and a case below takes out again.
                mProperties.mVulkan11.subgroupSupportedOperations
                    = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
                mProperties.mVulkan11.subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
                mProperties.mProperties2.properties.limits.maxPushConstantsSize = sPushConstantsFloor;
                mProperties.mVulkan12.shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;

                // Every required format offered whole, in optimal tiling, which is where an image
                // the trace samples lives.
                for (const RequiredFormat& required : getRequiredFormats())
                    mFormats.push_back(VkFormatProperties{
                        .linearTilingFeatures = 0, .optimalTilingFeatures = required.mFeatures, .bufferFeatures = 0 });

                // And every image a texture is made as, at the side every card of the targets takes.
                mProperties.mProperties2.properties.limits.maxImageDimension2D = 32768;
                for (std::size_t at = 0; at < getRequiredTextureImages().size(); ++at)
                    mImages.push_back(VkImageFormatProperties{ .maxExtent = { 32768, 32768, 1 },
                        .maxMipLevels = 16,
                        .maxArrayLayers = 2048,
                        .sampleCounts = VK_SAMPLE_COUNT_1_BIT,
                        .maxResourceSize = VkDeviceSize{ 1 } << 40 });
            }

            PhysicalDevice::Profile profile()
            {
                return PhysicalDevice::profileOf(
                    mProperties, mFeatures, mExtensions, mQueues, mFormats, mImages, mPresents);
            }

            DeviceProperties mProperties;
            DeviceFeatures mFeatures;
            std::vector<std::string> mExtensions;
            std::vector<VkQueueFamilyProperties> mQueues;
            std::vector<VkFormatProperties> mFormats;
            std::vector<std::optional<VkImageFormatProperties>> mImages;

            /// Empty for a renderer with no window, which every case here is but the one that asks.
            std::vector<VkBool32> mPresents;
        };

        /// Two cards this fork targets, and the profile differs in exactly what their hardware does.
        ///
        /// **The whole reason the type exists.** Neither card is asked, only described, and both
        /// answers matter: an RTX 2060 runs the same trace, and its 246 MiB aperture is what decides
        /// where the scene's tables live.
        TEST(RtxPhysicalDeviceTest, twoCardsDifferExactlyWhereTheirHardwareDoes)
        {
            Card turing(&describeTuring);
            Card ada(&describeAda);

            const PhysicalDevice::Profile onTuring = turing.profile();
            const PhysicalDevice::Profile onAda = ada.profile();

            EXPECT_EQ(onTuring.mObstacle, "") << "an RTX 2060 was refused";
            EXPECT_EQ(onAda.mObstacle, "") << "an RTX 4090 was refused";

            // 246 MiB against the whole of video memory, which is the difference resizable BAR
            // makes. `PhysicalDevice::Profile::mHostWrittenBytes` says what the figure then decides.
            EXPECT_EQ(onTuring.mHostWrittenBytes, 257949696ull);
            EXPECT_EQ(onAda.mHostWrittenBytes, 17171480576ull);
            EXPECT_NE(onTuring.mHostWrittenBytes, onAda.mHostWrittenBytes);

            // What both agree on, so a difference above is the hardware's and not the fixture's.
            EXPECT_EQ(onTuring.mQueueFamily, 0u);
            EXPECT_EQ(onAda.mQueueFamily, 0u);
            EXPECT_EQ(onTuring.mTimestampBits, 64u);
            EXPECT_EQ(onTuring.mTextureSide, 32768u);
        }

        /// **A queue is chosen for what this renderer submits and, where there is a window, for
        /// presenting to it**: graphics and compute, whether or not the family says it transfers,
        /// which Vulkan leaves it free not to; the first family that presents where the first that
        /// can do the rest does not; and a device with no family that presents, or no swapchain, is
        /// refused by name at selection rather than when the swapchain is made.
        TEST(RtxPhysicalDeviceTest, aQueueIsChosenForWhatIsSubmittedAndForTheWindow)
        {
            Card card(&describeTuring);
            card.mQueues.front().queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            EXPECT_EQ(card.profile().mObstacle, "") << "a family that does not say it transfers";

            card.mExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            card.mQueues.push_back(sWholeQueue);
            card.mPresents = { VK_FALSE, VK_TRUE };
            const PhysicalDevice::Profile windowed = card.profile();
            EXPECT_EQ(windowed.mObstacle, "");
            EXPECT_EQ(windowed.mQueueFamily, 1u) << "the family that presents";

            card.mPresents = { VK_FALSE, VK_FALSE };
            EXPECT_EQ(
                card.profile().mObstacle, "no queue family with graphics and compute that presents to the window");

            card.mPresents = { VK_FALSE, VK_TRUE };
            std::erase(card.mExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            EXPECT_EQ(card.profile().mObstacle, "missing extensions: VK_KHR_swapchain");
        }

        /// **A texture's side is the least any image it is made as takes, and the device's own limit
        /// above that**; and a device that takes no image of one of them is refused, named for what
        /// the image was for, at selection rather than by the first texture to arrive.
        TEST(RtxPhysicalDeviceTest, aTexturesSideIsTheLeastItsImagesTakeAndAnImageNoneTakesIsAnObstacle)
        {
            Card card(&describeTuring);
            ASSERT_GE(card.mImages.size(), 2u);
            card.mImages.back()->maxExtent.height = 16384;
            EXPECT_EQ(card.profile().mTextureSide, 16384u) << "the least of the images";
            card.mProperties.mProperties2.properties.limits.maxImageDimension2D = 8192;
            EXPECT_EQ(card.profile().mTextureSide, 8192u) << "the device's own limit";

            card.mImages.front().reset();
            EXPECT_EQ(card.profile().mObstacle, "no texture images for " + getRequiredTextureImages().front().mFor);
        }

        /// An optional extension is taken where the device lists it and left where it does not.
        ///
        /// **Neither answer refuses the device**, which is the whole difference between this list
        /// and the required one: a card without `VK_EXT_device_fault` traces the same frames and
        /// says less about a device loss.
        TEST(RtxPhysicalDeviceTest, anOptionalExtensionIsTakenOnlyWhereTheDeviceListsIt)
        {
            Card bare(&describeTuring);
            EXPECT_TRUE(bare.profile().mOptionalExtensions.empty()) << "an extension nothing offered was taken";

            // Every option's extensions, in the table's order.
            std::vector<const char*> every;
            for (const OptionalExtensions& option : getOptionalExtensions())
                every.insert(every.end(), option.mExtensions.begin(), option.mExtensions.end());

            Card full(&describeTuring);
            for (const char* const name : every)
                full.mExtensions.emplace_back(name);

            const PhysicalDevice::Profile profile = full.profile();
            ASSERT_EQ(profile.mOptionalExtensions.size(), every.size());
            for (std::size_t at = 0; at < profile.mOptionalExtensions.size(); ++at)
                EXPECT_STREQ(profile.mOptionalExtensions[at], every[at]) << "the order the table states was not kept";

            EXPECT_EQ(profile.mObstacle, "") << "an optional extension decided whether the card qualifies";
        }

        /// Each thing a device can lack is named, and nothing else is.
        ///
        /// **One case per obstacle, on a card that qualifies but for that one thing**, so a message
        /// naming the wrong lack fails here rather than sending a reader after it.
        TEST(RtxPhysicalDeviceTest, aCardShortOfOneThingIsNamedForThatThing)
        {
            {
                Card old(&describeTuring);
                old.mProperties.mProperties2.properties.apiVersion = VK_API_VERSION_1_3;
                EXPECT_EQ(old.profile().mObstacle, "reports Vulkan 1.3.0");

                // **And with nothing else asked of it**, as selection asks a device short of the
                // version nothing more: its version alone names the obstacle.
                DeviceFeatures none;
                EXPECT_EQ(PhysicalDevice::profileOf(old.mProperties, none, {}, {}, {}, {}, {}).mObstacle,
                    "reports Vulkan 1.3.0");
            }
            {
                Card short_(&describeTuring);
                short_.mExtensions.erase(short_.mExtensions.begin());
                EXPECT_EQ(short_.profile().mObstacle,
                    "missing extensions: " + std::string(getRequiredDeviceExtensions().front().mName));
            }
            {
                // Report 46422's driver: the card is short of a driver and not of hardware, and the
                // refusal names the release that has the extension beside the one that has not.
                Card dated(&describeTuring);
                std::erase(dated.mExtensions, VK_KHR_SHADER_FMA_EXTENSION_NAME);
                EXPECT_EQ(dated.profile().mObstacle,
                    "missing extensions: VK_KHR_shader_fma (NVIDIA driver 595 or later; this one is 590.48.01)");

                // Each driver is named with its own floor, and quoted as it describes itself: AMD's
                // as report 51246 does ("26.7.1 (LLPC)"), Mesa's as a release of it does ("Mesa
                // 26.2.3"), each a release short of the floor.
                const auto refusedUnder = [&](VkDriverId driver, std::string_view info) {
                    dated.mProperties.mVulkan12.driverID = driver;
                    std::ranges::fill(dated.mProperties.mVulkan12.driverInfo, '\0');
                    std::ranges::copy(info, dated.mProperties.mVulkan12.driverInfo);
                    return dated.profile().mObstacle;
                };
                EXPECT_EQ(refusedUnder(VK_DRIVER_ID_AMD_PROPRIETARY, "26.2.1 (LLPC)"),
                    "missing extensions: VK_KHR_shader_fma (AMD driver 26.3.1 or later; this one is 26.2.1 (LLPC))");
                EXPECT_EQ(refusedUnder(VK_DRIVER_ID_MESA_RADV, "Mesa 26.1.4"),
                    "missing extensions: VK_KHR_shader_fma (Mesa 26.2 or later; this one is Mesa 26.1.4)");
                EXPECT_EQ(refusedUnder(VK_DRIVER_ID_MESA_NVK, "Mesa 26.1.4"),
                    "missing extensions: VK_KHR_shader_fma (Mesa 26.2 or later; this one is Mesa 26.1.4)");

                // A driver the table says nothing of is refused for the extension alone.
                EXPECT_EQ(refusedUnder(VK_DRIVER_ID_MOLTENVK, "1.4.1"), "missing extensions: VK_KHR_shader_fma");
            }
            {
                // What a Radeon lists — its own memory types, AMD's device-coherent ones among them:
                // everything the trace needs, under another vendor's driver. The renderer traces on it.
                Card foreign(&describeRdna2);
                EXPECT_EQ(foreign.profile().mObstacle, "");
                foreign.mProperties.mVulkan12.driverID = VK_DRIVER_ID_AMD_PROPRIETARY;
                EXPECT_EQ(foreign.profile().mObstacle, "");
            }
            {
                Card short_(&describeTuring);
                const RequiredFeature& first = getRequiredDeviceFeatures().front();
                first.mField(short_.mFeatures) = VK_FALSE;
                EXPECT_EQ(short_.profile().mObstacle, "missing features: " + std::string(first.mName));
            }
            {
                // Offered for linear tiling only, which is not where an image the trace samples
                // lives, and so not offered.
                Card flat(&describeTuring);
                flat.mFormats.front()
                    = VkFormatProperties{ .linearTilingFeatures = getRequiredFormats().front().mFeatures,
                          .optimalTilingFeatures = 0,
                          .bufferFeatures = 0 };
                EXPECT_EQ(flat.profile().mObstacle,
                    "missing format features for " + std::string(getRequiredFormats().front().mFor));
            }
            {
                // Quads in the fragment stage alone, and in compute only the basic operations.
                Card fragmentQuads(&describeTuring);
                fragmentQuads.mProperties.mVulkan11.subgroupSupportedStages = VK_SHADER_STAGE_FRAGMENT_BIT;
                EXPECT_EQ(fragmentQuads.profile().mObstacle, "no quad subgroup operations in compute shaders");

                Card basic(&describeTuring);
                basic.mProperties.mVulkan11.subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
                EXPECT_EQ(basic.profile().mObstacle, "no quad subgroup operations in compute shaders");
            }
            {
                Card folding(&describeTuring);
                folding.mProperties.mVulkan12.shaderSignedZeroInfNanPreserveFloat32 = VK_FALSE;
                EXPECT_EQ(folding.profile().mObstacle,
                    "no preservation of signed zeros, infinities and NaNs in 32-bit floats");
            }
            {
                // Vulkan 1.0's 128 bytes, which a device reporting 1.4 must not.
                Card cramped(&describeTuring);
                cramped.mProperties.mProperties2.properties.limits.maxPushConstantsSize = 128;
                EXPECT_EQ(cramped.profile().mObstacle, "push constants of 128 bytes, under 256");
            }
            {
                // One under the largest frame's side, which RDNA 2 reports exactly.
                Card small(&describeTuring);
                small.mProperties.mProperties2.properties.limits.maxImageDimension2D = 16383;
                EXPECT_EQ(small.profile().mObstacle, "images of 16383 pixels a side at most, under 16384");
            }
            {
                Card split(&describeTuring);
                split.mQueues.front().queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
                EXPECT_EQ(split.profile().mObstacle, "no queue family with graphics and compute");
            }
            {
                // The whole of what makes such a card unusable: `Buffer::hostWritten` has nowhere to
                // go, and nothing in this renderer stages instead.
                Card walled(&describeTuring);
                walled.mProperties.mMemory.memoryTypeCount = 4;
                const PhysicalDevice::Profile profile = walled.profile();
                EXPECT_EQ(profile.mHostWrittenBytes, 0u);
                EXPECT_EQ(profile.mObstacle, "no memory type the host writes into and the device reads");
            }
        }

        /// A second aperture does not add to the first.
        ///
        /// **A buffer goes in one heap.** Two windows of 128 MiB hold no table a single 256 MiB one
        /// would not, so the profile reports the largest rather than the sum — and a sum would say a
        /// card had room it has nowhere.
        TEST(RtxPhysicalDeviceTest, twoAperturesReportTheLargerAndNotTheirSum)
        {
            Card split(&describeTuring);
            VkPhysicalDeviceMemoryProperties& memory = split.mProperties.mMemory;

            memory.memoryHeaps[2].size = 134217728ull;
            memory.memoryHeaps[memory.memoryHeapCount] = VkMemoryHeap{ 201326592ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
            memory.memoryTypes[memory.memoryTypeCount] = VkMemoryType{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                memory.memoryHeapCount };
            ++memory.memoryHeapCount;
            ++memory.memoryTypeCount;

            EXPECT_EQ(split.profile().mHostWrittenBytes, 201326592ull) << "two apertures were added together";
        }
    }
}
