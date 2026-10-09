#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
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
#include <components/rtxvulkan/shaders/shared/shadow.h>
#include <components/rtxvulkan/shaders/shared/shadowmoments.h>
#include <components/rtxvulkan/shaders/shared/unormmean.h>
#include <components/rtxvulkan/shaders/shared/unormround.h>

namespace Rtx
{
    namespace
    {
        /// The word a shader rounds to the nearest step with, `UNORM16_NEAREST`.
        constexpr std::uint32_t sNearest = 0x7fffffu;

        /// A unorm's step as the shader multiplies it back, `UNORM16_STEP`: 2^-16 + 2^-32.
        constexpr float sStep = (1.0f + 1.0f / 65536.0f) / 65536.0f;

        /// `value` as `roundedToUnorm16` rounds it by `word`, computed exactly: a float's
        /// twenty-four-bit significand times 65535 has forty bits, which a double holds, and so do the
        /// share above the step under it and that share in 2^-24ths.
        std::uint32_t unorm16Of(float value, std::uint32_t word)
        {
            const double scaled = std::clamp(static_cast<double>(value), 0.0, 1.0) * 65535.0;
            const double whole = std::floor(scaled);
            const double share = std::floor((scaled - whole) * 0x1p24);
            return static_cast<std::uint32_t>(whole) + (share > static_cast<double>(word) ? 1u : 0u);
        }

        /// The share of a step above the unorm under `value`, in 2^-24ths: the word it rounds up past.
        std::uint32_t shareOf(float value)
        {
            const double scaled = static_cast<double>(value) * 65535.0;
            return static_cast<std::uint32_t>(std::floor((scaled - std::floor(scaled)) * 0x1p24));
        }

        /// One lane's moments after a run of bits, by `updatedShadowMoments`' rule worked in doubles
        /// with the count capped at `cap`, and the variance the last frame made.
        struct Moments
        {
            double mMean = 0.0;
            double mDeviations = 0.0;
            double mCount = 0.0;
            double mVariance = 0.0;
        };

        Moments momentsAfter(const std::vector<std::uint32_t>& bits, const std::vector<float>& damping,
            std::uint32_t lanes, std::uint32_t lane, double cap)
        {
            Moments moments;
            for (std::size_t frame = 0; frame < damping.size(); ++frame)
            {
                const double current = bits[frame * lanes + lane];
                const double samples = std::min(moments.mCount + 1.0, cap);
                const double kept = moments.mCount > cap - 1.0 ? (cap - 1.0) / moments.mCount : 1.0;
                const double mean = moments.mMean + (current - moments.mMean) / samples;
                moments.mDeviations = moments.mDeviations * kept + (current - moments.mMean) * (current - mean);
                moments.mMean = mean;
                moments.mVariance = samples > 1.0 ? std::min(moments.mDeviations / (samples - 1.0), 1.0) : 1.0;
                moments.mCount = samples * static_cast<double>(damping[frame]);
            }
            return moments;
        }

        constexpr std::array<VkDescriptorSetLayoutBinding, 6> sRoundBindings{
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_VALUES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_WORDS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_ROUNDED, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_MEANS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_VARIANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::UNORM_ROUND_BIND_NEAREST, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };

        /// What the rounding probe made of each float and word.
        struct Rounded
        {
            std::vector<std::uint32_t> mRounded;
            std::vector<float> mMeans;
            std::vector<float> mVariances;
            std::vector<float> mNearest;
        };

