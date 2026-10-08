#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWED_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWED_GLSL

// One light a pixel keeps apart for the shadow denoiser, which the sky's source and the lamps each
// are: what it adds as though its ray got through, whether the ray did, and its penumbra.

#include "gbuffer.h"

/// What the light adds to the pixel as though its ray got through, the albedo and the lobe in;
/// whether the ray did, one or nought, drawn open by what the translucent surfaces it crossed let
/// through; and its penumbra in the pixel's footprints (`CHANNEL_PENUMBRA`). Nought, one and
/// `SHADOW_PENUMBRA_CLEAR` where nothing split it off.
struct Shadowed
{
    vec3 mLight;
    float mOpen;
    float mPenumbra;
};

Shadowed noShadowed()
{
    return Shadowed(vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR);
}

/// The light as its ray found it.
vec3 seenLight(Shadowed part)
{
    return part.mLight * part.mOpen;
}

/// The same light seen through what took `transmittance` off it on the way to the eye: the bit and
/// the penumbra are the light's own, and what the path takes is the light's alone.
Shadowed dimmed(Shadowed part, vec3 transmittance)
{
    return Shadowed(part.mLight * transmittance, part.mOpen, part.mPenumbra);
}

#endif
