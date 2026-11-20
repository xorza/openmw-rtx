#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// What a texture takes of the device's room, in the parts its maker makes. The one answer
    /// both to which side an arrival is held to and to what a standing texture reports, so the two
    /// cannot disagree.
    struct TextureCost
    {
        /// The image the trace samples.
        VkDeviceSize mImage = 0;

        /// A normal map's spread, by `TextureData::getCompanion`, and nothing beside any other. The float
        /// means a spread is built through are the arrival's room, and priced once for it
        /// (`spreadMeansBytes`).
        VkDeviceSize mCompanion = 0;

        /// What the texture keeps.
        VkDeviceSize standing() const { return mImage + mCompanion; }
    };

    /// What one canvas a scene's composites of a kind are baked on costs: a chain, four bytes a texel
    /// (`GroundCompositePass::makeCanvas`). Made once, with the scene's first composite of the kind.
    TextureCost priceCanvas();

    /// What the blocks every composite of a scene is encoded through cost: one chain's
    /// (`GroundCompositePass::makeBlocks`). Made once, with the scene's first composite.
    TextureCost priceBlocks();

    /// What a texture of one colour costs: the one texel, and its map.
    TextureCost priceColour();
}
