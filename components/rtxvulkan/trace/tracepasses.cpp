#include <components/rtxvulkan/frame/gbuffer.hpp>

#include "tracepasses.hpp"

#include <cassert>

#include "fogvolume.hpp"

namespace Rtx
{
    TracePasses::TracePasses(const Device& device, const SetLayout& textureLayout, const bool counting,
        const bool specialize, const UpscaleMasks masks)
        : mChannels(GBuffer::describeLayout(device))
        , mFog(FogVolume::describeLayout(device))
        , mVisibility(device, textureLayout, mChannels, mFog, counting, specialize, masks)
        , mComposite(device, masks)
        , mSpriteBin(device)
        , mSpriteShade(device)
        , mDenoise(device, masks)
    {
    }
}
