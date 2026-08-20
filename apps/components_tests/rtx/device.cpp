#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/device.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/instance.hpp>
#include <components/rtx/physicaldevice.hpp>
#include <components/rtx/requirements.hpp>
#include <components/rtx/shadermodule.hpp>

namespace Rtx
{
    namespace
    {
        /// Instance and device for the whole binary.
        ///
        /// Bring-up costs a good fraction of a second and nothing here mutates it, so paying once is
        /// the difference between a suite that gets run and one that does not. Both are held by
        /// pointer so the instance can be built and inspected before there is a device to pair it
        /// with, and so it is destroyed last.
        struct Harness
        {
            std::unique_ptr<Instance> mInstance;
            std::unique_ptr<Device> mDevice;
        };

        /// Null when this machine has no Vulkan device at all, with `reason` saying so.
        ///
        /// A machine without a GPU legitimately cannot run these, and skipping is honest. A machine
        /// *with* one that does not meet the requirements is a finding, so that throws out of
        /// `PhysicalDevice::select` and fails the suite rather than skipping.
        Harness* getHarness(std::string& reason)
        {
            static std::string sReason;
            static const std::unique_ptr<Harness> sHarness = []() -> std::unique_ptr<Harness> {
                std::uint32_t version = 0;
                if (vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < sApiVersion)
                {
                    sReason = "the Vulkan loader is absent or older than this renderer requires";
                    return nullptr;
                }

                InstanceOptions options;
                options.mValidation = true;
                // Tests provoke errors deliberately and assert on them; aborting would take the
                // suite down with the first one.
                options.mPolicy = ValidationPolicy::Log;

                auto harness = std::make_unique<Harness>();
                harness->mInstance = std::make_unique<Instance>(options);

                std::uint32_t count = 0;
                if (vkEnumeratePhysicalDevices(harness->mInstance->getHandle(), &count, nullptr) != VK_SUCCESS
                    || count == 0)
                {
                    sReason = "no Vulkan device is installed";
                    return nullptr;
                }

                harness->mDevice = std::make_unique<Device>(
                    *harness->mInstance, PhysicalDevice::select(harness->mInstance->getHandle()));
                return harness;
            }();

            reason = sReason;
            return sHarness.get();
        }

        class RtxDeviceTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                std::string reason;
                mHarness = getHarness(reason);
                if (mHarness == nullptr)
                    GTEST_SKIP() << reason;

                mHarness->mInstance->getValidationLog()->clear();
            }

            void TearDown() override
            {
                if (mHarness == nullptr)
                    return;

                for (const ValidationMessage& message :
                    mHarness->mInstance->getValidationLog()->getErrorsOnThisThread())
                    ADD_FAILURE() << "validation error: " << message.mText;
            }

            Harness* mHarness = nullptr;
        };

        /// Object names are what make a capture readable, and a capture is most wanted on a run that
        /// is not carrying the layers — so the two are enabled independently. Needs its own instance:
        /// the shared harness always asks for validation.
        TEST(RtxInstanceTest, objectNamesDoNotNeedTheValidationLayers)
        {
            std::uint32_t version = 0;
            if (vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < sApiVersion)
                GTEST_SKIP() << "the Vulkan loader is absent or older than this renderer requires";

            const Instance instance{ InstanceOptions{} };

            EXPECT_EQ(instance.getValidationLog(), nullptr);
#ifdef OPENMW_RTX_DEBUG_NAMES
            EXPECT_TRUE(instance.hasDebugUtils());
#else
            EXPECT_FALSE(instance.hasDebugUtils());
#endif
        }

        TEST_F(RtxDeviceTest, theValidationLayerIsLoaded)
        {
            // Without this every other test's clean bill of health means nothing.
            EXPECT_NE(mHarness->mInstance->getValidationLog(), nullptr);
        }

        TEST_F(RtxDeviceTest, theDeviceHasAQueueAndEveryExtensionEntryPoint)
        {
            EXPECT_NE(mHarness->mDevice->getHandle(), VK_NULL_HANDLE);
            EXPECT_NE(mHarness->mDevice->getQueue(), VK_NULL_HANDLE);

            // Device construction throws when any of these is missing, so reaching here already
            // proves it; asserting names the contract for anyone reading the failure.
            const DeviceFunctions& functions = mHarness->mDevice->getFunctions();
            EXPECT_NE(functions.mCmdTraceRays, nullptr);
            EXPECT_NE(functions.mCmdBuildAccelerationStructures, nullptr);
            EXPECT_NE(functions.mGetAccelerationStructureBuildSizes, nullptr);
            EXPECT_NE(functions.mCmdBuildMicromaps, nullptr);
        }

        TEST_F(RtxDeviceTest, everyRequiredFeatureIsActuallySupported)
        {
            DeviceFeatures supported;
            vkGetPhysicalDeviceFeatures2(mHarness->mDevice->getPhysicalDevice().getHandle(), &supported.mFeatures2);

            std::vector<std::string_view> missing;
            findMissingFeatures(supported, missing);

            EXPECT_TRUE(missing.empty()) << "first missing: " << (missing.empty() ? "" : missing.front());
        }

        TEST_F(RtxDeviceTest, theShaderBuildStepProducesLoadableModules)
        {
            const std::filesystem::path probe = std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "probe.comp.spv";
            ASSERT_TRUE(std::filesystem::exists(probe)) << probe;

            const ShaderModule module(*mHarness->mDevice, probe);
            EXPECT_NE(module.getHandle(), VK_NULL_HANDLE);
        }

        TEST_F(RtxDeviceTest, aFileThatIsNotSpirvIsRejectedRatherThanHandedToTheDriver)
        {
            const std::filesystem::path missing
                = std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "there-is-no-such-shader.spv";
            EXPECT_THROW(ShaderModule(*mHarness->mDevice, missing), Error);
        }

        TEST_F(RtxDeviceTest, theReportNamesTheDeviceAndItsRayTracingLimits)
        {
            const std::string report = mHarness->mDevice->getPhysicalDevice().describe();

            EXPECT_NE(
                report.find(mHarness->mDevice->getPhysicalDevice().getProperties().mProperties2.properties.deviceName),
                std::string::npos);
            EXPECT_NE(report.find("max ray recursion depth"), std::string::npos);
        }
    }
}
