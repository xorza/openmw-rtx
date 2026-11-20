#include "texturecost.hpp"

#include <algorithm>
#include <array>
#include <cassert>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/shaders/shared/bc7.h>

#include "blockchain.hpp"

namespace Rtx
{
    namespace
    {
        /// The texels of a chain from `width` by `height` to one texel.
        VkDeviceSize chainTexels(const std::uint32_t width, const std::uint32_t height)
        {
            VkDeviceSize texels = 0;
            for (std::uint32_t level = 0; level < levelsTo1x1(width, height); ++level)
                texels += VkDeviceSize{ std::max(width >> level, 1u) } * std::max(height >> level, 1u);
            return texels;
        }
    }

    TextureCost priceCanvas()
    {
        return TextureCost{ .mImage = chainTexels(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT)
                * texelBytes(TEXTURE_WRITTEN_FORMAT) };
    }

    TextureCost priceBlocks()
    {
        return TextureCost{ .mImage = BlockChain::of(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT,
                                levelsTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT),
                                Shaders::BC7_BLOCK_BYTES)
                                .mBytes };
    }

    TextureCost priceColour()
    {
        return TextureCost{ .mImage = sizeof(std::array<float, 4>) };
    }
}
