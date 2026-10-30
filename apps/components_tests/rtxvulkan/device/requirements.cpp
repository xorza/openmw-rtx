#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/requirements.hpp>

namespace Rtx
{
    namespace
    {
        TEST(RtxRequirementsTest, versionStringSpellsThePackedVersion)
        {
            EXPECT_EQ(versionString(VK_MAKE_API_VERSION(0, 1, 4, 357)), "1.4.357");
            EXPECT_EQ(versionString(sApiVersion), "1.4.0");
            EXPECT_EQ(versionString(VK_MAKE_API_VERSION(0, 0, 0, 0)), "0.0.0");
        }

        /// Every feature structure must be reachable from the head, or `vkGetPhysicalDeviceFeatures2`
        /// leaves it zeroed and the device is rejected over a feature nobody asked it about — with a
        /// message naming the feature rather than the missing link.
        TEST(RtxRequirementsTest, everyFeatureStructIsInTheChain)
        {
            DeviceFeatures features;

            std::set<const void*> linked;
            for (const VkBaseInStructure* next = reinterpret_cast<const VkBaseInStructure*>(&features.mFeatures2);
                 next != nullptr; next = next->pNext)
                linked.insert(next);

            EXPECT_EQ(linked.size(), 11u) << "a member was added to DeviceFeatures without chaining it";
            EXPECT_TRUE(linked.contains(&features.mFeatures2));
            EXPECT_TRUE(linked.contains(&features.mVulkan12));
            EXPECT_TRUE(linked.contains(&features.mVulkan13));
            EXPECT_TRUE(linked.contains(&features.mVulkan14));
            EXPECT_TRUE(linked.contains(&features.mAccelerationStructure));
            EXPECT_TRUE(linked.contains(&features.mRayQuery));
            EXPECT_TRUE(linked.contains(&features.mPositionFetch));
            EXPECT_TRUE(linked.contains(&features.mRayTracingMaintenance1));
            EXPECT_TRUE(linked.contains(&features.mRayTracingPipeline));
            EXPECT_TRUE(linked.contains(&features.mPipelineExecutable));
            EXPECT_TRUE(linked.contains(&features.mShaderClock));
        }

        /// **A candidate is asked only the structures of what it offers**: of a device that lists no
        /// extension, the core structures alone, and with the ray tracing pipeline listed, that one
        /// beside them. A structure of an extension a device does not list is no valid member of
        /// the chain.
        TEST(RtxRequirementsTest, aCandidateIsAskedTheStructuresOfWhatItOffers)
        {
            const auto count = [](const void* head) {
                std::size_t linked = 0;
                for (const VkBaseInStructure* next = static_cast<const VkBaseInStructure*>(head); next != nullptr;
                     next = next->pNext)
                    ++linked;
                return linked;
            };

            const DeviceFeatures bare(std::span<const std::string>{});
            EXPECT_EQ(count(&bare.mFeatures2), 4u) << "the features of 1.0 and 1.2 to 1.4, and nothing else";
            const DeviceProperties plain(std::span<const std::string>{});
            EXPECT_EQ(count(&plain.mProperties2), 4u) << "the properties of 1.0 to 1.3, and nothing else";

            const std::array<std::string, 1> pipeline{ VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME };
            const DeviceFeatures tracing(pipeline);
            EXPECT_EQ(count(&tracing.mFeatures2), 5u);
            EXPECT_EQ(tracing.mFeatures2.pNext, &tracing.mVulkan12);
            const DeviceProperties traced(pipeline);
            EXPECT_EQ(count(&traced.mProperties2), 5u);
        }

        TEST(RtxRequirementsTest, everyPropertyStructIsInTheChain)
        {
            DeviceProperties properties;

            std::set<const void*> linked;
            for (const VkBaseInStructure* next = reinterpret_cast<const VkBaseInStructure*>(&properties.mProperties2);
                 next != nullptr; next = next->pNext)
                linked.insert(next);

            EXPECT_EQ(linked.size(), 6u) << "a member was added to DeviceProperties without chaining it";
            EXPECT_TRUE(linked.contains(&properties.mVulkan11));
            EXPECT_TRUE(linked.contains(&properties.mVulkan12));
            EXPECT_TRUE(linked.contains(&properties.mVulkan13));
            EXPECT_TRUE(linked.contains(&properties.mAccelerationStructure));
            EXPECT_TRUE(linked.contains(&properties.mRayTracingPipeline));
        }

