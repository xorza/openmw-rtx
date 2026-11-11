#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_DLSS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_DLSS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

#include "upscaleinputs.h"

// What the guide pass of DLSS Ray Reconstruction reads and writes, for the shader and the host that
// binds it. RR is handed the noisy frame and five images at full traced resolution: the reversed device
// depth the renderer already hands FSR, screen-space motion in render pixels, the diffuse and the
// specular albedo, and the world normal with the roughness in its fourth (`DLSS.Roughness.Mode`
// packed). The trace stores every one of them in checkerboard fields, so one dispatch rebuilds them at
// every pixel before NGX sees them.

#define RR_DEPTH_FORMAT STORAGE_R32F
#define RR_MOTION_FORMAT STORAGE_RG16F
#define RR_OUTPUT_FORMAT STORAGE_RGBA16F

/// The albedos and the normal: half floats, RR's own suggestion for the normal (3.4.3), and free for the
/// albedos, which stand in the frame's arena over the upscale's span alone, under the trace's own.
#define RR_GUIDE_FORMAT STORAGE_RGBA16F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The guide pass's bindings, in the order the host writes them.
    const uint RR_GUIDES_BIND_SURFACE = 0;
    const uint RR_GUIDES_BIND_MOTION = 1;
    const uint RR_GUIDES_BIND_ALBEDO = 2;
    const uint RR_GUIDES_BIND_SPECULAR_ALBEDO = 3;
    const uint RR_GUIDES_BIND_SPECULAR = 4;
    const uint RR_GUIDES_BIND_DEPTH = 5;
    const uint RR_GUIDES_BIND_MOTION_OUT = 6;
    const uint RR_GUIDES_BIND_DIFFUSE_ALBEDO = 7;
    const uint RR_GUIDES_BIND_SPECULAR_ALBEDO_OUT = 8;
    const uint RR_GUIDES_BIND_NORMAL_ROUGHNESS = 9;
    const uint RR_GUIDES_BIND_CONSTANTS = 10;

    /// The pass's square workgroup: eight pixels on a side, as the trace's own stages use.
    const uint RR_GUIDES_WORKGROUP = 8;

    /// The diffuse albedo RR is handed at a pixel no surface stands at: its guide's default for the sky
    /// (3.4.1), which reads to it as no surface rather than as a black one.
    const float RR_SKY_DIFFUSE_ALBEDO = 0.5f;

    /// Under which, in every channel, a diffuse albedo is handed RR as nought: half a step of eight bits,
    /// as RTX Remix's prepare pass holds it, since RR reads a near-black albedo as a pixel very brightly
    /// lit.
    const float RR_ALBEDO_FLOOR = 0.5f / 255.0f;

    /// What the guide pass reads beside the channels: what every upscaler's input reads them with, and
    /// whether any surface of the frame can have a lobe (`TraceSubject::mMapped`), without which the
    /// lobe's channels hold their nothing everywhere and the pass reads neither.
    struct RrGuideConstants
    {
        UpscaleInputConstants mInputs;
        uint mLobed;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(RrGuideConstants) == 164, "RrGuideConstants must be scalar-packed on every side");
}
#endif

#endif
