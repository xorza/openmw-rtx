#ifndef RTX_LIB_UPSCALEDEPTH_GLSL
#define RTX_LIB_UPSCALEDEPTH_GLSL

#include "camera.h"
#include "gbuffer.h"

/// The device depth a rasterizer would have written at `pixel`, reversed and with its far plane at
/// infinity, from `seen`, what `CHANNEL_SURFACE` stored for it: the view depth is the surface's
/// distance along the pixel's own ray times the ray's cosine to the eye's forward axis, and the
/// device depth `near` over it. Nought where the ray met no surface. Every upscaler is handed this
/// one depth.
float upscaleDepth(vec2 seen, ivec2 pixel, Eyes eyes, float near)
{
    const Camera eye = eyeOfPixel(seen.y, eyes);
    const float along = surfaceDistance(seen.y) * dot(rayAt(eye, vec2(pixel)).mDirection, eye.mBasis.mForward);
    return seen.x == SURFACE_NO_NORMAL ? 0.0 : near / max(along, near);
}

#endif
