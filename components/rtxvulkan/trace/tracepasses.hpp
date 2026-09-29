#pragma once

#include <filesystem>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/scene/spritepasses.hpp>
#include <components/rtxvulkan/trace/denoise/compositepass.hpp>
#include <components/rtxvulkan/trace/denoise/denoisepasses.hpp>

#include "visibilitypass.hpp"

namespace Rtx
{
    class Device;

    /// What every chain shares, whichever camera it is for: the layouts every `GBuffer` and every
    /// `FogVolume` is shaped by, and the passes every trace runs. The renderer keeps one, built
    /// once — a pipeline is a compile, and two chains that each made their own made it twice — and
    /// what differs between two chains is the extent and what becomes of the picture. Owned here,
    /// so a chain that holds this is correct whatever order its holder declares its members in.
    struct TracePasses
    {
        /// @param textureLayout what every scene's texture array is shaped by, which the trace reads.
        /// @param counting `RendererOptions::mCounting`: whether the trace counts what its rays met.
        /// @param specialize `RenderProfile::mSpecializeLaunches`.
        TracePasses(const Device& device, const std::filesystem::path& shaders, const SetLayout& textureLayout,
            bool counting, bool specialize);

        SetLayout mChannels;
        SetLayout mFog;

        /// Every kernel the trace can ever need is compiled here, on threads of its own — ten
        /// seconds on a cold cache, measured — and waited for ahead of every trace, because a frame
        /// that stopped for one was a device reset.
        VisibilityPass mVisibility;
        CompositePass mComposite;

        /// One bin for everything binned: what differs per scene is the tables, and the camera
        /// arrives with the frame. And one shade, which runs ahead of the bin over the same tables.
        SpriteBinPass mSpriteBin;
        SpriteShadePass mSpriteShade;

        DenoisePasses mDenoise;
    };
}
