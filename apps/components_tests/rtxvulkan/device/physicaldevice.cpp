#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/cardmemory.hpp>
#include <components/rtxvulkan/device/driverprofile.hpp>
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
            for (const char* const required : getRequiredDeviceExtensions())
                names.emplace_back(required);

            return names;
        }

        /// An RTX 2060 as the Vulkan Hardware Database reports it: report 46422, driver 590.48.01 on Linux, Vulkan
        /// 1.4.325.
        ///
        /// **A card nobody here owns, described from what it says of itself.** Three heaps: six gigabytes of video
        /// memory, twenty-five of system memory, and a 246 MiB window the host writes into. A `Card` lists every
        /// required extension, and `aCardShortOfOneThingIsNamedForThatThing` takes one out again.
        void describeTuring(DeviceProperties& properties)
        {
            properties.mProperties2.properties.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 325);
            properties.mVulkan12.driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            std::ranges::copy(std::string_view("590.48.01"), properties.mVulkan12.driverInfo);

            properties.mMemory = Testing::turingMemory();
        }

        /// A high-end NVIDIA laptop GPU, as `openmw-rtxtool info` reports it.
        ///
        /// **One heap of video memory, host-visible throughout**, which is what resizable BAR makes
        /// of a card.
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
                mProperties.mVulkan11.subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT
                    | VK_SUBGROUP_FEATURE_QUAD_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT
                    | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
                mProperties.mVulkan11.subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
                mProperties.mVulkan13.minSubgroupSize = 32;
                mProperties.mVulkan13.maxSubgroupSize = 32;
                mProperties.mVulkan13.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
                mProperties.mProperties2.properties.limits.maxPushConstantsSize = sPushConstantsFloor;
                mProperties.mVulkan12.shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;

                // The arrays bound after use, at the million NVIDIA's drivers state.
                mProperties.mVulkan12.maxDescriptorSetUpdateAfterBindSampledImages = 1u << 20;
                mProperties.mVulkan12.maxPerStageDescriptorUpdateAfterBindSampledImages = 1u << 20;
                mProperties.mVulkan12.maxDescriptorSetUpdateAfterBindSamplers = 1u << 20;
                mProperties.mVulkan12.maxPerStageDescriptorUpdateAfterBindSamplers = 1u << 20;

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

        /// Two cards this fork targets are both taken, whatever window the host maps of their video memory: an RTX
        /// 2060's 246 MiB beside it, and an Ada card's whole heap. Neither card is asked, only described.
        TEST(RtxPhysicalDeviceTest, twoCardsThisForkTargetsAreBothTaken)
        {
            Card turing(&describeTuring);
            Card ada(&describeAda);

            const PhysicalDevice::Profile onTuring = turing.profile();
            const PhysicalDevice::Profile onAda = ada.profile();

            EXPECT_EQ(onTuring.mObstacle, "") << "an RTX 2060 was refused for its window";
            EXPECT_EQ(onAda.mObstacle, "") << "an Ada card was refused";

            EXPECT_EQ(onTuring.mQueueFamily, 0u);
            EXPECT_EQ(onAda.mQueueFamily, 0u);
            EXPECT_EQ(onTuring.mTimestampBits, 64u);
            EXPECT_EQ(onTuring.mTextureSide, 32768u);
        }

        /// **A driver's profile is read off its vendor and its driver alone**: NGX runs on NVIDIA's id
        /// and on no other; each driver names its statistics its own way, RADV and AMD's own
        /// differently on one card, and a driver that names none has none.
        TEST(RtxPhysicalDeviceTest, aDriversProfileIsItsCapabilitiesAndItsStatisticsNames)
        {
            const DriverProfile nvidia = DriverProfile::of(0x10DE, VK_DRIVER_ID_NVIDIA_PROPRIETARY);
            EXPECT_TRUE(nvidia.mNgx);
            EXPECT_EQ(nvidia.mStatistics.mRegisters, "Register Count");
            EXPECT_EQ(nvidia.mStatistics.mScratch, "Local Memory Size");
            EXPECT_EQ(nvidia.mStatistics.mScratchBits, 0xFFFFFFFFu);

            const DriverProfile radv = DriverProfile::of(0x1002, VK_DRIVER_ID_MESA_RADV);
            EXPECT_FALSE(radv.mNgx);
            EXPECT_EQ(radv.mStatistics.mScratch, "Scratch size");
            EXPECT_EQ(radv.mStatistics.mScratchBits, ~std::uint64_t{ 0 });
            const DriverProfile amd = DriverProfile::of(0x1002, VK_DRIVER_ID_AMD_PROPRIETARY);
            EXPECT_EQ(amd.mStatistics.mScratch, "scratchMemUsageInBytes");

            const DriverProfile other = DriverProfile::of(0x5143, VK_DRIVER_ID_QUALCOMM_PROPRIETARY);
            EXPECT_FALSE(other.mNgx);
            EXPECT_TRUE(other.mStatistics.mRegisters.empty());
        }

        /// **A queue is chosen for what this renderer submits and, where there is a window, for presenting to it**:
        /// graphics and compute, whether or not the family says it transfers, which Vulkan leaves it free not to; the
        /// first family that presents where the first that can do the rest does not; and a device with no presenting
        /// family, or no swapchain, is refused by name at selection rather than when the swapchain is made.
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
        /// **Neither answer refuses the device**, which is the difference between this list and the required one: a
        /// card without `VK_EXT_device_fault` traces the same frames and says less about a device loss.
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
                    "missing extensions: " + std::string(getRequiredDeviceExtensions().front()));
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

                // **Every row of the table is a requirement**, and none a slot a fixed size left
                // empty: an empty one asked every device about an undefined format for no reason.
                for (const RequiredFormat& required : getRequiredFormats())
                {
                    EXPECT_NE(required.mFormat, VK_FORMAT_UNDEFINED);
                    EXPECT_FALSE(std::string_view(required.mFor).empty());
                }
            }
            {
                // Quads in the fragment stage alone, and in compute only the basic operations.
                Card fragmentQuads(&describeTuring);
                fragmentQuads.mProperties.mVulkan11.subgroupSupportedStages = VK_SHADER_STAGE_FRAGMENT_BIT;
                EXPECT_EQ(fragmentQuads.profile().mObstacle, "no quad subgroup operations in compute shaders");

                Card basic(&describeTuring);
                basic.mProperties.mVulkan11.subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
                EXPECT_EQ(basic.profile().mObstacle, "no quad subgroup operations in compute shaders");

                // Each of the other two the stages lean on, missing alone.
                Card noBallot(&describeTuring);
                noBallot.mProperties.mVulkan11.subgroupSupportedOperations
                    &= ~VkSubgroupFeatureFlags{ VK_SUBGROUP_FEATURE_BALLOT_BIT };
                EXPECT_EQ(noBallot.profile().mObstacle, "no ballot subgroup operations in compute shaders");

                Card noArithmetic(&describeTuring);
                noArithmetic.mProperties.mVulkan11.subgroupSupportedOperations
                    &= ~VkSubgroupFeatureFlags{ VK_SUBGROUP_FEATURE_ARITHMETIC_BIT };
                EXPECT_EQ(noArithmetic.profile().mObstacle, "no arithmetic subgroup operations in compute shaders");
            }
            {
                // Subgroups of eight to sixteen lanes, and of 32 a compute stage cannot ask for.
                Card narrow(&describeTuring);
                narrow.mProperties.mVulkan13.minSubgroupSize = 8;
                narrow.mProperties.mVulkan13.maxSubgroupSize = 16;
                EXPECT_EQ(narrow.profile().mObstacle, "no compute subgroup of 32 lanes to ask for");

                Card unasked(&describeTuring);
                unasked.mProperties.mVulkan13.requiredSubgroupSizeStages = VK_SHADER_STAGE_FRAGMENT_BIT;
                EXPECT_EQ(unasked.profile().mObstacle, "no compute subgroup of 32 lanes to ask for");

                // AMD's 32 to 64 takes 32.
                Card wide(&describeTuring);
                wide.mProperties.mVulkan13.maxSubgroupSize = 64;
                EXPECT_EQ(wide.profile().mObstacle, "");
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
                // **The texture set's arrays**, two of 8192 images and eight samplers, held to the set's
                // limit and the stage's, whichever is less.
                Card few(&describeAda);
                few.mProperties.mVulkan12.maxPerStageDescriptorUpdateAfterBindSampledImages = 16383;
                EXPECT_EQ(few.profile().mObstacle, "16383 sampled images bound after use, under 16384");
                few.mProperties.mVulkan12.maxPerStageDescriptorUpdateAfterBindSampledImages = 16384;
                few.mProperties.mVulkan12.maxDescriptorSetUpdateAfterBindSamplers = 7;
                EXPECT_EQ(few.profile().mObstacle, "7 samplers bound after use, under 8");
                few.mProperties.mVulkan12.maxDescriptorSetUpdateAfterBindSamplers = 8;
                EXPECT_EQ(few.profile().mObstacle, "") << "exactly what the set needs";
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
                // No video memory the host writes into at all, which staged writes do not need.
                Card walled(&describeTuring);
                walled.mProperties.mMemory.memoryTypeCount = 4;
                EXPECT_EQ(walled.profile().mObstacle, "");
            }
        }

        /// **The first discrete device nothing stops is chosen, else the first nothing stops**, and the survey says
        /// what became of every device: an integrated card listed before a discrete one is passed over, a second
        /// discrete one for the first, and a software rasterizer is refused with its obstacle. With no discrete card
        /// the first standing device is chosen; with none standing, none is.
        TEST(RtxPhysicalDeviceTest, theFirstDiscreteDeviceIsChosenAndTheSurveySaysWhatBecameOfEachDevice)
        {
            const SurveyedDevice integrated{ .mName = "Intel UHD", .mDetails = "integrated", .mObstacle = "" };
            const SurveyedDevice rtx{ .mName = "RTX", .mDetails = "discrete", .mObstacle = "", .mDiscrete = true };
            const SurveyedDevice second{ .mName = "RTX 2", .mDetails = "discrete", .mObstacle = "", .mDiscrete = true };
            const SurveyedDevice software{
                .mName = "llvmpipe", .mDetails = "software", .mObstacle = "no ray tracing pipeline"
            };

            const std::vector<SurveyedDevice> laptop{ integrated, software, rtx, second };
            ASSERT_EQ(chooseDevice(laptop), 2u);
            EXPECT_EQ(describeSurvey(laptop, 2),
                "  Intel UHD: passed over for a discrete device (integrated)\n"
                "  llvmpipe: refused, no ray tracing pipeline (software)\n"
                "  RTX: chosen (discrete)\n"
                "  RTX 2: passed over for one listed before it (discrete)");

            const std::vector<SurveyedDevice> handheld{ software, integrated, integrated };
            ASSERT_EQ(chooseDevice(handheld), 1u);
            EXPECT_EQ(describeSurvey(handheld, 1),
                "  llvmpipe: refused, no ray tracing pipeline (software)\n"
                "  Intel UHD: chosen (integrated)\n"
                "  Intel UHD: passed over for one listed before it (integrated)");

            const std::vector<SurveyedDevice> none{ software };
            EXPECT_EQ(chooseDevice(none), std::nullopt);
            EXPECT_EQ(describeSurvey(none, std::nullopt), "  llvmpipe: refused, no ray tracing pipeline (software)");
            EXPECT_EQ(describeSurvey(std::vector<SurveyedDevice>{}, std::nullopt), "");
            EXPECT_EQ(chooseDevice(std::vector<SurveyedDevice>{}), std::nullopt);
        }
    }
}
