#include "mipchainpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/mipchain.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The upload in, the level above in, the level written out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::MIPCHAIN_BINDINGS> sBindings{
            computeBinding(Shaders::MIPCHAIN_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::MIPCHAIN_BIND_ABOVE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::MIPCHAIN_BIND_INTO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
    }

    MipChainPass::MipChainPass(const Device& device)
        : mPipeline(device, sBindings, {}, "mipchain.comp.spv", "mip chain")
    {
    }

    void MipChainPass::recordLevel(const VkCommandBuffer commands, const Image& source, const Image& chain,
        const std::uint32_t level, const bool encoded) const
    {
        assert(source.getMipLevels() == 1 && "a chain is built for a file that carried none");
        assert(chain.getWidth() == source.getWidth() && chain.getHeight() == source.getHeight()
            && "a chain shaped unlike its source");
        assert(level < chain.getMipLevels() && "a level past the chain");

        // Every dispatch binds the level above the one it writes, the first bound to the level it
        // writes and reading it not at all: one layout for every level, so a chain is ordered a
        // level at a time and never transitioned between two.
        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::MIPCHAIN_BIND_SOURCE,
            source.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        writes.image(Shaders::MIPCHAIN_BIND_ABOVE, chain.describeStorage(level > 0 ? level - 1 : 0));
        writes.image(Shaders::MIPCHAIN_BIND_INTO, chain.describeStorage(level));

        const Shaders::MipChainConstants constants{
            .mLevel = level,
            .mWidth = chain.getWidthAt(level),
            .mHeight = chain.getHeightAt(level),
            .mEncoded = encoded ? 1u : 0u,
        };

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(constants.mWidth, constants.mHeight, Shaders::MIP_CHAIN_WORKGROUP));
    }
}
