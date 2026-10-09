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
#include <components/rtxvulkan/shaders/shared/halfmean.h>
#include <components/rtxvulkan/shaders/shared/halfstore.h>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, 5> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_VALUES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_STORED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_PACKED, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_BYTES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::HALF_STORE_BIND_NEAREST, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };

        /// What the probe made of each float: stored into a half-float image, packed by
        /// `packHalf2x16` and widened again on the host, stored into an eight-bit normalized image,
        /// as the byte, and rounded to the nearest half by the shader (`nearestHalf`).
        struct Converted
        {
            std::vector<float> mStored;
            std::vector<float> mPacked;
            std::vector<std::uint8_t> mBytes;
            std::vector<float> mNearest;
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
                const Buffer nearest = Buffer::readBack(
                    device, values.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "half store nearest");

                DescriptorWrites writes(pipeline);
                writes.buffer(
                    Shaders::HALF_STORE_BIND_VALUES, VkDescriptorBufferInfo{ source.getHandle(), 0, VK_WHOLE_SIZE });
                writes.image(Shaders::HALF_STORE_BIND_STORED, stored.describeStorage());
                writes.buffer(
                    Shaders::HALF_STORE_BIND_PACKED, VkDescriptorBufferInfo{ packed.getHandle(), 0, VK_WHOLE_SIZE });
                writes.image(Shaders::HALF_STORE_BIND_BYTES, bytes.describeStorage());
                writes.buffer(
                    Shaders::HALF_STORE_BIND_NEAREST, VkDescriptorBufferInfo{ nearest.getHandle(), 0, VK_WHOLE_SIZE });

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
                const auto* rounded = static_cast<const float*>(nearest.map());
                for (std::uint32_t at = 0; at < count; ++at)
                {
                    converted.mStored.push_back(texels[at * 4]);
                    converted.mPacked.push_back(fromHalf(static_cast<std::uint16_t>(words[at] & 0xffffu)));
                    converted.mBytes.push_back(pixels[at * 4]);
                    converted.mNearest.push_back(rounded[at]);
                }
                return converted;
            }
        };

        /// Floats a quarter, a half and three quarters of a half's step above 1, 1024, a quarter and
        /// the subnormal 2^-23, either sign, where the step is 2^-10 of the value and 2^-24 under the
        /// least normal half; and each one's base, the half under it in size. Every base's mantissa
        /// is even.
        struct BetweenHalves
        {
            std::vector<float> mValues;
            std::vector<float> mBases;
            std::vector<float> mFractions;
        };

        BetweenHalves betweenHalves()
        {
            BetweenHalves between;
            for (const float base : { 1.0f, 1024.0f, 0.25f, 0x1p-23f })
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

        /// **Every value a half holds is stored as itself, the subnormals with the rest**, which a
        /// history rounded to halves in the shader rests on (`roundedToHalf`): the spec stores a
        /// representable value exactly, but leaves a device free to flush a denormal, and a dim
        /// bounce stands under the least normal half, 2^-14. The least subnormal, 2^-24, three of
        /// it, the largest, 1023 × 2^-24, the least normal, one and the step over it, and the
        /// largest half, 65504, either sign. **And nought either sign, its sign kept**, which a shadow
        /// channel's alpha rests on: a closed ray at a radius of nought is `-0.0` (`packShadowAlpha`).
        /// **And the shader's rounding to the nearest leaves each as it is** (`nearestHalf`).
        TEST_F(RtxHalfStoreTest, everyHalfIsStoredAsItselfTheSubnormalsWithTheRest)
        {
            std::vector<float> values;
            for (const float magnitude :
                { 0.0f, 0x1p-24f, 3.0f * 0x1p-24f, 1023.0f * 0x1p-24f, 0x1p-14f, 1.0f, 1.0f + 0x1p-10f, 65504.0f })
                for (const float sign : { 1.0f, -1.0f })
                    values.push_back(sign * magnitude);

            const Converted converted = convert(values);
            ASSERT_EQ(converted.mStored.size(), values.size());
            for (std::size_t at = 0; at < values.size(); ++at)
            {
                EXPECT_EQ(converted.mStored[at], values[at])
                    << values[at] << " was stored as " << converted.mStored[at];
                EXPECT_EQ(std::signbit(converted.mStored[at]), std::signbit(values[at]))
                    << values[at] << " was stored with the other sign";
                EXPECT_EQ(converted.mNearest[at], values[at])
                    << values[at] << " was rounded by the shader to " << converted.mNearest[at];
            }
        }

        /// **`packHalf2x16` rounds to nearest on this card, where its image store does not**: the
        /// same floats (`betweenHalves`) packed in the arithmetic. A quarter of a step lands on the
        /// base, three quarters on the half above it in size, and a half on the even one of the two,
        /// which is the base. Nothing may rest on it — the spec leaves this rounding to the device
        /// as it leaves the store's — and this says what a history that did would have met here.
        ///
        /// **And the shader's own `nearestHalf` rounds each the same, on every device**, which the
        /// wavelet's narrow levels rest on: exact steps, a tie to the even one.
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
                EXPECT_EQ(converted.mNearest[at], nearest)
                    << between.mValues[at] << " was rounded by the shader to " << converted.mNearest[at];
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

        /// **A running mean kept in halves keeps its target where the shader rounds it at random,
        /// and settles under it where the store rounds it.** 4096 means, each blended toward
        /// `1 + 0.4 × 2^-10` at a history's weight of nine tenths, 256 times, from nought: a target
        /// between two halves, a fifth of the way to the one above.
        ///
        /// - **Stored as it is**, each store rounds toward nought on this card. Under one, where a
        ///   half's step is 2^-11, a blend moves the mean by a tenth of what it lacks, and the store
        ///   drops that once it is under a step: every mean stops where it lacks ten steps, about
        ///   4.9 thousandths under the target.
        /// - **Rounded at random** (`roundedToHalf`), the stored value's mean is the value, so the
        ///   mean of the means is the target. Each store adds noise of half a step at most, 2^-11, so
        ///   a mean's deviation stands under `2^-11 / sqrt(1 - 0.81)`, 1.12 thousandths, and the mean
        ///   of 4096 under 1.75 hundred-thousandths: within a ten-thousandth by more than five
        ///   deviations.
        ///
        /// Measured: stored, 0.995605, 4.79 thousandths under the target of 1.000391; rounded at
        /// random, 1.000394, three millionths over it.
        TEST_F(RtxHalfStoreTest, aRunningMeanRoundedAtRandomKeepsItsTargetWhereAStoreRoundsItUnder)
        {
            const Device& device = getDevice();
            constexpr std::uint32_t count = 4096;
            constexpr std::uint32_t frames = 256;
            const float target = 1.0f + 0.4f * Testing::halfStepAt(1.0f);

            constexpr std::array<VkDescriptorSetLayoutBinding, 1> bindings{ VkDescriptorSetLayoutBinding{
                Shaders::HALF_MEAN_BIND_HISTORY, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                nullptr } };
            const ComputePipeline<Shaders::HalfMeanConstants> pipeline(
                device, bindings, {}, "halfmean.comp.spv", "half mean");

            const auto meanOf = [&](bool rounded) {
                const Image history(device, count, 1, toVulkanFormat(STORAGE_RGBA16F),
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    "half mean");
                DescriptorWrites writes(pipeline);
                writes.image(Shaders::HALF_MEAN_BIND_HISTORY, history.describeStorage());

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    constexpr VkClearColorValue nothing{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };
                    history.clear(commands, Use::sUndefined, nothing, Use::sComputeReadWrite);
                    for (std::uint32_t frame = 0; frame < frames; ++frame)
                    {
                        dispatch(commands, pipeline, writes,
                            Shaders::HalfMeanConstants{ .mTarget = target,
                                .mKept = 0.9f,
                                .mRounded = rounded ? 1u : 0u,
                                .mFrame = frame,
                                .mCount = count },
                            Groups::along(count, Shaders::HALF_MEAN_WORKGROUP));
                        history.transition(commands, Use::sComputeReadWrite, Use::sComputeReadWrite);
                    }
                    history.transition(commands, Use::sComputeReadWrite, Use::sAnyGeneralRead);
                });

                std::vector<float> texels;
                history.readFloats(VK_IMAGE_LAYOUT_GENERAL, texels);
                double sum = 0.0;
                for (std::uint32_t at = 0; at < count; ++at)
                    sum += static_cast<double>(texels[at * 4]);
                return sum / count;
            };

            const double stored = meanOf(false);
            const double rounded = meanOf(true);
            EXPECT_LT(stored, static_cast<double>(target) - 4e-3)
                << "a mean the store rounded settled at " << stored << ", under " << target
                << " by less than this card's toward-nought store leaves";
            EXPECT_NEAR(rounded, static_cast<double>(target), 1e-4)
                << "a mean rounded at random settled at " << rounded;
        }
    }
}
