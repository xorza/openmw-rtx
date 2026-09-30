#include "mipchainpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/mipchain.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
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

    void MipChainPass::record(
        const VkCommandBuffer commands, const Image& source, const Image& chain, const bool encoded) const
    {
        assert(source.getMipLevels() == 1 && "a chain is built for a file that carried none");
        assert(chain.getWidth() == source.getWidth() && chain.getHeight() == source.getHeight()
            && "a chain shaped unlike its source");

        // Read and written, from the first dispatch on: every dispatch binds the level above the one
        // it writes, the first bound to the level it writes and reading it not at all — which the
        // layers cannot see, so the transition makes it readable too rather than leaving a hazard
        // on the record.
        chain.transition(commands, Use::sUndefined, Use::sComputeReadWrite);

        for (std::uint32_t level = 0; level < chain.getMipLevels(); ++level)
        {
            // Every level but the first reads the one written just before it: one barrier between
            // two dispatches over one image, and the image stays where a dispatch reads and writes.
            if (level > 0)
                chain.transition(commands, Use::sComputeReadWrite, Use::sComputeReadWrite);

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

        chain.transition(commands, Use::sComputeReadWrite, Use::sTextureSample);
    }
}
