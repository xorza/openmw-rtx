#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_DLSS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_DLSS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What the DLSS input resolve reads and writes, for the shader and the host that binds it. DLSS
// itself is handed two full-resolution images: the reversed device depth the renderer already uses
// for FSR, and screen-space motion in render pixels. The trace stores both in checkerboard fields,
// so one dispatch rebuilds them at every pixel before NGX sees them.

#define DLSS_DEPTH_FORMAT STORAGE_R32F
#define DLSS_MOTION_FORMAT STORAGE_RG16F
#define DLSS_OUTPUT_FORMAT STORAGE_RGBA16F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The input resolve's bindings, in the order the host writes them.
    const uint DLSS_INPUTS_BIND_SURFACE = 0;
    const uint DLSS_INPUTS_BIND_MOTION = 1;
    const uint DLSS_INPUTS_BIND_DEPTH = 2;
    const uint DLSS_INPUTS_BIND_MOTION_OUT = 3;
    const uint DLSS_INPUTS_BIND_CONSTANTS = 4;

    /// The resolve's square workgroup: eight pixels on a side, as the trace's own stages use.
    const uint DLSS_INPUT_WORKGROUP = 8;

#ifdef RTX_HOST
}
#endif

#endif
