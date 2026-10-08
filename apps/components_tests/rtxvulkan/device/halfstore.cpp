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
#include <components/rtx/common/halffloat.hpp>
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
        constexpr std::array<VkDescriptorSetLayoutBinding, 4> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_VALUES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_STORED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_PACKED, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_BYTES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };

        /// What the probe made of each float: stored into a half-float image, packed by
        /// `packHalf2x16` and widened again on the host, and stored into an eight-bit normalized
        /// image, as the byte.
        struct Converted
        {
            std::vector<float> mStored;
            std::vector<float> mPacked;
            std::vector<std::uint8_t> mBytes;
        };

        struct RtxHalfStoreTest : Testing::DeviceTest
        {
            Converted convert(std::span<const float> values)
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
                const Buffer packed = Buffer::readBack(
                    device, count * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "half store packed");
                const Image bytes(device, count, 1, toVulkanFormat(STORAGE_RGBA8),
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "half store bytes");

                DescriptorWrites writes(pipeline);
                writes.buffer(
                    Shaders::HALF_STORE_BIND_VALUES, VkDescriptorBufferInfo{ source.getHandle(), 0, VK_WHOLE_SIZE });
                writes.image(Shaders::HALF_STORE_BIND_STORED, stored.describeStorage());
                writes.buffer(
                    Shaders::HALF_STORE_BIND_PACKED, VkDescriptorBufferInfo{ packed.getHandle(), 0, VK_WHOLE_SIZE });
                writes.image(Shaders::HALF_STORE_BIND_BYTES, bytes.describeStorage());

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    stored.transition(commands, Use::sUndefined, Use::sComputeWrite);
                    bytes.transition(commands, Use::sUndefined, Use::sComputeWrite);
                    dispatch(commands, pipeline, writes, Shaders::HalfStoreConstants{ .mCount = count },
                        Groups::along(count, Shaders::HALF_STORE_WORKGROUP));
                    stored.transition(commands, Use::sComputeWrite, Use::sAnyGeneralRead);
                    bytes.transition(commands, Use::sComputeWrite, Use::sAnyGeneralRead);
                    handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
                });

                Converted converted;
                std::vector<float> texels;
                stored.readFloats(VK_IMAGE_LAYOUT_GENERAL, texels);
                std::vector<std::uint8_t> pixels;
                bytes.read(VK_IMAGE_LAYOUT_GENERAL, pixels);
                const auto* words = static_cast<const std::uint32_t*>(packed.map());
                for (std::uint32_t at = 0; at < count; ++at)
                {
                    converted.mStored.push_back(texels[at * 4]);
                    converted.mPacked.push_back(fromHalf(static_cast<std::uint16_t>(words[at] & 0xffffu)));
                    converted.mBytes.push_back(pixels[at * 4]);
                }
                return converted;
            }
        };

        /// Floats a quarter, a half and three quarters of a half's step above 1, 1024 and a
        /// quarter, either sign, where the step is 2^-10 of the value; and each one's base, the half
        /// under it in size.
        struct BetweenHalves
        {
            std::vector<float> mValues;
            std::vector<float> mBases;
            std::vector<float> mFractions;
        };

        BetweenHalves betweenHalves()
        {
            BetweenHalves between;
            for (const float base : { 1.0f, 1024.0f, 0.25f })
                for (const float fraction : { 0.25f, 0.5f, 0.75f })
                    for (const float sign : { 1.0f, -1.0f })
                    {
                        between.mValues.push_back(sign * (base + fraction * Testing::halfStepAt(base)));
                        between.mBases.push_back(sign * base);
                        between.mFractions.push_back(fraction);
                    }
            return between;
        }

        /// **A half-float store rounds toward nought on this card**, which every history kept in
        /// halves rests on: a running mean falls a little at every store rather than rounding back
        /// to where it was (`accumulate.h`, `specular.h`). Vulkan allows either mode.
        ///
        /// **Hand-placed between halves** (`betweenHalves`). Toward nought each lands on the half
        /// under it in size; to nearest, three quarters would land on the one above, and a half on
        /// the even one.
        TEST_F(RtxHalfStoreTest, aHalfStoreRoundsTowardNought)
        {
            const BetweenHalves between = betweenHalves();
            const Converted converted = convert(between.mValues);
            ASSERT_EQ(converted.mStored.size(), between.mValues.size());
            for (std::size_t at = 0; at < between.mValues.size(); ++at)
                EXPECT_EQ(converted.mStored[at], between.mBases[at])
                    << between.mValues[at] << " was stored as " << converted.mStored[at];
        }

        /// **`packHalf2x16` rounds to nearest on this card, where its image store does not**: the
        /// same floats (`betweenHalves`) packed in the arithmetic. A quarter of a step lands on the
        /// base, three quarters on the half above it in size, and a half on the even one of the two,
        /// which is the base: every base's mantissa is nought. Nothing may rest on it — the spec
        /// leaves this rounding to the device as it leaves the store's — and this says what a
        /// history that did would have met here.
        TEST_F(RtxHalfStoreTest, aPackedHalfRoundsToNearestOnThisCard)
        {
            const BetweenHalves between = betweenHalves();
            const Converted converted = convert(between.mValues);
            ASSERT_EQ(converted.mPacked.size(), between.mValues.size());
            for (std::size_t at = 0; at < between.mValues.size(); ++at)
            {
                const float base = between.mBases[at];
                const float above = base + std::copysign(Testing::halfStepAt(std::abs(base)), base);
                const float nearest = between.mFractions[at] > 0.5f ? above : base;
                EXPECT_EQ(converted.mPacked[at], nearest)
                    << between.mValues[at] << " was packed as " << converted.mPacked[at];
            }
        }

        /// **A store into an eight-bit normalized image rounds to nearest on this card**, which the
        /// channels whose steps are a byte's rest on (backdrop, lift, the upscaler's masks): Vulkan
        /// only says it should. A quarter and three quarters of a step above 1, 100 and 254 of 255
        /// land on the byte under and the byte over; toward nought both would land under. A half is
        /// left out, since the spec names no rule for a tie.
        TEST_F(RtxHalfStoreTest, aByteStoreRoundsToNearestOnThisCard)
        {
            std::vector<float> values;
            std::vector<std::uint8_t> nearest;
            for (const int step : { 1, 100, 254 })
                for (const float fraction : { 0.25f, 0.75f })
                {
                    values.push_back((static_cast<float>(step) + fraction) / 255.0f);
                    nearest.push_back(static_cast<std::uint8_t>(fraction < 0.5f ? step : step + 1));
                }

            const Converted converted = convert(values);
            ASSERT_EQ(converted.mBytes.size(), values.size());
            for (std::size_t at = 0; at < values.size(); ++at)
                EXPECT_EQ(converted.mBytes[at], nearest[at])
                    << values[at] * 255.0f << " of 255 was stored as " << static_cast<int>(converted.mBytes[at]);
        }
    }
}
