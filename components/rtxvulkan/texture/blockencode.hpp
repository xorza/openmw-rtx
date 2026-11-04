#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/shaders/shared/blockencode.h>

#include "blockchain.hpp"

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// What every block encoder binds: every level of the source in, the chain's blocks out.
    extern const std::array<VkDescriptorSetLayoutBinding, Shaders::BLOCK_ENCODE_BINDINGS> sBlockEncodeBindings;

    /// The source's levels as `sBlockEncodeBindings`' array of storage views: the array past the
    /// chain's last level repeats it, a view no lane reads but that every element of the binding
    /// must have.
    std::array<VkDescriptorImageInfo, Shaders::BLOCK_ENCODE_MOST_LEVELS> describeChainLevels(
        const Image& source, const BlockChain& chain);

    /// The most workgroups a dispatch launches along its first axis: `Groups::linear`'s rows.
    std::uint32_t mostGroupsAcross(const Device& device);

    /// After an encoder's dispatch: its blocks copied into `target`, met undefined, which is left as
    /// a texture the trace samples.
    void copyBlocks(VkCommandBuffer commands, const Buffer& blocks, const Image& target, const BlockChain& chain);
}
