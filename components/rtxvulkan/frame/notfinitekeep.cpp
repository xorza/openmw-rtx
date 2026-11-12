#include "notfinitekeep.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx
{
    namespace
    {
        static_assert(
            Shaders::KEEP_DENOISER_IMAGES == sDenoiserImageCount, "a denoiser image the keep has no room for");
        static_assert(Shaders::KEEP_IMAGES == DigestImages::sCount, "the keep and the digest read other images");
        static_assert(Shaders::KEEP_RADIUS == NotFiniteImage::sRadius, "a window the host reads another size of");
        static_assert(
            Shaders::DIGEST_SLOTS <= DescriptorWrites::sMostImages, "a run of more images than a push carries");

        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sGateBindings{
            computeBinding(Shaders::KEEP_BIND_KEPT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// The search's and the window's, which bind one run alike: the search reads no words.
        constexpr std::array<VkDescriptorSetLayoutBinding, 3> sRunBindings{
            VkDescriptorSetLayoutBinding{ Shaders::KEEP_BIND_IMAGES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                Shaders::DIGEST_FLOAT_SLOTS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{ Shaders::KEEP_BIND_WORDS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                Shaders::DIGEST_WORD_SLOTS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            computeBinding(Shaders::KEEP_BIND_KEPT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// What each dispatch after the gate does with the kept block: reads its size there, and reads and
        /// writes the block. Ordered after every dispatch before it, since a dispatch writes through the
        /// whole binding as far as a barrier can tell, the sizes the next reads included.
        constexpr BufferUse sIndirectDispatch{ VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
                | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };

        /// Where image `image` of the frame, as `DigestImages` counts them, stands in the frame's order,
        /// `NotFiniteFrame::mImages`': the channels but the direct one, the denoiser's in the order its stages
        /// run, and the direct channel's slot, the composed frame, last.
        constexpr std::uint32_t keptAt(const std::size_t image)
        {
            static_assert(indexOf(Channel::Direct) == 0, "the composed frame's slot is the first channel's");
            return image == 0 ? Shaders::KEEP_IMAGES - 1 : static_cast<std::uint32_t>(image - 1);
        }

        /// `keptAt` undone.
        constexpr std::size_t imageAt(const std::uint32_t kept)
        {
            return kept == Shaders::KEEP_IMAGES - 1 ? 0 : std::size_t{ kept } + 1;
        }

        static_assert(imageAt(keptAt(0)) == 0);
        static_assert(imageAt(keptAt(DigestImages::sCount - 1)) == DigestImages::sCount - 1);

        /// What `NotFiniteFrame` calls the image kept at `kept`.
        std::string_view nameAt(const std::uint32_t kept)
        {
            const std::size_t image = imageAt(kept);
            if (image == 0)
                return "composed";
            return image < Shaders::DIGEST_IMAGES
                ? channelName(static_cast<Channel>(image))
                : sDenoiserImageNames.name(static_cast<DenoiserImage>(image - Shaders::DIGEST_IMAGES));
        }

        constexpr std::uint32_t sFindOffset = offsetof(Shaders::NotFiniteKept, mFind);
        constexpr std::uint32_t sCopyOffset = offsetof(Shaders::NotFiniteKept, mCopy);
    }

    NotFiniteKeep::NotFiniteKeep(const Device& device)
        : mGate(device, sGateBindings, {}, "notfinitegate.comp.spv", "not-finite gate")
        , mFind(device, sRunBindings, {}, "notfinitefind.comp.spv", "not-finite find")
        , mKeep(device, sRunBindings, {}, "notfinitekeep.comp.spv", "not-finite keep")
        , mKept(Buffer::readBack(device, sizeof(Shaders::NotFiniteKept),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, "not-finite kept"))
        , mNoFloats(makeStandIn(device, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "keep no floats"))
        , mNoWords(makeStandIn(device, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT, "keep no words"))
    {
        *static_cast<Shaders::NotFiniteKept*>(mKept.map()) = Shaders::NotFiniteKept{};
    }

    void NotFiniteKeep::record(const VkCommandBuffer commands, const DigestImages& images, const std::uint64_t frame,
        const std::uint32_t sampleFrame) const
    {
        const VkExtent2D extent = images.getExtent();

        // The census is written by every kind of stage and the images by the passes before, which order
        // their writes for what they hand on and not for this.
        handOver(commands, Use::sBufferAnyReadWrite, Use::sBufferComputeReadWrite);

        DescriptorWrites gate(mGate);
        gate.buffer(Shaders::KEEP_BIND_KEPT, mKept.describe());
        dispatch(commands, mGate, gate,
            Shaders::NotFiniteGateConstants{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFrameLow = static_cast<std::uint32_t>(frame),
                .mFrameHigh = static_cast<std::uint32_t>(frame >> 32) },
            Groups{});

        const auto constantsOf = [&](const DigestImages::Run& bound) {
            Shaders::NotFiniteKeepConstants constants{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFloats = bound.mFloats,
                .mFloatsInFields = bound.mFloatsInFields,
                .mWords = bound.mWords,
                .mFrame = sampleFrame,
                .mKeptAt = {} };
            // A slot nothing is bound at is read nowhere; its word names an image all the same, the last.
            const auto keptOf = [](const std::size_t image) {
                return image < DigestImages::sCount ? keptAt(image) : Shaders::KEEP_IMAGES - 1;
            };
            for (std::uint32_t slot = 0; slot < Shaders::DIGEST_FLOAT_SLOTS; ++slot)
                constants.mKeptAt[slot] = keptOf(bound.mFloatImages[slot]);
            for (std::uint32_t slot = 0; slot < Shaders::DIGEST_WORD_SLOTS; ++slot)
                constants.mKeptAt[Shaders::DIGEST_FLOAT_SLOTS + slot] = keptOf(bound.mWordImages[slot]);
            return constants;
        };

        const auto dispatchRuns
            = [&](const ComputePipeline<Shaders::NotFiniteKeepConstants>& pipeline, const std::uint32_t sizedAt) {
                  for (std::uint32_t run = 0; run < DigestImages::sRuns; ++run)
                  {
                      const DigestImages::Run bound = images.run(run, mNoFloats, mNoWords);
                      handOver(commands, Use::sBufferComputeReadWrite, sIndirectDispatch);
                      DescriptorWrites writes(pipeline);
                      writes.images(Shaders::KEEP_BIND_IMAGES, bound.mFloatViews);
                      writes.images(Shaders::KEEP_BIND_WORDS, bound.mWordViews);
                      writes.buffer(Shaders::KEEP_BIND_KEPT, mKept.describe());
                      bind(commands, pipeline);
                      pushDescriptors(commands, pipeline, writes);
                      pipeline.push(commands, constantsOf(bound));
                      vkCmdDispatchIndirect(commands, mKept.getHandle(), sizedAt);
                  }
              };

        // Every image's first before any window, which stands around the first of them all.
        dispatchRuns(mFind, sFindOffset);
        dispatchRuns(mKeep, sCopyOffset);

        mKept.orderForHostRead(commands);
    }

    std::optional<NotFiniteFrame> NotFiniteKeep::take()
    {
        auto& kept = *static_cast<Shaders::NotFiniteKept*>(mKept.map());
        if (kept.mKept == 0)
            return std::nullopt;

        const auto placeOf = [](const std::uint32_t key) {
            return key == 0
                ? std::nullopt
                : std::optional(NotFiniteAt{ .mX = Shaders::censusFirstX(key), .mY = Shaders::censusFirstY(key) });
        };

        NotFiniteFrame frame{
            .mFrame = kept.mFrameLow | (std::uint64_t{ kept.mFrameHigh } << 32),
            .mWidth = kept.mWidth,
            .mHeight = kept.mHeight,
            .mCentre = placeOf(kept.mCentre),
            .mImages = {},
        };
        for (std::uint32_t at = 0; at < Shaders::KEEP_IMAGES; ++at)
        {
            if (kept.mTaken[at] == 0)
                continue;

            NotFiniteImage& read = frame.mImages.emplace_back(NotFiniteImage{
                .mName = nameAt(at),
                .mWords = DigestImages::slotOf(imageAt(at)).mWords,
                .mFirst = placeOf(kept.mFirst[at]),
                .mTexels = {},
            });
            const std::uint32_t* const texels = &kept.mTexels[at * Shaders::KEEP_TEXELS * 4];
            for (std::uint32_t texel = 0; texel < Shaders::KEEP_TEXELS; ++texel)
                for (std::uint32_t word = 0; word < 4; ++word)
                    read.mTexels[texel][word] = texels[texel * 4 + word];
        }

        // Every word, firsts included, which the search reaches by their maximum. A write the next submit
        // makes visible to the device, the frames that could read it having been waited out.
        kept = Shaders::NotFiniteKept{};
        return frame;
    }
}
