#include "bc7encodepass.hpp"

#include <cassert>

#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

#include "blockencode.hpp"

namespace Rtx
{
    Bc7EncodePass::Bc7EncodePass(const Device& device)
        : mPipeline(device, sBlockEncodeBindings, {}, "bc7encode.comp.spv", "bc7 encode")
        , mMostGroupsAcross(mostGroupsAcross(device))
    {
    }

    void Bc7EncodePass::record(const VkCommandBuffer commands, const Image& source, const Buffer& blocks,
        const Image& target, const bool weighsAlpha) const
    {
        assert(target.getWidth() == source.getWidth() && target.getHeight() == source.getHeight()
            && target.getMipLevels() == source.getMipLevels() && "a target shaped unlike its source");
        assert((target.getFormat() == VK_FORMAT_BC7_SRGB_BLOCK || target.getFormat() == VK_FORMAT_BC7_UNORM_BLOCK)
            && "a target that is not BC7");

        const BlockChain chain
            = BlockChain::of(source.getWidth(), source.getHeight(), source.getMipLevels(), Shaders::BC7_BLOCK_BYTES);
        assert(chain.mCount <= Shaders::BLOCK_ENCODE_MOST_LEVELS && "a chain past the levels one dispatch binds");
        assert(blocks.getSize() >= chain.mBytes && "a buffer too small for the chain's blocks");

        DescriptorWrites writes(mPipeline);
        writes.images(Shaders::BLOCK_ENCODE_BIND_SOURCE, describeChainLevels(source, chain));
        writes.buffer(Shaders::BLOCK_ENCODE_BIND_BLOCKS, blocks.describe(0, chain.mBytes));
        dispatch(commands, mPipeline, writes,
            Shaders::Bc7Constants{ .mWidth = source.getWidth(),
                .mHeight = source.getHeight(),
                .mLevels = chain.mCount,
                .mWeighsAlpha = weighsAlpha ? 1u : 0u },
            Groups::linear(chain.groups(), mMostGroupsAcross));

        copyBlocks(commands, blocks, target, chain);
    }
}
