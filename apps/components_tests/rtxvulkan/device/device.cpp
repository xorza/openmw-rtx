#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/notfinitecensus.hpp>
#include <components/rtxvulkan/device/owned.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>

namespace Rtx
{
    namespace
    {
        /// Nothing of its own — a name, because `TEST_F` takes one identifier and prints it as the
        /// suite these tests are reported under.
        using RtxDeviceTest = Testing::DeviceTest;

        /// A wait on a device that never answers ends the process as a crash, and says which wait it
        /// was.
        ///
        /// **The alternative cannot be told from success.** `vkWaitForFences` with no timeout makes a
        /// device that will never signal and one still working the same call, and a stalled submit
        /// took the whole suite with it — a wedged process, a GPU at full tilt, and no message. Every
        /// wait in this renderer now has a deadline; this is the one that proves the deadline fires
        /// rather than being a number nobody has ever reached.
        ///
        /// A fence nothing submits against, and a patience short enough that the test does not sit
        /// out the real one.
        TEST_F(RtxDeviceTest, aWaitOnADeviceThatNeverAnswersEndsAndNamesItself)
        {
            const VkFenceCreateInfo unsignalled{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0
            };

            VkFence fence = VK_NULL_HANDLE;
            ASSERT_EQ(vkCreateFence(mHarness.mDevice->getHandle(), &unsignalled, nullptr, &fence), VK_SUCCESS);

            // Named, because a count says a frame is stuck and nothing about which one. A crash,
            // because nothing goes on from a device that stopped answering, and the report is
            // taken where it was found.
            Testing::expectDies([&] { awaitVk(*mHarness.mDevice, fence, "a submit nobody made", 1'000'000ull); },
                "a submit nobody made did not complete within 1 ms; the device has stopped answering");

            vkDestroyFence(mHarness.mDevice->getHandle(), fence, nullptr);
        }

        /// **A create that fails adopts nothing.** Its output is undefined after a failure, and a
        /// driver may leave anything there: an owner that took it would end a handle that never
        /// was. A create that writes a sentinel and fails throws, and nothing waits to be ended.
        TEST_F(RtxDeviceTest, aCreateThatFailsAdoptsNothing)
        {
            const Device& device = *mHarness.mDevice;
            const auto failing
                = [](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* out) {
                      *out = std::bit_cast<VkSampler>(std::uint64_t{ 0xdead });
                      return VK_ERROR_OUT_OF_HOST_MEMORY;
                  };
            const std::size_t held = device.getGraveyard().getHeldCount();

            using Deferred = Owned<VkSampler, vkDestroySampler>;
            EXPECT_THROW(Deferred::make(device, failing, VkSamplerCreateInfo{}, "a failed create"), DeviceError);
            EXPECT_EQ(device.getGraveyard().getHeldCount(), held) << "a failed create's output was buried";

            // Ended at once where it was adopted, which would destroy the sentinel here.
            using AtOnce = Immediate<VkSampler, vkDestroySampler>;
            EXPECT_THROW(
                AtOnce::make(device.getHandle(), failing, VkSamplerCreateInfo{}, "a failed create"), DeviceError);
        }

        TEST_F(RtxDeviceTest, theValidationLayerIsLoaded)
        {
            // Without this every other test's clean bill of health means nothing.
            EXPECT_NE(mHarness.mInstance->getValidationLog(), nullptr);
        }

        TEST_F(RtxDeviceTest, theDeviceHasAQueueAndEveryExtensionEntryPoint)
        {
            EXPECT_NE(mHarness.mDevice->getHandle(), VK_NULL_HANDLE);
            EXPECT_NE(mHarness.mDevice->getQueue(), VK_NULL_HANDLE);

            // Device construction throws when any of these is missing, so reaching here already
            // proves it; asserting names the contract for anyone reading the failure.
            const DeviceFunctions& functions = mHarness.mDevice->getFunctions();
            EXPECT_NE(functions.mCmdBuildAccelerationStructures, nullptr);
            EXPECT_NE(functions.mGetAccelerationStructureDeviceAddress, nullptr);
            EXPECT_NE(functions.mGetAccelerationStructureBuildSizes, nullptr);

            // The one optional entry point, present exactly where the driver offers its extension.
            // An extension enabled and never read is what this proves gone.
            const PhysicalDevice& physical = mHarness.mDevice->getPhysicalDevice();
            EXPECT_EQ(mHarness.mDevice->canDescribeFault(),
                physical.hasOptionalExtension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME));
        }

