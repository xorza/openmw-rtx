#include "tracepasses.hpp"

#include "fogvolume.hpp"
#include "gbuffer.hpp"

namespace Rtx
{
    TracePasses::TracePasses(const Device& device, const std::filesystem::path& shaders, const SetLayout& textureLayout,
        const bool counting, const bool specialize, const Reorder reorder)
        : mChannels(GBuffer::describeLayout(device))
        , mFog(FogVolume::describeLayout(device))
        , mVisibility(device, shaders, textureLayout, mChannels, mFog, counting, specialize, reorder)
        , mComposite(device, shaders)
        , mSpriteBin(device, shaders)
        , mSpriteShade(device, shaders)
        , mAccumulate(device, shaders)
        , mFilter(device, shaders)
    {
    }
}
