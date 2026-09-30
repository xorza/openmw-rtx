#include "spritelightpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/spritelight.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The source in, one level of the bake out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SPRITE_LIGHT_BINDINGS> sBindings{
            computeBinding(Shaders::SPRITE_LIGHT_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::SPRITE_LIGHT_BIND_BAKE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
    }

    SpriteLightPass::SpriteLightPass(const Device& device)
        : mPipeline(device, sBindings, {}, "spritelight.comp.spv", "sprite light")
    {
    }

    void SpriteLightPass::record(const VkCommandBuffer commands, const Image& source, const Image& bake) const
    {
        assert(bake.getMipLevels() == source.getMipLevels() && "a bake shaped unlike its source");

        bake.transition(commands, Use::sUndefined, Use::sComputeWrite);

        for (std::uint32_t level = 0; level < bake.getMipLevels(); ++level)
        {
            DescriptorWrites writes(mPipeline);
            writes.image(Shaders::SPRITE_LIGHT_BIND_SOURCE,
                source.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            writes.image(Shaders::SPRITE_LIGHT_BIND_BAKE, bake.describeStorage(level));

            const Shaders::SpriteLightConstants constants{
                .mLevel = level,
                .mWidth = bake.getWidthAt(level),
                .mHeight = bake.getHeightAt(level),
            };

            dispatch(commands, mPipeline, writes, constants,
                Groups::covering(constants.mWidth, constants.mHeight, Shaders::SPRITE_LIGHT_WORKGROUP));
        }

        bake.transition(commands, Use::sComputeWrite, Use::sTextureSample);
    }
}
