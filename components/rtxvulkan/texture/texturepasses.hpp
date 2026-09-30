#pragma once

#include "mipchainpass.hpp"
#include "normalspreadpass.hpp"
#include "shadingpass.hpp"
#include "spritelightpass.hpp"

namespace Rtx
{
    class Device;

    /// The dispatches a texture is made with as it arrives, which the renderer owns once and every
    /// array is handed together: the chain a file did not carry, the light painted into it, a normal
    /// map's spread, and a sprite's own light bake.
    struct TexturePasses
    {
        explicit TexturePasses(const Device& device)
            : mChain(device)
            , mShading(device)
            , mSpread(device)
            , mBake(device)
        {
        }

        MipChainPass mChain;
        ShadingPass mShading;
        NormalSpreadPass mSpread;
        SpriteLightPass mBake;
    };
}
