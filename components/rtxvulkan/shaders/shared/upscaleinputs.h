#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UPSCALEINPUTS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UPSCALEINPUTS_H

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What every upscaler's input reads the trace's channels with, for the shader and the host that
    /// writes it: the two eyes a pixel's surface can name, the near plane the reversed depth is
    /// written against (`upscaleDepth`), and the frame whose field mapping wrote the channels
    /// (`fieldSwapOf`).
    struct UpscaleInputConstants
    {
        Eyes mEyes;
        float mNear;
        uint mFrame;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(UpscaleInputConstants) == 160, "UpscaleInputConstants must be scalar-packed on every side");
}
#endif

#endif
