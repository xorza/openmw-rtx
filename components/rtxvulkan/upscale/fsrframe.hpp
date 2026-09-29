#pragma once

#include <cstdint>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/fsr.h>

namespace Rtx
{
    /// What FSR 3.1.4's host keeps from one frame to the next and hands its passes: a port of the
    /// constant half of `ffx_fsr3upscaler.cpp`'s dispatch, with no device in it, so a test reads every
    /// number off it.
    ///
    /// **The renderer's frame, in FSR's terms**:
    ///
    /// - **The jitter is negated.** The trace aims its ray through `pixel + 0.5 + jitter`, and FSR
    ///   reads a low-resolution sample as standing at `pixel + 0.5 - Jitter()`
    ///   (`ffx_fsr3upscaler_common.h`, `GetUpsampleSample`'s source position; `Accumulate`'s
    ///   `fLrUvJittered` is the same with the sign the other way round). Both axes, because the trace's
    ///   `y` runs down the image as FSR's UV does.
    /// - **The depth is reversed and infinite**, with its near plane at `sNear`: what
    ///   `fsrcallbacks.glsl` writes, and `setupDeviceDepthToViewSpaceDepthParams`'s branch for it.
    /// - **The field of view is the camera's own**: the eye's `mRight` and `mUp` are the half extents
    ///   of the image plane at unit distance, the tangents of the half angles FSR works out from a
    ///   vertical angle and an aspect.
    class FsrFrame
    {
    public:
        /// Where the reversed depth's near plane stands, in world units. A reversed float depth is
        /// precise in proportion at every distance, so the choice moves no precision; a surface
        /// nearer than it reads as standing on it, which `fsrcallbacks.glsl` clamps to.
        static constexpr float sNear = 1.0f;

        /// What one frame is: the extents, where the trace sampled in its pixel, how long since the
        /// last frame, and whether the history is worth anything.
        struct Frame
        {
            VkExtent2D mRender;
            VkExtent2D mOutput;

            /// The eye the trace sampled through, its jitter set: what the field of view is read off.
            Shaders::Camera mCamera;

            float mDeltaMs = 0.0f;
            bool mReset = false;
        };

        /// Steps the state to `frame` and answers the main block the passes read — FSR's
        /// `fsr3upscalerDispatch` up to its first dispatch.
        const Shaders::FsrConstants& advance(const Frame& frame);

        /// The luma pyramids' block for a render extent, `ffxSpdSetup` over the whole of it, and how
        /// many workgroups the pyramids dispatch across and down.
        struct Pyramid
        {
            Shaders::FsrPyramidConstants mConstants;
            std::uint32_t mGroupsX = 0;
            std::uint32_t mGroupsY = 0;
        };
        static Pyramid pyramidFor(VkExtent2D render);

        /// Whether the frame `advance` last stepped to reads the first of each pair of histories
        /// (`false`) or the second. FSR's `resourceFrameIndex & 1`, which turns every frame.
        bool readsSecond() const { return mParity != 0; }

        /// Forgets everything, as a new context would: the next frame is a first frame.
        void restart() { *this = FsrFrame{}; }

    private:
        Shaders::FsrConstants mConstants{};
        std::uint32_t mParity = 1;
        bool mFirst = true;
    };
}