        struct RtxShadowWordTest : Testing::DeviceTest
        {
            Rounded round(const std::vector<float>& values, const std::vector<std::uint32_t>& words)
            {
                const Device& device = getDevice();
                const ComputePipeline<Shaders::UnormRoundConstants> pipeline(
                    device, sRoundBindings, {}, "unormround.comp.spv", "unorm round");
                const auto count = static_cast<std::uint32_t>(values.size());
                const VkDeviceSize bytes = count * sizeof(std::uint32_t);

                Buffer valuesIn
                    = Buffer::hostWritten(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round values");
                valuesIn.write(std::span<const float>(values));
                Buffer wordsIn
                    = Buffer::hostWritten(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round words");
                wordsIn.write(std::span<const std::uint32_t>(words));
                const Buffer rounded
                    = Buffer::readBack(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round rounded");
                const Buffer means
                    = Buffer::readBack(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round means");
                const Buffer variances
                    = Buffer::readBack(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round variances");
                const Buffer nearest
                    = Buffer::readBack(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "unorm round nearest");

                DescriptorWrites writes(pipeline);
                const auto bind = [&](std::uint32_t binding, const Buffer& buffer) {
                    writes.buffer(binding, VkDescriptorBufferInfo{ buffer.getHandle(), 0, VK_WHOLE_SIZE });
                };
                bind(Shaders::UNORM_ROUND_BIND_VALUES, valuesIn);
                bind(Shaders::UNORM_ROUND_BIND_WORDS, wordsIn);
                bind(Shaders::UNORM_ROUND_BIND_ROUNDED, rounded);
                bind(Shaders::UNORM_ROUND_BIND_MEANS, means);
                bind(Shaders::UNORM_ROUND_BIND_VARIANCES, variances);
                bind(Shaders::UNORM_ROUND_BIND_NEAREST, nearest);

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    dispatch(commands, pipeline, writes, Shaders::UnormRoundConstants{ .mCount = count },
                        Groups::along(count, Shaders::UNORM_ROUND_WORKGROUP));
                    handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
                });

                const auto* roundedOut = static_cast<const std::uint32_t*>(rounded.map());
                const auto* meansOut = static_cast<const float*>(means.map());
                const auto* variancesOut = static_cast<const float*>(variances.map());
                const auto* nearestOut = static_cast<const float*>(nearest.map());
                return Rounded{
                    .mRounded = std::vector<std::uint32_t>(roundedOut, roundedOut + count),
                    .mMeans = std::vector<float>(meansOut, meansOut + count),
                    .mVariances = std::vector<float>(variancesOut, variancesOut + count),
                    .mNearest = std::vector<float>(nearestOut, nearestOut + count),
                };
            }

            /// Each lane's moments after `damping.size()` frames of its `bits`, kept as they are or
            /// packed and read back each frame (`ShadowMomentsProbeConstants`).
            std::vector<float> momentsOf(const std::vector<std::uint32_t>& bits, const std::vector<float>& damping,
                std::uint32_t lanes, bool packed)
            {
                const Device& device = getDevice();
                constexpr std::array<VkDescriptorSetLayoutBinding, 3> bindings{
                    VkDescriptorSetLayoutBinding{ Shaders::SHADOW_MOMENTS_PROBE_BIND_BITS,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    VkDescriptorSetLayoutBinding{ Shaders::SHADOW_MOMENTS_PROBE_BIND_DAMPING,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    VkDescriptorSetLayoutBinding{ Shaders::SHADOW_MOMENTS_PROBE_BIND_MOMENTS,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                };
                const ComputePipeline<Shaders::ShadowMomentsProbeConstants> pipeline(
                    device, bindings, {}, "shadowmoments.comp.spv", "shadow moments");

                Buffer bitsIn = Buffer::hostWritten(device, bits.size() * sizeof(std::uint32_t),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shadow moments bits");
                bitsIn.write(std::span<const std::uint32_t>(bits));
                Buffer dampingIn = Buffer::hostWritten(device, damping.size() * sizeof(float),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shadow moments damping");
                dampingIn.write(std::span<const float>(damping));
                const Buffer out = Buffer::readBack(
                    device, lanes * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shadow moments");

                DescriptorWrites writes(pipeline);
                writes.buffer(Shaders::SHADOW_MOMENTS_PROBE_BIND_BITS,
                    VkDescriptorBufferInfo{ bitsIn.getHandle(), 0, VK_WHOLE_SIZE });
                writes.buffer(Shaders::SHADOW_MOMENTS_PROBE_BIND_DAMPING,
                    VkDescriptorBufferInfo{ dampingIn.getHandle(), 0, VK_WHOLE_SIZE });
                writes.buffer(Shaders::SHADOW_MOMENTS_PROBE_BIND_MOMENTS,
                    VkDescriptorBufferInfo{ out.getHandle(), 0, VK_WHOLE_SIZE });

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    dispatch(commands, pipeline, writes,
                        Shaders::ShadowMomentsProbeConstants{ .mLanes = lanes,
                            .mFrames = static_cast<std::uint32_t>(damping.size()),
                            .mPacked = packed ? 1u : 0u },
                        Groups::along(lanes, Shaders::SHADOW_MOMENTS_PROBE_WORKGROUP));
                    handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
                });

                const auto* values = static_cast<const float*>(out.map());
                return std::vector<float>(values, values + lanes * 4);
            }

            /// The words of `count` running means kept as a shadow field's are, each blended toward
            /// `target` keeping `kept` of itself `frames` times from nought, rounded at random or to
            /// the nearest step.
            std::vector<std::uint32_t> settle(
                float target, float kept, bool rounded, std::uint32_t frames, std::uint32_t count)
            {
                const Device& device = getDevice();
                constexpr std::array<VkDescriptorSetLayoutBinding, 1> bindings{ VkDescriptorSetLayoutBinding{
                    Shaders::UNORM_MEAN_BIND_HISTORY, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                    nullptr } };
                const ComputePipeline<Shaders::UnormMeanConstants> pipeline(
                    device, bindings, {}, "unormmean.comp.spv", "unorm mean");

                const Image history(device, count, 1, toVulkanFormat(STORAGE_R32UI),
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    "unorm mean");
                DescriptorWrites writes(pipeline);
                writes.image(Shaders::UNORM_MEAN_BIND_HISTORY, history.describeStorage());

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    constexpr VkClearColorValue nothing{ .uint32 = { 0u, 0u, 0u, 0u } };
                    history.clear(commands, Use::sUndefined, nothing, Use::sComputeReadWrite);
                    for (std::uint32_t frame = 0; frame < frames; ++frame)
                    {
                        dispatch(commands, pipeline, writes,
                            Shaders::UnormMeanConstants{ .mTarget = target,
                                .mKept = kept,
                                .mRounded = rounded ? 1u : 0u,
                                .mFrame = frame,
                                .mCount = count },
                            Groups::along(count, Shaders::UNORM_MEAN_WORKGROUP));
                        history.transition(commands, Use::sComputeReadWrite, Use::sComputeReadWrite);
                    }
                    history.transition(commands, Use::sComputeReadWrite, Use::sAnyGeneralRead);
                });

                std::vector<std::uint8_t> texels;
                history.read(VK_IMAGE_LAYOUT_GENERAL, texels);
                std::vector<std::uint32_t> words(count);
                std::memcpy(words.data(), texels.data(), count * sizeof(std::uint32_t));
                return words;
            }
        };

        /// **A shadow word's mean is rounded exactly, and read back exactly**, on every device: the
        /// shader rounds in integers on the float's bits, and reads a step back by one pinned multiply.
        ///
        /// - **Rounded by its word**, each float stands as `unorm16Of` computes it exactly: up from the
        ///   step under it where the share of a step it stands above it, in 2^-24ths, passes the word.
        ///   Each at the words either side of its own share, nought, the nearest's and the largest:
        ///   nought and one, a half, the least step, 2^-40 and the float under it, where the share
        ///   leaves the word's reach, values past either end, every float whose product with 65535
        ///   rounds up onto 1, 2, 255, 32768 or 60001, and 4096 drawn in `[0, 1]`.
        /// - **Read back**, each of the 65536 steps is the float of its index times
        ///   `2^-16 + 2^-32`, within an ulp of the step's exact value, and the ends are nought and
        ///   one: `65535 × (2^-16 + 2^-32) = 1 - 2^-32`, which rounds to one.
        /// - **Its variance is the nearest half**, `SHADOW_NO_RECEIVER` among them, exactly.
        TEST_F(RtxShadowWordTest, aShadowWordRoundsItsMeanExactlyAndReadsItBackExactly)
        {
            std::vector<float> values;
            std::vector<std::uint32_t> words;
            const auto add = [&](float value) {
                const std::uint32_t share = std::min(shareOf(std::clamp(value, 0.0f, 1.0f)), 0xffffffu);
                for (const std::uint32_t word :
                    { 0u, sNearest, 0xffffffu, share, share == 0u ? 0u : share - 1u, std::min(share + 1u, 0xffffffu) })
                {
                    values.push_back(value);
                    words.push_back(word);
                }
            };
            for (const float value : { 0.0f, 1.0f, 0.5f, 0.25f + 0x1p-20f, sStep, 0x1p-40f,
                     std::nextafter(0x1p-40f, 0.0f), 0x1p-24f, 1.5f, -0.5f, Shaders::SHADOW_NO_RECEIVER })
                add(value);
            // **And the floats whose product rounds up onto a whole step**, the largest under each of a
            // few steps: the step under them is theirs, with a share of all but a hair of a step.
            std::size_t hairs = 0;
            for (const float step : { 1.0f, 2.0f, 255.0f, 32768.0f, 60001.0f })
            {
                float under = step / 65535.0f;
                while (static_cast<double>(under) * 65535.0 >= static_cast<double>(step))
                    under = std::nextafter(under, 0.0f);
                while (under * 65535.0f == step)
                {
                    add(under);
                    ++hairs;
                    under = std::nextafter(under, 0.0f);
                }
            }
            ASSERT_EQ(hairs, 5u) << "a step lost the float whose product rounds up onto it";
            std::mt19937 draws(20261009u);
            std::uniform_real_distribution<float> inUnit(0.0f, 1.0f);
            for (int i = 0; i < 4096; ++i)
                add(inUnit(draws));

            std::uniform_int_distribution<std::uint32_t> anyWord(0u, 0xffffffu);
            while (values.size() < 65536)
            {
                values.push_back(inUnit(draws));
                words.push_back(anyWord(draws));
            }

            const Rounded rounded = round(values, words);
            ASSERT_EQ(rounded.mRounded.size(), values.size());
            for (std::size_t at = 0; at < values.size(); ++at)
            {
                EXPECT_EQ(rounded.mRounded[at], unorm16Of(values[at], words[at]))
                    << values[at] << " by the word " << words[at];
                EXPECT_EQ(rounded.mVariances[at], rounded.mNearest[at])
                    << values[at] << " as a variance came back as " << rounded.mVariances[at];
            }

            for (std::uint32_t step = 0; step < 65536; ++step)
            {
                const float mean = rounded.mMeans[step];
                EXPECT_EQ(mean, static_cast<float>(step) * sStep) << "step " << step;
                EXPECT_LE(std::abs(static_cast<double>(mean) - step / 65535.0),
                    static_cast<double>(std::nextafter(mean, 2.0f) - mean))
                    << "step " << step << " read back as " << mean;
            }
            EXPECT_EQ(rounded.mMeans[0], 0.0f);
            EXPECT_EQ(rounded.mMeans[65535], 1.0f);
        }

        /// **A running mean kept in a shadow word keeps its target where the shader rounds it at
        /// random, and stalls under it where it rounds to the nearest step.** 4096 means, each
        /// blended toward 0.4 of a step past step 32767 at the temporal pass's weight, keeping 0.95,
        /// 512 times from nought: 0.95^512 is 4 × 10^-12, so the start is long gone.
        ///
        /// - **To the nearest**, a blend moves the mean by a twentieth of what it lacks, and the
        ///   rounding drops that once it is under half a step: every mean stops where it lacks under
        ///   ten steps. Exactly where is the host's to say, through the same pinned blend
        ///   (`fma(mean, kept, target × (1 - kept))`) and the same rounding: at step 32758, 9.4 steps
        ///   and 1.4 × 10^-4 under.
        /// - **At random**, the stored value's mean is the value, so the mean of the means is the
        ///   target. Each store adds noise of half a step at most, 2^-17, so a mean's deviation stands
        ///   under `2^-17 / sqrt(1 - 0.95²)`, 2.4 × 10^-5, and the mean of 4096 under 3.8 × 10^-7:
        ///   within 2 × 10^-6 by more than five deviations.
        TEST_F(RtxShadowWordTest, aRunningMeanInAShadowWordKeepsItsTargetWhereRoundedAtRandom)
        {
            constexpr std::uint32_t count = 4096;
            constexpr std::uint32_t frames = 512;
            constexpr float kept = 0.95f;
            const float target = (32767.0f + 0.4f) / 65535.0f;

            std::uint32_t stalled = 0;
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                stalled
                    = unorm16Of(std::fma(static_cast<float>(stalled) * sStep, kept, target * (1.0f - kept)), sNearest);
            ASSERT_EQ(stalled, 32758u);

            for (const std::uint32_t word : settle(target, kept, false, frames, count))
                EXPECT_EQ(word & 0xffffu, stalled) << "a mean rounded to the nearest stalled elsewhere";

            double sum = 0.0;
            for (const std::uint32_t word : settle(target, kept, true, frames, count))
                sum += static_cast<double>(word & 0xffffu) * static_cast<double>(sStep);
            EXPECT_NEAR(sum / count, static_cast<double>(target), 2e-6) << "a mean rounded at random settled elsewhere";
        }

        /// **A field's moments are Welford's under the cap and running means past it, and their
        /// packed store moves neither.** 4096 lanes, each drawing bits at a chance of its own from
        /// 0.05 to 0.95, 400 frames, the count damped to 0.3 of itself at frame 200 as a history that
        /// stood off is (`shadowtiles.comp`).
        ///
        /// - **Kept as they are**, each lane's moments are the rule worked in doubles
        ///   (`momentsAfter`), to the float's rounding, and its count stands at the cap, 128: the
        ///   damping took it to 38.4, and the 199 frames after count it back past the cap. **Without
        ///   the cap the sum holds every frame's**: about 400 × 0.1825 a lane, the mean of `p(1 - p)`
        ///   over the chances, where the capped one settles toward 128 × 0.1825 and stands near 30
        ///   after the damping, so the lanes' sums stand 2.4 times apart.
        /// - **Packed and read back each frame**, every store rounded at random, the lanes' means of
        ///   the mean, the sum and the variance stand where the unpacked ones do: a mean's store adds
        ///   noise of half a unorm step, 7.6 × 10^-6, and the sum's half of a half's step, 2^-12 of
        ///   it, each held over about 128 frames of the running mean; averaged over 4096 lanes, the
        ///   bounds below are more than five deviations wide. Each count is 128 either way, which a
        ///   half holds exactly.
        TEST_F(RtxShadowWordTest, aFieldsMomentsAreWelfordsUnderTheCapAndRunningMeansPastIt)
        {
            constexpr std::uint32_t lanes = 4096;
            constexpr std::uint32_t frames = 400;
            ASSERT_EQ(Shaders::SHADOW_MOMENT_FRAMES, 128.0f);

            std::vector<float> damping(frames, 1.0f);
            damping[200] = 0.3f;
            std::vector<std::uint32_t> bits(static_cast<std::size_t>(lanes) * frames);
            std::mt19937 draws(20261010u);
            std::uniform_real_distribution<double> inUnit(0.0, 1.0);
            std::vector<double> chances(lanes);
            for (double& chance : chances)
                chance = 0.05 + 0.9 * inUnit(draws);
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                for (std::uint32_t lane = 0; lane < lanes; ++lane)
                    bits[static_cast<std::size_t>(frame) * lanes + lane] = inUnit(draws) < chances[lane] ? 1u : 0u;

            const std::vector<float> unpacked = momentsOf(bits, damping, lanes, false);
            const std::vector<float> packed = momentsOf(bits, damping, lanes, true);

            double cappedSum = 0.0;
            double uncappedSum = 0.0;
            double meanApart = 0.0;
            double sumApart = 0.0;
            double varianceApart = 0.0;
            for (std::uint32_t lane = 0; lane < lanes; ++lane)
            {
                const Moments expected = momentsAfter(bits, damping, lanes, lane, Shaders::SHADOW_MOMENT_FRAMES);
                const float* kept = &unpacked[lane * 4];
                EXPECT_NEAR(kept[0], expected.mMean, 1e-5) << "lane " << lane;
                EXPECT_NEAR(kept[1], expected.mDeviations, 1e-4 * std::max(1.0, expected.mDeviations))
                    << "lane " << lane;
                EXPECT_EQ(kept[2], 128.0f) << "lane " << lane;
                EXPECT_NEAR(kept[3], expected.mVariance, 1e-5) << "lane " << lane;
                EXPECT_EQ(packed[lane * 4 + 2], 128.0f) << "lane " << lane;

                cappedSum += expected.mDeviations;
                uncappedSum
                    += momentsAfter(bits, damping, lanes, lane, std::numeric_limits<double>::infinity()).mDeviations;
                meanApart += static_cast<double>(packed[lane * 4]) - static_cast<double>(kept[0]);
                sumApart += static_cast<double>(packed[lane * 4 + 1]) - static_cast<double>(kept[1]);
                varianceApart += static_cast<double>(packed[lane * 4 + 3]) - static_cast<double>(kept[3]);
            }
            EXPECT_GT(uncappedSum, 2.0 * cappedSum) << "the cap moved nothing a test can see";
            EXPECT_NEAR(meanApart / lanes, 0.0, 1e-5) << "the packed mean drifted";
            EXPECT_NEAR(sumApart / lanes, 0.0, 5e-3) << "the packed sum drifted";
            EXPECT_NEAR(varianceApart / lanes, 0.0, 1e-4) << "the packed variance drifted";
        }
    }
}
