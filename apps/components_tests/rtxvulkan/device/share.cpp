#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/share.h>

namespace Rtx
{
    namespace
    {
        using RtxShareTest = Testing::DeviceTest;

        /// A share's term is its value in twenty fractional bits, and a value past what a term can
        /// name is clamped to the largest and not converted out of range.
        ///
        /// **Hand-computed.** One is 2^20, 1048576; a half 524288; 3.25 is 3407872; 4095 is 4095 ·
        /// 2^20 = 4293918720, a float exactly. 4096 is 2^32, one past a `uint`, and 10^9 far past
        /// it: both are `SHARE_MOST`, the largest float under 2^32, 2^32 − 256 = 4294967040. A
        /// negative value is nought, and so is a NaN; an infinity is `SHARE_MOST`. **And a sum
        /// saturates**: a term added to itself is twice it below the top — 2097152 for one — and
        /// the largest `uint`, 4294967295, past it.
        TEST_F(RtxShareTest, aTermIsItsValueInTwentyBitsAndAPastTermIsClamped)
        {
            const Device& device = getDevice();
            constexpr std::array<VkDescriptorSetLayoutBinding, 3> bindings{
                VkDescriptorSetLayoutBinding{ Shaders::SHARE_PROBE_BIND_VALUES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                VkDescriptorSetLayoutBinding{ Shaders::SHARE_PROBE_BIND_TERMS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                VkDescriptorSetLayoutBinding{ Shaders::SHARE_PROBE_BIND_SUMS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            };
            const ComputePipeline<Shaders::ShareProbeConstants> pipeline(
                device, bindings, {}, "share.comp.spv", "share probe");

            constexpr std::array<float, 10> values{ 0.0f, 1.0f, 0.5f, 3.25f, 4095.0f, 4096.0f, 1.0e9f, -1.0f,
                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
            constexpr std::uint32_t sMost = 4294967040u;
            constexpr std::uint32_t sTop = 4294967295u;
            constexpr std::array<std::uint32_t, 10> terms{ 0u, 1048576u, 524288u, 3407872u, 4293918720u, sMost, sMost,
                0u, 0u, sMost };
            constexpr std::array<std::uint32_t, 10> sums{ 0u, 2097152u, 1048576u, 6815744u, sTop, sTop, sTop, 0u, 0u,
                sTop };

            const auto count = static_cast<std::uint32_t>(values.size());
            Buffer in = Buffer::hostWritten(
                device, count * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "share probe values");
            in.write(std::span<const float>(values));
            const Buffer termsOut = Buffer::readBack(
                device, count * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "share probe terms");
            const Buffer sumsOut = Buffer::readBack(
                device, count * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "share probe sums");

            DescriptorWrites writes(pipeline);
            writes.buffer(Shaders::SHARE_PROBE_BIND_VALUES, VkDescriptorBufferInfo{ in.getHandle(), 0, VK_WHOLE_SIZE });
            writes.buffer(
                Shaders::SHARE_PROBE_BIND_TERMS, VkDescriptorBufferInfo{ termsOut.getHandle(), 0, VK_WHOLE_SIZE });
            writes.buffer(
                Shaders::SHARE_PROBE_BIND_SUMS, VkDescriptorBufferInfo{ sumsOut.getHandle(), 0, VK_WHOLE_SIZE });

            getPool().submitAndWait([&](VkCommandBuffer commands) {
                dispatch(commands, pipeline, writes, Shaders::ShareProbeConstants{ .mCount = count },
                    Groups::along(count, Shaders::SHARE_PROBE_WORKGROUP));
                handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
            });

            const auto* termsRead = static_cast<const std::uint32_t*>(termsOut.map());
            const auto* sumsRead = static_cast<const std::uint32_t*>(sumsOut.map());
            for (std::uint32_t at = 0; at < count; ++at)
            {
                EXPECT_EQ(termsRead[at], terms[at]) << "the term of " << values[at];
                EXPECT_EQ(sumsRead[at], sums[at]) << "the term of " << values[at] << " added to itself";
            }
        }
    }
}
