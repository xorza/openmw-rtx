#ifndef OPENMW_APPS_COMPONENTS_TESTS_RTX_HARNESS_H
#define OPENMW_APPS_COMPONENTS_TESTS_RTX_HARNESS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <components/rtx/device.hpp>
#include <components/rtx/instance.hpp>
#include <components/rtx/physicaldevice.hpp>
#include <components/rtx/requirements.hpp>

namespace Rtx::Testing
{
    /// Instance and device for the whole test binary.
    ///
    /// Bring-up costs a good fraction of a second and nothing mutates it, so paying once is the
    /// difference between a suite that gets run and one that does not. Both are held by pointer so
    /// the instance can be built and inspected before there is a device to pair it with, and so it
    /// is destroyed last.
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
    inline Harness* getHarness(std::string& reason)
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
            // Tests provoke errors deliberately and assert on them; aborting would take the suite
            // down with the first one.
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

    /// Where the build wrote the compiled shaders.
    inline std::filesystem::path getShaderDirectory()
    {
        return std::filesystem::path(OPENMW_RTX_SHADER_DIR);
    }
}

#endif
