#pragma once

#include "bc7encodepass.hpp"
#include "mipchainpass.hpp"
#include "normalspreadpass.hpp"
#include "rgtcencodepass.hpp"
#include "shadingpass.hpp"
#include "spritelightpass.hpp"

namespace Rtx
{
    class Device;

    /// The dispatches a texture is made with as it arrives, which the renderer owns once and every
    /// array is handed together: the chain a file did not carry, the light painted into it, a normal
    /// map's spread, a sprite's own light bake, and the blocks a large completed chain is kept in:
    /// BC7, or BC4 and BC5 for a file of one channel or two.
    struct TexturePasses
    {
        explicit TexturePasses(const Device& device)
            : mChain(device)
            , mBc7Encode(device)
            , mRgtcEncode(device)
            , mShading(device)
            , mSpread(device)
            , mBake(device)
        {
        }

        MipChainPass mChain;
        Bc7EncodePass mBc7Encode;
        RgtcEncodePass mRgtcEncode;
        ShadingPass mShading;
        NormalSpreadPass mSpread;
        SpriteLightPass mBake;
    };
}
