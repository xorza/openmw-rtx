#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/halfstep.hpp>
#include <components/rtx/shaders/storageformat.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/halfstore.h>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, 2> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_VALUES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_STORED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };

        struct RtxHalfStoreTest : Testing::DeviceTest
        {
            /// `values` stored into a half-float image by the probe and read back.
            std::vector<float> store(std::span<const float> values)
            {
                const Device& device = getDevice();
                const ComputePipeline<Shaders::HalfStoreConstants> pipeline(
                    device, sBindings, {}, "halfstore.comp.spv", "half store");
                const auto count = static_cast<std::uint32_t>(values.size());

                Buffer source = Buffer::hostWritten(
                    device, values.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "half store values");
                source.write(values);
                const Image stored(device, count, 1, toVulkanFormat(STORAGE_RGBA16F),
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "half store");

                DescriptorWrites writes(pipeline);
                writes.buffer(
                    Shaders::HALF_STORE_BIND_VALUES, VkDescriptorBufferInfo{ source.getHandle(), 0, VK_WHOLE_SIZE });
                writes.image(Shaders::HALF_STORE_BIND_STORED, stored.describeStorage());

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    stored.transition(commands, Use::sUndefined, Use::sComputeWrite);
                    dispatch(commands, pipeline, writes, Shaders::HalfStoreConstants{ .mCount = count },
                        Groups::along(count, Shaders::HALF_STORE_WORKGROUP));
                    stored.transition(commands, Use::sComputeWrite, Use::sAnyGeneralRead);
                });

                std::vector<float> texels;
                stored.readFloats(VK_IMAGE_LAYOUT_GENERAL, texels);
                std::vector<float> read(count);
                for (std::uint32_t at = 0; at < count; ++at)
                    read[at] = texels[at * 4];
                return read;
            }
        };

        /// **A half-float store rounds toward nought on this card**, which every history kept in
        /// halves rests on: a running mean falls a little at every store rather than rounding back
        /// to where it was (`accumulate.h`, `specular.h`). Vulkan allows either mode.
        ///
        /// **Hand-placed between halves**: a quarter, a half and three quarters of a step above
        /// one, 1024 and a quarter, either sign, where the step is 2^-10 of the value. Toward
        /// nought each lands on the half under it in size; to nearest, three quarters would land
        /// on the one above, and a half on the even one.
        TEST_F(RtxHalfStoreTest, aHalfStoreRoundsTowardNought)
        {
            std::vector<float> values;
            std::vector<float> towardNought;
            for (const float base : { 1.0f, 1024.0f, 0.25f })
                for (const float fraction : { 0.25f, 0.5f, 0.75f })
                    for (const float sign : { 1.0f, -1.0f })
                    {
                        values.push_back(sign * (base + fraction * Testing::halfStepAt(base)));
                        towardNought.push_back(sign * base);
                    }

            const std::vector<float> read = store(values);
            ASSERT_EQ(read.size(), values.size());
            for (std::size_t at = 0; at < values.size(); ++at)
                EXPECT_EQ(read[at], towardNought[at]) << values[at] << " was stored as " << read[at];
        }
    }
}
