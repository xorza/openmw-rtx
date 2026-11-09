#pragma once

#include <cstdint>

#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    /// Everything one reconstruction reads. Every image is created with `VK_IMAGE_USAGE_SAMPLED_BIT`,
    /// since an upscaler samples all of them.
    struct UpscaleInputs
    {
        /// The frame at render resolution, as the trace chain composed it: denoised where the
        /// denoisers ran, and the trace's own composite where nothing filtered it.
        const Image& mColour;

        /// `CHANNEL_SURFACE`: the distance from the eye along each pixel's ray in `g`, which the
        /// upscaler's depth is worked out from, and in its sign the eye the ray left.
        const Image& mSurface;

        /// Where each surface stood on the previous frame's screen, less where it was sampled on this
        /// one, in render pixels.
        const Image& mMotion;

        /// `CHANNEL_UPSCALE_MASKS`: the reactive mask and the transparency and composition mask, as
        /// the trace measured what `mMotion` does not describe. FSR reads both; DLSS has no
        /// equivalent and ignores it.
        const Image& mMasks;

        /// The two eyes the trace sampled through, jitter and all.
        Shaders::Eyes mEyes;

        /// Where inside its pixel the previous frame sampled: `VisibilityConstants::mPreviousJitter`.
        Shaders::vec2 mPreviousJitter;

        /// `VisibilityConstants::mFrame`, which says how the channels' fields lie.
        std::uint32_t mFrame = 0;

        /// `Reconstruction::mJitterPhases`: what the jitter the trace sampled with repeats after.
        std::uint32_t mJitterPhases = 0;

        /// How long since the previous frame, in seconds, or nought where there was none.
        float mSeconds = 0.0f;

        /// The frame in flight this reconstruction is recorded into, whose constants it writes.
        FrameSlot mSlot;
    };
}