        /// A hand-written table of two dozen accessors is where a copy-paste sends two entries at the
        /// same field, and nothing else would notice: both would be requested, both would read as
        /// supported, and the feature one of them was meant to name would never be asked for.
        TEST(RtxRequirementsTest, everyRequiredFeatureAddressesADistinctField)
        {
            DeviceFeatures features;

            std::set<const VkBool32*> seen;
            for (const RequiredFeature& required : getRequiredDeviceFeatures())
                EXPECT_TRUE(seen.insert(&required.mField(features)).second) << required.mName;

            EXPECT_EQ(seen.size(), getRequiredDeviceFeatures().size());
        }

        /// **What each option rests on is stated**, which the registry's `depends` says: paging by
        /// priority rests on the priority, and a present fence rests on a swapchain and the
        /// instance's half of its maintenance. Taken without them, it would go to every headless
        /// device. No option is also required.
        TEST(RtxRequirementsTest, theOptionsAreTheirExtensionsWholeAndStateWhatTheyRestOn)
        {
            const auto names = [](std::span<const char* const> list) {
                return std::vector<std::string_view>(list.begin(), list.end());
            };
            const std::span<const OptionalExtensions> options = getOptionalExtensions();
            ASSERT_EQ(options.size(), sDeviceOptions);

            const OptionalExtensions& fences = options[static_cast<std::size_t>(DeviceOption::PresentFences)];
            EXPECT_EQ(names(fences.mNeeds),
                (std::vector<std::string_view>{
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME }));

            // A present's id rests on a swapchain, and a wait for one on the id as well.
            EXPECT_EQ(names(options[static_cast<std::size_t>(DeviceOption::PresentIds)].mNeeds),
                (std::vector<std::string_view>{ VK_KHR_SWAPCHAIN_EXTENSION_NAME }));
            EXPECT_EQ(names(options[static_cast<std::size_t>(DeviceOption::PresentWait)].mNeeds),
                (std::vector<std::string_view>{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME }));

            // Paging by priority rests on the priority.
            const OptionalExtensions& pageable = options[static_cast<std::size_t>(DeviceOption::PageableMemory)];
            EXPECT_EQ(names(pageable.mNeeds), (std::vector<std::string_view>{ VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME }));

            for (const DeviceOption alone :
                { DeviceOption::FaultReport, DeviceOption::MemoryBudget, DeviceOption::MemoryPriority,
                    DeviceOption::Checkpoints, DeviceOption::BufferMarkers, DeviceOption::PerformanceQuery })
                EXPECT_TRUE(options[static_cast<std::size_t>(alone)].mNeeds.empty())
                    << "an option core 1.4 carries alone names a need";

            // Checkpoints and markers are set where the build names things, and taken only there.
            for (const OptionalExtensions& option : options)
            {
                const bool breadcrumb
                    = option.mOption == DeviceOption::Checkpoints || option.mOption == DeviceOption::BufferMarkers;
                EXPECT_EQ(option.mRead, sDebugNames || !breadcrumb);
            }

            const std::span<const char* const> required = getRequiredDeviceExtensions();
            for (const OptionalExtensions& option : options)
                for (const char* const name : option.mExtensions)
                    EXPECT_EQ(std::find_if(required.begin(), required.end(),
                                  [&](const char* held) { return std::string_view(held) == name; }),
                        required.end())
                        << name << " is both required and optional";

            // **Each option that brings a feature reaches its own**, structure and flag, and the
            // structure is typed for the query: the two chains `Device` builds are this table, so a
            // row pointed at another option's structure asks the device the wrong question.
            OptionalFeatures features;
            const auto reaches
                = [&](DeviceOption option, const void* structure, const VkBool32* field, VkStructureType type) {
                      const OptionalFeature* feature = options[static_cast<std::size_t>(option)].mFeature;
                      ASSERT_NE(feature, nullptr) << static_cast<int>(option);
                      EXPECT_EQ(&feature->mStructure(features), structure) << static_cast<int>(option);
                      EXPECT_EQ(&feature->mField(features), field) << static_cast<int>(option);
                      EXPECT_EQ(feature->mStructure(features).sType, type) << static_cast<int>(option);
                  };
            reaches(DeviceOption::FaultReport, &features.mFault, &features.mFault.deviceFault,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT);
            reaches(DeviceOption::MemoryPriority, &features.mPriority, &features.mPriority.memoryPriority,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT);
            reaches(DeviceOption::PageableMemory, &features.mPageable, &features.mPageable.pageableDeviceLocalMemory,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT);
            reaches(DeviceOption::PresentFences, &features.mPresentFences,
                &features.mPresentFences.swapchainMaintenance1,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR);
            reaches(DeviceOption::PresentIds, &features.mPresentIds, &features.mPresentIds.presentId,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR);
            reaches(DeviceOption::PresentWait, &features.mPresentWait, &features.mPresentWait.presentWait,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR);
            reaches(DeviceOption::PerformanceQuery, &features.mPerformanceQuery,
                &features.mPerformanceQuery.performanceCounterQueryPools,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR);
            for (const DeviceOption bare :
                { DeviceOption::MemoryBudget, DeviceOption::Checkpoints, DeviceOption::BufferMarkers })
                EXPECT_EQ(options[static_cast<std::size_t>(bare)].mFeature, nullptr) << static_cast<int>(bare);
        }

