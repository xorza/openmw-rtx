#include "digestpass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::DIGEST_BINDINGS> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::DIGEST_BIND_IMAGES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                Shaders::DIGEST_FLOAT_SLOTS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::DIGEST_BIND_WORDS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                Shaders::DIGEST_WORD_SLOTS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            computeBinding(Shaders::DIGEST_BIND_LANES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// Which of the denoiser's images hold words — fast means, shadow words and moments —
        /// which a float view cannot read, at each one's index. A slot's own and not read off the
        /// image, because the layout binds each kind apart; `record` holds each image to it.
        constexpr std::array<bool, sDenoiserImageCount> sHeldInWords = [] {
            std::array<bool, sDenoiserImageCount> words{};
            for (const DenoiserImage image : { DenoiserImage::AccumulateFast, DenoiserImage::SkyShadowMoments,
                     DenoiserImage::LampShadowMoments, DenoiserImage::PaneFast, DenoiserImage::SpecularFast,
                     DenoiserImage::SkyShadowLevel0, DenoiserImage::LampShadowLevel0, DenoiserImage::SkyShadowLevel1,
                     DenoiserImage::LampShadowLevel1, DenoiserImage::SkyShadowLevel2, DenoiserImage::LampShadowLevel2 })
                words[static_cast<std::size_t>(image)] = true;
            return words;
        }();

        constexpr std::size_t countHeldInWords()
        {
            std::size_t words = 0;
            for (const bool held : sHeldInWords)
                words += held ? 1 : 0;
            return words;
        }

        static_assert(
            Shaders::DIGEST_SLOTS <= DescriptorWrites::sMostImages, "a run of more images than a push carries");

        static_assert(countHeldInWords() == Shaders::DIGEST_DENOISER_WORDS
                && sDenoiserImageCount - countHeldInWords() == Shaders::DIGEST_DENOISER_FLOATS,
            "the digest's bindings and the denoiser's images count them differently");

        DigestWords wordsAt(const std::uint32_t* words, const std::size_t image)
        {
            const std::uint32_t* const lane = words + image * Shaders::DIGEST_LANES;
            return DigestWords{ lane[0] | (std::uint64_t{ lane[1] } << 32),
                lane[2] | (std::uint64_t{ lane[3] } << 32) };
        }
    }

    DigestPass::Slot DigestPass::slotOf(const DenoiserImage image)
    {
        const auto at = static_cast<std::size_t>(image);
        assert(at < sDenoiserImageCount);
        std::uint32_t before = 0;
        for (std::size_t other = 0; other < at; ++other)
            before += sHeldInWords[other] == sHeldInWords[at] ? 1u : 0u;
        return Slot{ .mWords = sHeldInWords[at], .mAt = before };
    }

    std::size_t DigestPass::lanesOf(const DenoiserImage image)
    {
        const Slot slot = slotOf(image);
        return slot.mWords ? Shaders::DIGEST_FLOAT_IMAGES + slot.mAt : Shaders::DIGEST_IMAGES + slot.mAt;
    }

    DigestPass::DigestPass(const Device& device)
        : mPipeline(device, sBindings, {}, "digest.comp.spv", "digest")
        , mLanes(Buffer::deviceLocal(device, sBytes,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              "digest lanes"))
        , mNoFloats(makeStandIn(device, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "digest no floats"))
        , mNoWords(makeStandIn(device, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT, "digest no words"))
    {
    }

    std::uint32_t DigestPass::record(const VkCommandBuffer commands,
        const std::array<const Image*, Shaders::DIGEST_IMAGES>& channels,
        const std::array<const Image*, sDenoiserImageCount>& denoiser, const Buffer& into, GpuTimer* const timer) const
    {
        assert(into.getSize() >= sBytes && "a digest of more images than the frame has room for");

        const Image& first = *channels.front();
        [[maybe_unused]] const auto sameExtent = [&](const Image& image) {
            return image.getWidth() == first.getWidth() && image.getHeight() == first.getHeight();
        };

        // Every image of the frame in the order its lanes lie: the floats, the channels first, and
        // then the words. Null where the denoiser wrote none.
        std::array<const Image*, Shaders::DIGEST_FLOAT_IMAGES> floats{};
        std::array<const Image*, Shaders::DIGEST_DENOISER_WORDS> words{};
        for (std::size_t at = 0; at < channels.size(); ++at)
        {
            assert(sameExtent(*channels[at]) && "a digest of images at two extents");
            assert(!holdsWords(channels[at]->getFormat()) && "a channel of words bound as floats");
            floats[at] = channels[at];
        }

        std::uint32_t taken = 0;
        for (std::size_t at = 0; at < denoiser.size(); ++at)
        {
            const Slot slot = slotOf(static_cast<DenoiserImage>(at));
            const Image* const image = denoiser[at];
            assert((image == nullptr || sameExtent(*image)) && "a digest of images at two extents");
            assert((image == nullptr || holdsWords(image->getFormat()) == slot.mWords)
                && "a denoiser image bound as the kind of texel it does not hold");
            (slot.mWords ? words[slot.mAt] : floats[Shaders::DIGEST_IMAGES + slot.mAt]) = image;
            taken |= image != nullptr ? 1u << at : 0u;
        }

        // The denoiser's images were written by its passes, which the frame's own barriers order
        // for whatever composes the frame and not for this.
        if (taken != 0)
            handOver(commands,
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT },
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT });

        const GpuZone timed(timer, commands, FrameZone::Digest);

        // Cleared on the queue; the last frame's copy out of it is behind the head barrier
        // `CommandPool::begin` recorded.
        mLanes.clear(commands);
        mLanes.transition(commands, Use::sBufferClearWrite, Use::sBufferComputeReadWrite);

        // A run of each kind a dispatch, as many dispatches as the longer kind needs. Each lands in lanes of
        // their own and the folds commute, so nothing orders one dispatch against the next.
        constexpr std::size_t sRuns = std::max(groupsFor(Shaders::DIGEST_FLOAT_IMAGES, Shaders::DIGEST_FLOAT_SLOTS),
            groupsFor(Shaders::DIGEST_DENOISER_WORDS, Shaders::DIGEST_WORD_SLOTS));
        for (std::uint32_t run = 0; run < sRuns; ++run)
        {
            std::array<VkDescriptorImageInfo, Shaders::DIGEST_FLOAT_SLOTS> runFloats{};
            std::array<VkDescriptorImageInfo, Shaders::DIGEST_WORD_SLOTS> runWords{};
            Shaders::DigestConstants constants{ .mWidth = first.getWidth(),
                .mHeight = first.getHeight(),
                .mFloatFirst = run * Shaders::DIGEST_FLOAT_SLOTS,
                .mFloats = 0,
                .mWordFirst = Shaders::DIGEST_FLOAT_IMAGES + run * Shaders::DIGEST_WORD_SLOTS,
                .mWords = 0 };

            for (std::uint32_t slot = 0; slot < runFloats.size(); ++slot)
            {
                const std::size_t at = constants.mFloatFirst + slot;
                const Image* const image = at < floats.size() ? floats[at] : nullptr;
                runFloats[slot] = (image != nullptr ? *image : mNoFloats).describeStorage();
                constants.mFloats |= image != nullptr ? 1u << slot : 0u;
            }
            for (std::uint32_t slot = 0; slot < runWords.size(); ++slot)
            {
                const std::size_t at = run * Shaders::DIGEST_WORD_SLOTS + slot;
                const Image* const image = at < words.size() ? words[at] : nullptr;
                runWords[slot] = (image != nullptr ? *image : mNoWords).describeStorage();
                constants.mWords |= image != nullptr ? 1u << slot : 0u;
            }

            DescriptorWrites writes(mPipeline);
            writes.images(Shaders::DIGEST_BIND_IMAGES, runFloats);
            writes.images(Shaders::DIGEST_BIND_WORDS, runWords);
            writes.buffer(Shaders::DIGEST_BIND_LANES, mLanes.describe());
            dispatch(commands, mPipeline, writes, constants,
                Groups::covering(first.getWidth(), first.getHeight(), Shaders::DIGEST_WORKGROUP));
        }

        // The host's read of what `into` held before is behind the submit that carries this — a
        // host read finished before the queue took the commands needs no dependency of its own.
        mLanes.transition(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
        mLanes.copyTo(commands, into, sBytes);

        into.orderForHostRead(commands);
        return taken;
    }

    void DigestPass::unpack(const Buffer& lanes, FrameDigest& into)
    {
        const auto* const words = static_cast<const std::uint32_t*>(lanes.map());
        for (std::size_t image = 0; image < into.mImages.size(); ++image)
            into.mImages[image] = wordsAt(words, image);

        for (std::size_t image = 0; image < into.mDenoiser.size(); ++image)
            into.mDenoiser[image] = (into.mDenoiserTaken & (1u << image)) != 0
                ? wordsAt(words, lanesOf(static_cast<DenoiserImage>(image)))
                : DigestWords{};
    }
}
