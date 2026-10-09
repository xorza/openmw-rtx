#include "bc7encodepass.hpp"

#include <algorithm>
#include <array>
#include <cassert>

#include <volk.h>

#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// Every level of the source in, the chain's blocks out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::BC7_BINDINGS> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::BC7_BIND_SOURCE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                Shaders::BC7_MOST_LEVELS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            computeBinding(Shaders::BC7_BIND_BLOCKS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };
    }

    Bc7EncodePass::Bc7EncodePass(const Device& device)
        : mPipeline(device, sBindings, {}, "bc7encode.comp.spv", "bc7 encode")
    {
    }

    void Bc7EncodePass::record(const VkCommandBuffer commands, const Image& source, const Buffer& blocks,
        const Image& target, const bool weighsAlpha) const
    {
        assert(target.getWidth() == source.getWidth() && target.getHeight() == source.getHeight()
            && target.getMipLevels() == source.getMipLevels() && "a target shaped unlike its source");
        assert((target.getFormat() == VK_FORMAT_BC7_SRGB_BLOCK || target.getFormat() == VK_FORMAT_BC7_UNORM_BLOCK)
            && "a target that is not BC7");

        const Bc7Chain chain = Bc7Chain::of(source.getWidth(), source.getHeight(), source.getMipLevels());
        assert(chain.mCount <= Shaders::BC7_MOST_LEVELS && blocks.getSize() >= chain.mBytes
            && "a buffer too small for the chain's blocks");

        // One dispatch over every level: the array past the chain's last level repeats it, a view no
        // lane reads but that every element of the binding must have.
        std::array<VkDescriptorImageInfo, Shaders::BC7_MOST_LEVELS> levels{};
        for (std::uint32_t at = 0; at < levels.size(); ++at)
            levels[at] = source.describeStorage(std::min(at, chain.mCount - 1));

        DescriptorWrites writes(mPipeline);
        writes.images(Shaders::BC7_BIND_SOURCE, levels);
        writes.buffer(Shaders::BC7_BIND_BLOCKS, blocks.describe(0, chain.mBytes));
        std::uint32_t groups = 0;
        for (std::uint32_t at = 0; at < chain.mCount; ++at)
            groups += groupsFor(chain.mLevels[at].mBlocks, Shaders::BC7_WORKGROUP);
        dispatch(commands, mPipeline, writes,
            Shaders::Bc7Constants{ .mWidth = source.getWidth(),
                .mHeight = source.getHeight(),
                .mLevels = chain.mCount,
                .mWeighsAlpha = weighsAlpha ? 1u : 0u },
            Groups{ .mX = groups, .mY = 1, .mZ = 1 });

        handOver(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
        target.transition(commands, Use::sUndefined, Use::sCopyWrite);

        std::array<VkBufferImageCopy, Shaders::BC7_MOST_LEVELS> regions{};
        for (std::uint32_t at = 0; at < chain.mCount; ++at)
            regions[at] = wholeLevel(
                chain.mLevels[at].mOffset, at, VkExtent3D{ chain.mLevels[at].mWidth, chain.mLevels[at].mHeight, 1 });
        vkCmdCopyBufferToImage(commands, blocks.getHandle(), target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            chain.mCount, regions.data());

        target.transition(commands, Use::sCopyWrite, Use::sTextureSample);
    }
}