        /// The device and the renderer are made without a validation error. What the layers raise
        /// while either is made reaches no test's own drain, which takes whatever is on the log for
        /// a previous test's and drops it.
        TEST_F(RtxDeviceTest, theDeviceAndTheRendererAreMadeWithoutAValidationError)
        {
            for (const std::string& error : mHarness.mMadeWith)
                ADD_FAILURE() << "making the device: " << error;

            // Made before any test ran, by `DeviceEnvironment`.
            for (const std::string& error : Testing::getRendererMadeWith())
                ADD_FAILURE() << "making the renderer: " << error;
        }

        /// A device with no window takes no option that rests on a swapchain, whatever the driver
        /// offers: no present fence. The test's device is made on an instance with no surface, as
        /// every headless run's is.
        TEST_F(RtxDeviceTest, aDeviceWithNoWindowTakesNoOptionThatRestsOnASwapchain)
        {
            ASSERT_FALSE(mHarness.mInstance->hasExtension(VK_KHR_SURFACE_EXTENSION_NAME));

            EXPECT_FALSE(mHarness.mDevice->hasPresentFences());
        }

        /// **A module's census word is its place among the census set's modules, sorted by name**,
        /// whichever compile hand asked first, so a pipeline is the same to the driver's cache from
        /// run to run: words handed out in the order pipelines were made moved with the trace's
        /// compile threads, and the cache missed the trace's every pipeline. `accumulate` sorts
        /// before `accumulateclamp`, `.` before `c`, and the `.spv` is no part of the name. A module
        /// the directory does not hold is refused by name.
        TEST_F(RtxDeviceTest, aModulesCensusWordIsItsPlaceInTheSortedDirectory)
        {
            const NotFiniteCensus* const census = getDevice().getCensus();
            ASSERT_NE(census, nullptr) << "the tests' device reads the census set";

            std::vector<std::string> modules;
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(getDevice().getShaderDirectory()))
                if (entry.path().extension() == ".spv")
                    modules.push_back(Files::pathToUnicodeString(entry.path().filename()));
            std::sort(modules.begin(), modules.end());
            ASSERT_FALSE(modules.empty());

            for (std::uint32_t at = 0; at < modules.size(); ++at)
                EXPECT_EQ(census->kernelOf(modules[at]), at) << modules[at];

            EXPECT_EQ(census->kernelOf("accumulate.comp.spv") + 1, census->kernelOf("accumulateclamp.comp.spv"));
            EXPECT_EQ(census->kernelOf("accumulate.comp"), census->kernelOf("accumulate.comp.spv"));
            EXPECT_THROW(census->kernelOf("nothing.comp.spv"), InputError);
        }

        TEST_F(RtxDeviceTest, everyRequiredFeatureIsActuallySupported)
        {
            DeviceFeatures supported;
            vkGetPhysicalDeviceFeatures2(mHarness.mDevice->getPhysicalDevice().getHandle(), &supported.mFeatures2);

            std::vector<std::string_view> missing;
            findMissingFeatures(supported, missing);

            EXPECT_TRUE(missing.empty()) << "first missing: " << (missing.empty() ? "" : missing.front());
        }

        TEST_F(RtxDeviceTest, theReportNamesTheDeviceAndItsRayTracingLimits)
        {
            const std::string report = mHarness.mDevice->getPhysicalDevice().describe();

            EXPECT_NE(
                report.find(mHarness.mDevice->getPhysicalDevice().getProperties().mProperties2.properties.deviceName),
                std::string::npos);
            EXPECT_NE(report.find("max primitive count"), std::string::npos);
        }
    }
}
