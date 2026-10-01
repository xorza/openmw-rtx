#include "normalspreadpass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/normalspread.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The map in, the level before's means in, the level's means and its spread out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::NORMALSPREAD_BINDINGS> sBindings{
            computeBinding(Shaders::NORMALSPREAD_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::NORMALSPREAD_BIND_ABOVE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::NORMALSPREAD_BIND_MEAN, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::NORMALSPREAD_BIND_SPREAD, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
    }

    NormalSpreadPass::NormalSpreadPass(const Device& device)
        : mPipeline(device, sBindings, {}, "normalspread.comp.spv", "normal spread")
    {
    }

    void NormalSpreadPass::recordLevel(const VkCommandBuffer commands, const Image& map, const Image& means,
        const Image& spread, const std::uint32_t level) const
    {
        assert(spread.getWidth() == std::max(map.getWidth() / 2, 1u)
            && spread.getHeight() == std::max(map.getHeight() / 2, 1u)
            && "a spread shaped unlike its map's second level");
        assert(means.getWidth() == spread.getWidth() && means.getHeight() == spread.getHeight()
            && means.getMipLevels() == spread.getMipLevels() && "means shaped unlike the spread they are carried for");
        assert(level < spread.getMipLevels() && "a level past the spread");

        // Every dispatch binds the level before the one it writes, the first bound to its own, for
        // the reason `MipChainPass::recordLevel` gives.
        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::NORMALSPREAD_BIND_SOURCE,
            map.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        writes.image(Shaders::NORMALSPREAD_BIND_ABOVE, means.describeStorage(level > 0 ? level - 1 : 0));
        writes.image(Shaders::NORMALSPREAD_BIND_MEAN, means.describeStorage(level));
        writes.image(Shaders::NORMALSPREAD_BIND_SPREAD, spread.describeStorage(level));

        const Shaders::NormalSpreadConstants constants{
            .mLevel = level + 1,
            .mWidth = spread.getWidthAt(level),
            .mHeight = spread.getHeightAt(level),
            .mPadding = 0,
        };

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(constants.mWidth, constants.mHeight, Shaders::NORMAL_SPREAD_WORKGROUP));
    }
}
