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

#include "bc7encodepass.hpp"

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

        /// The texels of a spread of a file begun at level `first`: half the side it stands at, to
        /// one texel.
        VkDeviceSize spreadTexels(const TextureData& texture, const std::uint32_t first)
        {
            const MipLevel& top = texture.mLevels[first];
            return texelsOf(chainTo1x1(std::max(top.mWidth / 2, 1u), std::max(top.mHeight / 2, 1u)));
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

        // A completed chain is the device's, made from the file's one level uploaded as it is; one
        // past the loose side stands as BC7, encoded off a loose chain and its blocks that go with
        // the batch as the upload does.
        if (texture.mCompleteChain)
        {
            const ImageShape shape = shapeOf(texture, first);
            cost.mTransient = texture.mBytes.size();
            if (!texture.encodesChain())
                cost.mImage = writtenBytes(shape);
            else
            {
                const VkDeviceSize blocks = Bc7Chain::of(shape.mWidth, shape.mHeight, shape.mLevels).mBytes;
                cost.mImage = blocks;
                cost.mTransient += writtenBytes(shape) + blocks;
            }
        }
        else
            cost.mImage = texture.bytesFrom(first);

        cost.mCompanion = texture.getCompanion() == TextureCompanion::Spread
            ? spreadTexels(texture, first) * texelBytes(NORMAL_SPREAD_FORMAT)
            : sShadingBytes;

        return cost;
    }

    VkDeviceSize spreadMeansBytes(const TextureData& texture, const std::uint32_t first)
    {
        return texture.getCompanion() == TextureCompanion::Spread
            ? spreadTexels(texture, first) * Shaders::NORMAL_SPREAD_MEAN_BYTES
            : 0;
    }

    TextureCost priceBake(const ImageShape& source)
    {
        return TextureCost{ .mImage = writtenBytes(source), .mCompanion = sShadingBytes };
    }

    TextureCost priceComposite()
    {
        return TextureCost{
            .mImage = Bc7Chain::of(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT,
                levelsTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT))
                          .mBytes,
            .mCompanion = sShadingBytes,
        };
    }

    TextureCost priceCanvas()
    {
        return TextureCost{ .mImage
            = writtenBytes(chainTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT)) };
    }

    TextureCost priceBlocks()
    {
        return TextureCost{ .mImage = Bc7Chain::of(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT,
                                levelsTo1x1(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT))
                                          .mBytes };
    }

    TextureCost priceColour()
    {
        return TextureCost{ .mImage = sizeof(std::array<float, 4>), .mCompanion = sShadingBytes };
    }
}
