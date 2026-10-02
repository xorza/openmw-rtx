#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    struct TextureData;

    /// The extent and the level count of the image a texture stands as.
    struct ImageShape
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint32_t mLevels = 0;
    };

    /// What a texture takes of the device's room, in the parts its maker makes. The one answer
    /// both to which side an arrival is held to and to what a standing texture reports, so the two
    /// cannot disagree.
    struct TextureCost
    {
        /// The image the trace samples.
        VkDeviceSize mImage = 0;

        /// Its shading map, or a normal map's spread, by `TextureData::getCompanion`.
        VkDeviceSize mCompanion = 0;

        /// What the arrival holds until the texture is made: the one level a completed chain is
        /// made from, and the float means a spread is built through.
        VkDeviceSize mTransient = 0;

        /// What the texture keeps once its batch has let go of what made it.
        VkDeviceSize standing() const { return mImage + mCompanion; }

        /// What making it takes at the most.
        VkDeviceSize total() const { return standing() + mTransient; }
    };

    /// The image a file stands as, begun at level `first`: from that level down, or the chain the
    /// device completes to one texel.
    ImageShape shapeOf(const TextureData& texture, std::uint32_t first);

    /// What a file costs, begun at level `first`, as `Texture::standFile` makes it.
    TextureCost priceFile(const TextureData& texture, std::uint32_t first);

    /// What a bake costs, made at the shape its source stands as.
    TextureCost priceBake(const ImageShape& source);

    /// What a ground composite costs, whose side is the renderer's own.
    TextureCost priceComposite();

    /// What a texture of one colour costs: the one texel, and its map.
    TextureCost priceColour();
}