        /// **An option falls with what it needs**, whatever order the table lists them in, and
        /// stands where what it needs is given or taken. Every option offered: with nothing given,
        /// the present fences fall, since no swapchain is enabled; with the priority not taken,
        /// paging by priority falls with it; and every option that needs nothing stands. Given the
        /// swapchain and its surface's maintenance, the fences stand.
        TEST(RtxRequirementsTest, anOptionFallsWithWhatItNeeds)
        {
            const std::span<const OptionalExtensions> options = getOptionalExtensions();
            const auto index = [](DeviceOption option) { return static_cast<std::size_t>(option); };

            std::array<bool, sDeviceOptions> taken{};
            taken.fill(true);
            taken[index(DeviceOption::MemoryPriority)] = false;
            dropUnmetNeeds(options, taken, [](const char*) { return false; });
            EXPECT_FALSE(taken[index(DeviceOption::PresentFences)]);
            EXPECT_FALSE(taken[index(DeviceOption::PresentIds)]);
            EXPECT_FALSE(taken[index(DeviceOption::PresentWait)]);
            EXPECT_FALSE(taken[index(DeviceOption::PageableMemory)]);
            for (const OptionalExtensions& option : options)
            {
                const bool alone = option.mNeeds.empty() && option.mOption != DeviceOption::MemoryPriority;
                EXPECT_TRUE(!alone || taken[index(option.mOption)]) << "an option that needs nothing fell";
            }

            taken.fill(true);
            dropUnmetNeeds(options, taken, [](const char* name) {
                return std::string_view(name) == VK_KHR_SWAPCHAIN_EXTENSION_NAME
                    || std::string_view(name) == VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
            });
            EXPECT_TRUE(taken[index(DeviceOption::PresentFences)]);
            EXPECT_TRUE(taken[index(DeviceOption::PageableMemory)]) << "the priority it rests on is taken";
            EXPECT_TRUE(taken[index(DeviceOption::PresentIds)]) << "the swapchain it rests on is given";
            EXPECT_TRUE(taken[index(DeviceOption::PresentWait)]) << "and the id, an option still taken";

            // **The wait falls with the id**, which a driver may offer without the wait.
            taken.fill(true);
            taken[index(DeviceOption::PresentIds)] = false;
            dropUnmetNeeds(options, taken,
                [](const char* name) { return std::string_view(name) == VK_KHR_SWAPCHAIN_EXTENSION_NAME; });
            EXPECT_FALSE(taken[index(DeviceOption::PresentWait)]);
        }

        /// The two directions of the table have to agree: what `requestRequiredFeatures` writes is
        /// exactly what `findMissingFeatures` reads.
        TEST(RtxRequirementsTest, requestingEveryRequiredFeatureLeavesNothingMissing)
        {
            DeviceFeatures features;

            std::vector<std::string_view> missing;
            findMissingFeatures(features, missing);
            EXPECT_EQ(missing.size(), getRequiredDeviceFeatures().size())
                << "a freshly zeroed chain supports nothing, so every entry should be reported";

            requestRequiredFeatures(features);
            missing.clear();
            findMissingFeatures(features, missing);
            EXPECT_TRUE(missing.empty()) << "first still missing: " << (missing.empty() ? "" : missing.front());
        }
    }
}
