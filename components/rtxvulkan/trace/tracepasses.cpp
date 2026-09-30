#include "tracepasses.hpp"

#include "fogvolume.hpp"
#include "gbuffer.hpp"

namespace Rtx
{
    TracePasses::TracePasses(
        const Device& device, const SetLayout& textureLayout, const bool counting, const bool specialize)
        : mChannels(GBuffer::describeLayout(device))
        , mFog(FogVolume::describeLayout(device))
        , mVisibility(device, textureLayout, mChannels, mFog, counting, specialize)
        , mComposite(device)
        , mSpriteBin(device)
        , mSpriteShade(device)
        , mDenoise(device)
    {
    }
}
