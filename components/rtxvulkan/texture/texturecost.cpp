#include "texturecost.hpp"

#include <algorithm>
#include <array>
#include <cassert>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/shaders/shared/bc7.h>
#include <components/rtxvulkan/shaders/shared/normalspread.h>

#include "blockchain.hpp"

namespace Rtx
{
    namespace
    {
        /// The texels of a chain of `shape`, every level halving to one texel at the least.
        VkDeviceSize texelsOf(const ImageShape& shape)
        {
            VkDeviceSize texels = 0;
            for (std::uint32_t level = 0; level < shape.mLevels; ++level)
                texels += VkDeviceSize{ std::max(shape.mWidth >> level, 1u) } * std::max(shape.mHeight >> level, 1u);
            return texels;
        }

        /// A chain to one texel from `width` by `height`.
        ImageShape chainTo1x1(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageShape{ .mWidth = width, .mHeight = height, .mLevels = levelsTo1x1(width, height) };
        }

        /// What the device writes a texture it makes as: a bake or a composite's canvas.
        VkDeviceSize writtenBytes(const ImageShape& shape)
        {
            return texelsOf(shape) * texelBytes(TEXTURE_WRITTEN_FORMAT);
        }

        /// The texels of a spread of an image `width` by `height`: half its side, to one texel.
        VkDeviceSize spreadTexels(const std::uint32_t width, const std::uint32_t height)
        {
            return texelsOf(chainTo1x1(std::max(width / 2, 1u), std::max(height / 2, 1u)));
        }

        /// The same of a file begun at level `first`.
        VkDeviceSize spreadTexels(const TextureData& texture, const std::uint32_t first)
        {
            const MipLevel& top = texture.mLevels[first];
            return spreadTexels(top.mWidth, top.mHeight);
        }
    }

    ImageShape shapeOf(const TextureData& texture, const std::uint32_t first)
    {
        assert(first < texture.mLevels.size() && "a texture begun past the file's last level");

        const MipLevel& top = texture.mLevels[first];
        return ImageShape{
            .mWidth = top.mWidth,
            .mHeight = top.mHeight,
            .mLevels = static_cast<std::uint32_t>(texture.mLevels.size()) - first,
        };
    }

    TextureCost priceFile(const TextureData& texture, const std::uint32_t first)
    {
        return priceShape(texture.mFormat, shapeOf(texture, first), texture.getCompanion() == TextureCompanion::Spread);
    }

    TextureCost priceShape(const TextureFormat format, const ImageShape& shape, const bool spread)
    {
        const TexelLayout layout = layoutOf(format);
        VkDeviceSize image = 0;
        for (std::uint32_t level = 0; level < shape.mLevels; ++level)
            image += layout.levelBytes(std::max(shape.mWidth >> level, 1u), std::max(shape.mHeight >> level, 1u));
        return TextureCost{
            .mImage = image,
            .mCompanion = spread ? spreadTexels(shape.mWidth, shape.mHeight) * texelBytes(NORMAL_SPREAD_FORMAT) : 0,
        };
    }

    VkDeviceSize spreadMeansBytes(const TextureData& texture, const std::uint32_t first)
    {
        return texture.getCompanion() == TextureCompanion::Spread
            ? spreadTexels(texture, first) * Shaders::NORMAL_SPREAD_MEAN_BYTES
            : 0;
    }

    TextureCost priceBake(const ImageShape& source)
    {
        return TextureCost{ .mImage = writtenBytes(source) };
    }

    TextureCost priceComposite()
    {
        return TextureCost{
            .mImage = BlockChain::of(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT,
                levelsTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT),
                Shaders::BC7_BLOCK_BYTES)
                .mBytes,
        };
    }

    TextureCost priceCanvas()
    {
        return TextureCost{ .mImage
            = writtenBytes(chainTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT)) };
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
