#include "spritelightpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/spritelight.h>

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

    void SpriteLightPass::recordStage(const VkCommandBuffer commands, const Image& source, const Image& bake,
        const std::uint32_t level, const std::uint32_t stage) const
    {
        assert(bake.getMipLevels() == source.getMipLevels() && "a bake shaped unlike its source");
        assert(level < bake.getMipLevels() && "a level past the bake");

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::SPRITE_LIGHT_BIND_SOURCE,
            source.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        writes.image(Shaders::SPRITE_LIGHT_BIND_BAKE, bake.describeStorage(level));

        const Shaders::SpriteLightConstants constants{
            .mLevel = level,
            .mWidth = bake.getWidthAt(level),
            .mHeight = bake.getHeightAt(level),
            .mStage = stage,
        };

        // A lane a line: the rows for the stages across, the columns for the stages down.
        const std::uint32_t lines = stage < Shaders::SPRITE_LIGHT_FROM_BELOW ? constants.mHeight : constants.mWidth;
        dispatch(commands, mPipeline, writes, constants, Groups::along(lines, Shaders::SPRITE_LIGHT_WORKGROUP));
    }
}
