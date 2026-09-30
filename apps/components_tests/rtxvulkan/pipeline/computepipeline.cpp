#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

#include "../../rtx/support/death.hpp"

namespace Rtx
{
    namespace
    {
        /// Nothing of its own — a name, because `TEST_F` takes one identifier and prints it as the
        /// suite this test is reported under.
        using RtxComputePipelineTest = Testing::DeviceTest;

        // The push range is the constants' type: its size, and nought for a pipeline pushed nothing,
        // whose layout then declares no range, since Vulkan takes no empty one.
        static_assert(pushBytesOf<NoConstants>() == 0);
        static_assert(pushBytesOf<float>() == sizeof(float));

        /// A pipeline whose shader cannot be opened gives back everything it had already made.
        ///
        /// **The failure comes third.** The set layout and the pipeline layout are live by the time
        /// the module is looked for, and a constructor that throws gets no destructor — so without
        /// the unwind inside `ComputePipeline` both outlive the device. What proves they did not is
        /// the device closing with the layers watching and saying nothing: a live child is what
        /// `vkDestroyDevice` reports.
        ///
        /// A device of its own, closed inside the test, because the suite's is closed after the last
        /// test has run and could not be asked.
        ///
        /// **On the suite's instance, and with no pipeline cache.** A second instance loads the
        /// layers again for an answer this already has, and a cache is a quarter of a gigabyte read
        /// and written for a test whose one pipeline never compiles.
        TEST_F(RtxComputePipelineTest, aMissingShaderLeavesNothingBehindOnTheDevice)
        {
            const Instance& instance = *mHarness.mInstance;
            auto device = std::make_unique<Device>(instance, PhysicalDevice::select(instance.getHandle()),
                Testing::getShaderDirectory(), PipelineCacheSpec{});

            ValidationLog* log = instance.getValidationLog();
            ASSERT_NE(log, nullptr) << "the layers are what this test reads its answer from";

            constexpr std::array<VkDescriptorSetLayoutBinding, 1> bindings{
                VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            };
            // Pushed a float, so the layout the unwind has to give back holds a push range too.
            EXPECT_THROW(ComputePipeline<float>(*device, bindings, {}, "no-such.comp.spv", "scratch"), InputError);

            log->clear();
            device.reset();

            std::vector<std::string> raised;
            log->takeErrorsOnThisThread(raised);
            for (const std::string& message : raised)
                ADD_FAILURE() << "validation error at device teardown: " << message;
        }

        /// **The writes take their types and counts from the table they are made against**, and
        /// hold the pass to it: a sampled image, a buffer and an array of two storage images come
        /// back as three writes of the table's types, counts 1, 1 and 2, into the set named. A binding
        /// left unwritten dies, and so does an array given one image of its two.
        TEST_F(RtxComputePipelineTest, descriptorWritesTakeTheTableTheyAreMadeAgainst)
        {
            constexpr std::array<VkDescriptorSetLayoutBinding, 3> bindings{
                VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
                VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
                VkDescriptorSetLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, VK_SHADER_STAGE_COMPUTE_BIT },
            };
            const SetLayout layout = makeSetLayout(getDevice(), bindings);

            // Never handed to Vulkan: a handle to compare against, not a set.
            const auto set = reinterpret_cast<VkDescriptorSet>(std::uintptr_t{ 0x5e7 });
            constexpr VkDescriptorImageInfo image{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL };
            constexpr std::array<VkDescriptorImageInfo, 2> pair{ image, image };
            constexpr VkDescriptorBufferInfo block{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };

            DescriptorWrites writes(layout, set);
            writes.image(0, image);
            writes.buffer(1, block);
            writes.images(2, pair);

            const std::span<const VkWriteDescriptorSet> written = writes.get();
            ASSERT_EQ(written.size(), bindings.size());
            for (std::size_t at = 0; at < bindings.size(); ++at)
            {
                SCOPED_TRACE(at);
                EXPECT_EQ(written[at].dstSet, set);
                EXPECT_EQ(written[at].dstBinding, bindings[at].binding);
                EXPECT_EQ(written[at].descriptorType, bindings[at].descriptorType);
                EXPECT_EQ(written[at].descriptorCount, bindings[at].descriptorCount);
            }
            EXPECT_TRUE(writes.against(layout.getBindings()));

            Testing::expectAssertDies(
                [&] {
                    DescriptorWrites partial(layout, set);
                    partial.image(0, image);
                    static_cast<void>(partial.get());
                },
                "a binding the table declares was left unwritten");
            Testing::expectAssertDies(
                [&] {
                    DescriptorWrites halfArray(layout, set);
                    halfArray.image(0, image);
                    halfArray.buffer(1, block);
                    halfArray.image(2, image);
                },
                "a binding written with another count than the table's");
        }
    }
}
