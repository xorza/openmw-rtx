#pragma once

#include "alpharange.hpp"
#include "specularlayout.hpp"
#include "texels.hpp"

namespace Rtx
{
    /// **What a file's texels say, every fact a material asks read together** (`FileFactsPass`): one walk of its
    /// finest level where it is first read, one entry in the store, and nothing asked again of its texels after.
    struct ImageFacts
    {
        /// Whether its alpha ever reaches solid, as a translucent material's own diffuse map is asked. True for an
        /// image no reader here decodes, which is the answer that changes nothing about how the surface is traced.
        bool mReachesSolid = true;

        /// What a texel is worth on average, as an additive material's diffuse map, a sprite's and the sky's sheets
        /// are asked. Nothing for an image no reader here decodes.
        MeanTexel mMean;

        /// The lowest and the highest alpha it holds, as a normal map whose alpha is a height is asked: the heights
        /// its relief spans. The whole byte range for an image no reader here decodes, which bounds whatever it holds.
        AlphaRange mAlphaRange;

        /// What its channels mean as a `_spec` map. Metal-roughness for an image no reader here decodes, which reads
        /// it as the data it would be.
        SpecularLayout mSpecularLayout = SpecularLayout::MetalRoughness;
    };
}
