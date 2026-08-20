#include <filesystem>
#include <memory>
#include <optional>
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
        /// the difference between a suite that gets run and one that does not.
        struct Harness
        {
            Instance mInstance;
            Device mDevice;

            Harness()
                : mInstance(makeOptions())
                , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle()))
            {
            }

            static InstanceOptions makeOptions()
            {
                InstanceOptions options;
                options.mValidation = true;
                // Tests provoke errors deliberately and assert on them; aborting would take the
                // suite down with the first one.
                options.mPolicy = ValidationPolicy::Log;
                return options;
            }
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

                auto instance = std::make_unique<Instance>(Harness::makeOptions());
                std::uint32_t count = 0;
                if (vkEnumeratePhysicalDevices(instance->getHandle(), &count, nullptr) != VK_SUCCESS || count == 0)
                {
                    sReason = "no Vulkan device is installed";
                    return nullptr;
                }

                return std::make_unique<Harness>();
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

                mHarness->mInstance.getValidationLog()->clear();
            }

            void TearDown() override
            {
                if (mHarness == nullptr)
                    return;

                for (const ValidationMessage& message : mHarness->mInstance.getValidationLog()->getErrorsOnThisThread())
                    ADD_FAILURE() << "validation error: " << message.mText;
            }

            Harness* mHarness = nullptr;
        };

        TEST_F(RtxDeviceTest, theValidationLayerIsLoaded)
        {
            // Without this every other test's clean bill of health means nothing.
            EXPECT_NE(mHarness->mInstance.getValidationLog(), nullptr);
        }

        TEST_F(RtxDeviceTest, theDeviceHasAQueueAndEveryExtensionEntryPoint)
        {
            EXPECT_NE(mHarness->mDevice.getHandle(), VK_NULL_HANDLE);
            EXPECT_NE(mHarness->mDevice.getQueue(), VK_NULL_HANDLE);

            // Device construction throws when any of these is missing, so reaching here already
            // proves it; asserting names the contract for anyone reading the failure.
            const DeviceFunctions& functions = mHarness->mDevice.getFunctions();
            EXPECT_NE(functions.mCmdTraceRays, nullptr);
            EXPECT_NE(functions.mCmdBuildAccelerationStructures, nullptr);
            EXPECT_NE(functions.mGetAccelerationStructureBuildSizes, nullptr);
            EXPECT_NE(functions.mCmdBuildMicromaps, nullptr);
        }

        TEST_F(RtxDeviceTest, everyRequiredFeatureIsActuallySupported)
        {
            DeviceFeatures supported;
            vkGetPhysicalDeviceFeatures2(mHarness->mDevice.getPhysicalDevice().getHandle(), &supported.mFeatures2);

            std::vector<std::string_view> missing;
            findMissingFeatures(supported, missing);

            EXPECT_TRUE(missing.empty()) << "first missing: " << (missing.empty() ? "" : missing.front());
        }

        TEST_F(RtxDeviceTest, theShaderBuildStepProducesLoadableModules)
        {
            const std::filesystem::path probe = std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "probe.comp.spv";
            ASSERT_TRUE(std::filesystem::exists(probe)) << probe;

            const ShaderModule module(mHarness->mDevice, probe);
            EXPECT_NE(module.getHandle(), VK_NULL_HANDLE);
        }

        TEST_F(RtxDeviceTest, aFileThatIsNotSpirvIsRejectedRatherThanHandedToTheDriver)
        {
            const std::filesystem::path missing
                = std::filesystem::path(OPENMW_RTX_SHADER_DIR) / "there-is-no-such-shader.spv";
            EXPECT_THROW(ShaderModule(mHarness->mDevice, missing), Error);
        }

        TEST_F(RtxDeviceTest, theReportNamesTheDeviceAndItsRayTracingLimits)
        {
            const std::string report = mHarness->mDevice.getPhysicalDevice().describe();

            EXPECT_NE(
                report.find(mHarness->mDevice.getPhysicalDevice().getProperties().mProperties2.properties.deviceName),
                std::string::npos);
            EXPECT_NE(report.find("max ray recursion depth"), std::string::npos);
        }
    }
}
