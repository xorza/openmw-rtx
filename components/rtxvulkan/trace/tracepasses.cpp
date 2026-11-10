#include "tracepasses.hpp"

#include <cassert>

#include "fogvolume.hpp"
#include "gbuffer.hpp"

namespace Rtx
{
    TracePasses::TracePasses(const Device& device, const SetLayout& textureLayout, const bool counting,
        const bool specialize, const UpscalerKind upscaler)
        : mChannels(GBuffer::describeLayout(device))
        , mFog(FogVolume::describeLayout(device))
        , mVisibility(device, textureLayout, mChannels, mFog, counting, specialize, upscaler)
        , mComposite(device, upscaler)
        , mSpriteBin(device)
        , mSpriteShade(device)
        , mDenoise(device, upscaler)
    {
        assert(upscaler != UpscalerKind::Auto && "the trace's kernels specialized on an upscaler never resolved");
    }
}
