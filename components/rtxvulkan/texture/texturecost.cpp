#include "texturecost.hpp"

#include <algorithm>
#include <array>
#include <cassert>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/shaders/shared/ground.h>
#include <components/rtxvulkan/shaders/shared/normalspread.h>

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

        /// What the device writes a texture it makes as: a bake, a composite, a completed chain.
        VkDeviceSize writtenBytes(const ImageShape& shape)
        {
            return texelsOf(shape) * texelBytes(TEXTURE_WRITTEN_FORMAT);
        }

        constexpr VkDeviceSize sShadingBytes
            = VkDeviceSize{ Shaders::SHADING_EXTENT } * Shaders::SHADING_EXTENT * texelBytes(SHADING_MAP_FORMAT);
    }

    ImageShape shapeOf(const TextureData& texture, const std::uint32_t first)
    {
        assert(first < texture.mLevels.size() && "a texture begun past the file's last level");

        if (texture.mCompleteChain)
            return chainTo1x1(texture.mWidth, texture.mHeight);

        const MipLevel& top = texture.mLevels[first];
        return ImageShape{
            .mWidth = top.mWidth,
            .mHeight = top.mHeight,
            .mLevels = static_cast<std::uint32_t>(texture.mLevels.size()) - first,
        };
    }

    TextureCost priceFile(const TextureData& texture, const std::uint32_t first)
    {
        TextureCost cost;

        // A completed chain is the device's, made from the file's one level uploaded as it is.
        if (texture.mCompleteChain)
        {
            cost.mImage = writtenBytes(shapeOf(texture, first));
            cost.mTransient = texture.mBytes.size();
        }
        else
            cost.mImage = texture.bytesFrom(first);

        if (texture.getCompanion() == TextureCompanion::Spread)
        {
            // Half the side the texture stands at, to one texel, and its means beside it.
            const MipLevel& top = texture.mLevels[first];
            const VkDeviceSize texels
                = texelsOf(chainTo1x1(std::max(top.mWidth / 2, 1u), std::max(top.mHeight / 2, 1u)));
            cost.mCompanion = texels * texelBytes(NORMAL_SPREAD_FORMAT);
            cost.mTransient += texels * texelBytes(NORMAL_SPREAD_MEAN_FORMAT);
        }
        else
            cost.mCompanion = sShadingBytes;

        return cost;
    }

    TextureCost priceBake(const ImageShape& source)
    {
        return TextureCost{ .mImage = writtenBytes(source), .mCompanion = sShadingBytes };
    }

    TextureCost priceComposite()
    {
        return TextureCost{
            .mImage = writtenBytes(chainTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT)),
            .mCompanion = sShadingBytes,
        };
    }

    TextureCost priceColour()
    {
        return TextureCost{ .mImage = sizeof(std::array<float, 4>), .mCompanion = sShadingBytes };
    }
}
