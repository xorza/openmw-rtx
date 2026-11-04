#include "blockencode.hpp"

#include <algorithm>

#include <volk.h>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    const std::array<VkDescriptorSetLayoutBinding, Shaders::BLOCK_ENCODE_BINDINGS> sBlockEncodeBindings{
        VkDescriptorSetLayoutBinding{ Shaders::BLOCK_ENCODE_BIND_SOURCE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            Shaders::BLOCK_ENCODE_MOST_LEVELS, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        computeBinding(Shaders::BLOCK_ENCODE_BIND_BLOCKS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    };

    std::array<VkDescriptorImageInfo, Shaders::BLOCK_ENCODE_MOST_LEVELS> describeChainLevels(
        const Image& source, const BlockChain& chain)
    {
        std::array<VkDescriptorImageInfo, Shaders::BLOCK_ENCODE_MOST_LEVELS> levels{};
        for (std::uint32_t at = 0; at < levels.size(); ++at)
            levels[at] = source.describeStorage(std::min(at, chain.mCount - 1));
        return levels;
    }

    std::uint32_t mostGroupsAcross(const Device& device)
    {
        return device.getPhysicalDevice().getProperties().mProperties2.properties.limits.maxComputeWorkGroupCount[0];
    }

    void copyBlocks(const VkCommandBuffer commands, const Buffer& blocks, const Image& target, const BlockChain& chain)
    {
        handOver(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
        target.transition(commands, Use::sUndefined, Use::sCopyWrite);

        std::array<VkBufferImageCopy, Shaders::BLOCK_ENCODE_MOST_LEVELS> regions{};
        for (std::uint32_t at = 0; at < chain.mCount; ++at)
            regions[at] = wholeLevel(
                chain.mLevels[at].mOffset, at, VkExtent3D{ chain.mLevels[at].mWidth, chain.mLevels[at].mHeight, 1 });
        vkCmdCopyBufferToImage(commands, blocks.getHandle(), target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            chain.mCount, regions.data());

        target.transition(commands, Use::sCopyWrite, Use::sTextureSample);
    }
}
