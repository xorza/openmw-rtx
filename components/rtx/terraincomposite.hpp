#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec4f>

#include "texturedata.hpp"

namespace Rtx
{
    /// One layer of the stack a chunk's ground is drawn from, as a bake needs it.
    ///
    /// The same four facts `MaterialLayer` carries, with the images themselves in place of the slots
    /// they were put in: a bake reads texels, and the scene's table holds indices.
    struct CompositeLayer
    {
        /// The tiling ground texture, decoded, with whatever mip chain its file carried.
        TextureData mDiffuse;

        /// The light already painted into that texture, `ShadingMap::sExtent` squared factors.
        /// Empty is neutral, which is what a texture nothing could estimate one for gets.
        std::span<const float> mShading;

        /// Chunk texture coordinates to this layer's, as `uv * xy + zw` — the shader's spelling, so
        /// the transforms the extractor read off the terrain builder come across unchanged.
        osg::Vec4f mDiffuseTransform{ 1.0f, 1.0f, 0.0f, 0.0f };

        /// The weights this layer shows through, row by row. Empty covers the chunk entirely, which
        /// is what a chunk of a single ground type gets.
        std::span<const float> mMask;
        std::uint32_t mMaskWidth = 0;
        std::uint32_t mMaskHeight = 0;
        osg::Vec4f mMaskTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
    };

    /// A chunk's whole layer stack, flattened into one texture.
    ///
    /// **The composite is the shading LOD and not only a way around a render target.** A distant
    /// chunk covers many cells and carries every ground type in them; shading it live is a mask
    /// lookup and a texture fetch per layer per hit, and distant hits are most of the pixels once
    /// there is distance to look at. This turns that into one fetch, and the near field keeps the
    /// live stack where the layer count is small and the sharpness is worth paying for.
    ///
    /// **On the CPU and in the core**, so it is written once for both backends, needs no device to
    /// test, and reaches the uploader as the same `TextureData` a file does. The GL renderer answers
    /// the same question with `Terrain::CompositeMapRenderer`, which this path has no context for.
    ///
    /// **Everything is summed in light.** Each layer's texel is decoded, has its painted light
    /// divided out, is weighted by its mask and only then re-encoded — the same order the shader
    /// reaches at a hit, and the reason a half-and-half blend comes out at 188 rather than 128.
    ///
    /// **Not on the frame path.** The cost is one trilinear fetch per layer per output texel, which
    /// for a large chunk and a full stack is millions of them; a frame that bakes one is a dropped
    /// frame however good the average is. What the unit of incremental work should be is the
    /// question the caller has to answer.
    class TerrainComposite
    {
    public:
        /// Bakes the stack at `extent` square.
        ///
        /// @param extent a power of two, so the chain below halves exactly and ends at one texel.
        /// @param delight how much of each layer's painted light to divide out, matching the frame
        ///        constant the shader reads. **Baked in rather than left to the shader**, because
        ///        the estimate repeats with the texture's tiling and the composite has none: this is
        ///        the last point at which the tiling is still known.
        TerrainComposite(std::span<const CompositeLayer> layers, std::uint32_t extent, float delight);

        /// Moved and never copied, like every other description that hands out spans of itself: two
        /// composites holding the same texels under one key is two answers to a question with one.
        TerrainComposite(const TerrainComposite&) = delete;
        TerrainComposite& operator=(const TerrainComposite&) = delete;
        TerrainComposite(TerrainComposite&&) = default;
        TerrainComposite& operator=(TerrainComposite&&) = default;

        /// The baked image, spanning storage this object owns and carrying a neutral shading map.
        ///
        /// `mSlot` and `mName` are the caller's to fill: the scene decides where a composite goes
        /// and what key found it.
        TextureData describe() const;

        std::uint32_t getLevelCount() const { return static_cast<std::uint32_t>(mLevels.size()); }

    private:
        std::vector<std::byte> mBytes;
        std::vector<MipLevel> mLevels;
        std::uint32_t mExtent = 0;
    };
}
