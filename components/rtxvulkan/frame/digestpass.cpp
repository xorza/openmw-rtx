#include "digestpass.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
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

        static_assert(
            Shaders::DIGEST_SLOTS <= DescriptorWrites::sMostImages, "a run of more images than a push carries");

        Misc::DigestWords wordsAt(const std::uint32_t* words, const std::size_t image)
        {
            const std::uint32_t* const lane = words + image * Shaders::DIGEST_LANES;
            return Misc::DigestWords{ lane[0] | (std::uint64_t{ lane[1] } << 32),
                lane[2] | (std::uint64_t{ lane[3] } << 32) };
        }
    }

    std::size_t DigestPass::lanesOf(const std::size_t image)
    {
        const DigestImages::Slot slot = DigestImages::slotOf(image);
        return slot.mWords ? DigestImages::sFloats + slot.mAt : slot.mAt;
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

    void DigestPass::record(const VkCommandBuffer commands, const DigestImages& images, const std::uint32_t frame,
        const Buffer& into, GpuTimer* const timer) const
    {
        assert(into.getSize() >= sBytes && "a digest of more images than the frame has room for");

        // The denoiser's images were written by its passes, which the frame's own barriers order
        // for whatever composes the frame and not for this.
        if (images.getDenoiserTaken() != 0)
            handOver(commands,
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT },
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT });

        const GpuZone timed(timer, commands, FrameZone::Digest);

        // Cleared on the queue; the last frame's copy out of it is behind the head barrier
        // `CommandPool::begin` recorded.
        mLanes.clear(commands);
        mLanes.transition(commands, Use::sBufferClearWrite, Use::sBufferComputeReadWrite);

        // Each run lands in lanes of its own and the folds commute, so nothing orders one dispatch against
        // the next.
        const VkExtent2D extent = images.getExtent();
        for (std::uint32_t run = 0; run < DigestImages::sRuns; ++run)
        {
            const DigestImages::Run bound = images.run(run, mNoFloats, mNoWords);
            const Shaders::DigestConstants constants{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFloatFirst = run * Shaders::DIGEST_FLOAT_SLOTS,
                .mFloats = bound.mFloats,
                .mFloatsInFields = bound.mFloatsInFields,
                .mFrame = frame,
                .mWordFirst = DigestImages::sFloats + run * Shaders::DIGEST_WORD_SLOTS,
                .mWords = bound.mWords };

            DescriptorWrites writes(mPipeline);
            writes.images(Shaders::DIGEST_BIND_IMAGES, bound.mFloatViews);
            writes.images(Shaders::DIGEST_BIND_WORDS, bound.mWordViews);
            writes.buffer(Shaders::DIGEST_BIND_LANES, mLanes.describe());
            dispatch(commands, mPipeline, writes, constants,
                Groups::covering(extent.width, extent.height, Shaders::DIGEST_WORKGROUP));
        }

        // The host's read of what `into` held before is behind the submit that carries this — a
        // host read finished before the queue took the commands needs no dependency of its own.
        mLanes.transition(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
        mLanes.copyTo(commands, into, sBytes);

        into.orderForHostRead(commands);
    }

    void DigestPass::unpack(const Buffer& lanes, FrameDigest& into)
    {
        const auto* const words = static_cast<const std::uint32_t*>(lanes.map());
        for (std::size_t image = 0; image < into.mImages.size(); ++image)
            into.mImages[image] = wordsAt(words, image);

        for (std::size_t image = 0; image < into.mDenoiser.size(); ++image)
            into.mDenoiser[image] = (into.mDenoiserTaken & (1u << image)) != 0
                ? wordsAt(words, lanesOf(Shaders::DIGEST_IMAGES + image))
                : Misc::DigestWords{};
    }
}
