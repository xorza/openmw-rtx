#pragma once

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/texture/groundcompositepass.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturepasses.hpp>

#include "skinpass.hpp"

namespace Rtx
{
    class Device;

    /// What every scene is built and placed with, whichever it is, the doll's and the maps'
    /// included: the layout every texture array is shaped by, so one pass samples any scene's set,
    /// and the passes that pose its bodies, make its textures as they arrive and flatten its
    /// ground. What differs per scene is the tables, which each `DeviceScene` holds. The renderer
    /// keeps one, and every scene holds it.
    struct ScenePasses
    {
        explicit ScenePasses(const Device& device)
            : mTextureLayout(TextureArray::describeLayout(device))
            , mSkin(device)
            , mTextures(device)
            , mGround(device, mTextureLayout.get())
        {
        }

        SetLayout mTextureLayout;
        SkinPass mSkin;
        TexturePasses mTextures;
        GroundCompositePass mGround;
    };
}
