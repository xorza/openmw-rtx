#include "rgtcencodepass.hpp"

#include <cassert>

#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

#include <components/rtx/image/textureformat.hpp>

#include "blockencode.hpp"

namespace Rtx
{
    namespace
    {
        static_assert(traitsOf(TextureFormat::Bc4Unorm).mLayout.mBytes == Shaders::RGTC_CHANNEL_BYTES,
            "a BC4 block is one channel's");
        static_assert(traitsOf(TextureFormat::Bc5Unorm).mLayout.mBytes == 2 * Shaders::RGTC_CHANNEL_BYTES,
            "a BC5 block is two channels'");

        /// How many channels a target of `format` holds, a block each: one for BC4 and two for BC5.
        std::uint32_t channelsOf(const VkFormat format)
        {
            assert((format == VK_FORMAT_BC4_UNORM_BLOCK || format == VK_FORMAT_BC5_UNORM_BLOCK)
                && "a format that is not BC4 or BC5");
            return format == VK_FORMAT_BC5_UNORM_BLOCK ? 2 : 1;
        }
    }

    RgtcEncodePass::RgtcEncodePass(const Device& device)
        : mPipeline(device, sBlockEncodeBindings, {}, "rgtcencode.comp.spv", "rgtc encode")
        , mMostGroupsAcross(mostGroupsAcross(device))
    {
    }

    void RgtcEncodePass::record(
        const VkCommandBuffer commands, const Image& source, const Buffer& blocks, const Image& target) const
    {
        assert(target.getWidth() == source.getWidth() && target.getHeight() == source.getHeight()
            && target.getMipLevels() == source.getMipLevels() && "a target shaped unlike its source");

        const std::uint32_t channels = channelsOf(target.getFormat());
        const BlockChain chain = BlockChain::of(
            source.getWidth(), source.getHeight(), source.getMipLevels(), channels * Shaders::RGTC_CHANNEL_BYTES);
        assert(chain.mCount <= Shaders::BLOCK_ENCODE_MOST_LEVELS && "a chain past the levels one dispatch binds");
        assert(blocks.getSize() >= chain.mBytes && "a buffer too small for the chain's blocks");

        DescriptorWrites writes(mPipeline);
        writes.images(Shaders::BLOCK_ENCODE_BIND_SOURCE, describeChainLevels(source, chain));
        writes.buffer(Shaders::BLOCK_ENCODE_BIND_BLOCKS, blocks.describe(0, chain.mBytes));
        dispatch(commands, mPipeline, writes,
            Shaders::RgtcConstants{ .mWidth = source.getWidth(),
                .mHeight = source.getHeight(),
                .mLevels = chain.mCount,
                .mChannels = channels },
            Groups::linear(chain.groups(), mMostGroupsAcross));

        copyBlocks(commands, blocks, target, chain);
    }
}
