#pragma once

#include <cstdint>

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

        /// What one frame is: the extents, where the trace sampled in its pixel, how many phases its
        /// jitter cycles through, and how long since the last frame.
        struct Frame
        {
            VkExtent2D mRender;
            VkExtent2D mOutput;

            /// The eye the trace sampled through, its jitter set: what the field of view is read off.
            Shaders::Camera mCamera;

            /// `Reconstruction::mJitterPhases`: what the sequence the trace sampled from repeats
            /// after, which the state walks toward a step a frame.
            std::uint32_t mJitterPhases = 0;

            /// How long since the last frame, in seconds, or nought where there was none.
            float mSeconds = 0.0f;
        };

        /// Steps the state to `frame` and answers the main block the passes read — FSR's
        /// `fsr3upscalerDispatch` up to its first dispatch. Spends a `reset`.
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

        /// Says the history is worthless, after a jump no motion vector can describe: the next frame
        /// starts the count again, and keeps what it carries as the previous frame's.
        void reset() { mFresh = true; }

        /// Whether the next `advance` is a first frame, which reads no history.
        bool isFresh() const { return mFresh; }

        /// Forgets everything, as a new context would: the next frame is a first frame, and carries
        /// nothing.
        void restart() { *this = FsrFrame{}; }

    private:
        Shaders::FsrConstants mConstants{};
        std::uint32_t mParity = 1;
        bool mFresh = true;
    };
}
