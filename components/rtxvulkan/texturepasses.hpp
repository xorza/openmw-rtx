#pragma once

#include <filesystem>

#include "mipchainpass.hpp"
#include "shadingpass.hpp"
#include "spritelightpass.hpp"

namespace Rtx
{
    class Device;

    /// The three dispatches a texture is made with as it arrives, which the renderer owns once and
    /// every array is handed together: the chain a file did not carry, the light painted into it,
    /// and a sprite's own light bake.
    struct TexturePasses
    {
        TexturePasses(const Device& device, const std::filesystem::path& shaders)
            : mChain(device, shaders)
            , mShading(device, shaders)
            , mBake(device, shaders)
        {
        }

        MipChainPass mChain;
        ShadingPass mShading;
        SpriteLightPass mBake;
    };
}
