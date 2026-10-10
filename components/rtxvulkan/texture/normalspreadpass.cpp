#include "normalspreadpass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>

namespace Rtx
{
    namespace
    {
        /// The map in, the means read and written, the level's spread out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::NORMALSPREAD_BINDINGS> sBindings{
            computeBinding(Shaders::NORMALSPREAD_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::NORMALSPREAD_BIND_MEANS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            computeBinding(Shaders::NORMALSPREAD_BIND_SPREAD, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };

        /// Where `level` of `spread`'s means starts, counted from the first's.
        std::uint32_t levelAt(const Image& spread, const std::uint32_t level)
        {
            std::uint32_t at = 0;
            for (std::uint32_t before = 0; before < level; ++before)
                at += spread.getWidthAt(before) * spread.getHeightAt(before);
            return at;
        }
    }

    std::uint32_t NormalSpreadPass::meansTexels(const Image& spread)
    {
        return levelAt(spread, spread.getMipLevels());
    }

    NormalSpreadPass::NormalSpreadPass(const Device& device)
        : mPipeline(device, sBindings, {}, "normalspread.comp.spv", "normal spread")
        , mAlignment(
              device.getPhysicalDevice().getProperties().mProperties2.properties.limits.minStorageBufferOffsetAlignment)
    {
    }

    void NormalSpreadPass::recordLevel(const VkCommandBuffer commands, const Image& map, const Image& spread,
        const std::uint32_t level, const Buffer& means, const std::uint32_t meansAt) const
    {
        assert(spread.getWidth() == std::max(map.getWidth() / 2, 1u)
            && spread.getHeight() == std::max(map.getHeight() / 2, 1u)
            && "a spread shaped unlike its map's second level");
        assert(level < spread.getMipLevels() && "a level past the spread");
        assert((VkDeviceSize{ meansAt } + meansTexels(spread)) * Shaders::NORMAL_SPREAD_MEAN_BYTES <= means.getSize()
            && "means past the room they are carried in");

        // The first level reads the map, so its level before is the map's own extent and no means.
        const bool first = level == 0;
        const std::uint32_t width = spread.getWidthAt(level);
        const std::uint32_t height = spread.getHeightAt(level);
        const std::uint32_t meanAt = meansAt + levelAt(spread, level);
        const std::uint32_t aboveAt = first ? meanAt : meansAt + levelAt(spread, level - 1);

        // **Bound from the level before to the end of the level written, and no further**: the two
        // are next to each other in the map's run, and a dispatch of each map of a group writes the
        // one buffer in the same level, which a binding of all of it said was a write over another
        // map's. From where a binding may start, before the level before: a texel is sixteen bytes
        // and the alignment a power of two, so what lies between is whole texels, which the indices
        // count from.
        const VkDeviceSize start = VkDeviceSize{ aboveAt } * Shaders::NORMAL_SPREAD_MEAN_BYTES;
        const VkDeviceSize bound = start / mAlignment * mAlignment;
        assert(bound % Shaders::NORMAL_SPREAD_MEAN_BYTES == 0 && "a binding of the means part way into a texel");
        const auto before = static_cast<std::uint32_t>(bound / Shaders::NORMAL_SPREAD_MEAN_BYTES);
        const VkDeviceSize end
            = (VkDeviceSize{ meanAt } + VkDeviceSize{ width } * height) * Shaders::NORMAL_SPREAD_MEAN_BYTES;

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::NORMALSPREAD_BIND_SOURCE,
            map.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        writes.buffer(Shaders::NORMALSPREAD_BIND_MEANS, means.describe(bound, end - bound));
        writes.image(Shaders::NORMALSPREAD_BIND_SPREAD, spread.describeStorage(level));

        const Shaders::NormalSpreadConstants constants{
            .mLevel = level + 1,
            .mWidth = width,
            .mHeight = height,
            .mAboveWidth = first ? map.getWidth() : spread.getWidthAt(level - 1),
            .mAboveHeight = first ? map.getHeight() : spread.getHeightAt(level - 1),
            .mAboveAt = aboveAt - before,
            .mMeanAt = meanAt - before,
        };

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(constants.mWidth, constants.mHeight, Shaders::NORMAL_SPREAD_WORKGROUP));
    }
}
