#pragma once

#include <cstdint>

namespace Rtx
{
    /// **The largest side a frame is shown at**: the least `maxImageDimension2D` a device may report
    /// and be chosen (`PhysicalDevice::profileOf`), which every target card reports or passes. The
    /// frame is held to it where its size is decided, before an image is made at it.
    inline constexpr std::uint32_t sLargestFrameSide = 16384;

    /// What the renderer traces at, and what it presents at. Equal wherever nothing is upscaling.
    struct FrameExtents
    {
        /// The trace's own resolution, and so the size of every G-buffer channel, of the camera the
        /// trace is handed, and of a channel read back.
        std::uint32_t mRenderWidth = 0;
        std::uint32_t mRenderHeight = 0;

        /// The size of what `Renderer::readPixels` gives back, which is what `Renderer::resize`
        /// was asked for.
        std::uint32_t mOutputWidth = 0;
        std::uint32_t mOutputHeight = 0;
    };
}
