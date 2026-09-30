#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SURFACEMATCH_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SURFACEMATCH_GLSL

// Whether two samples of the frame are one surface: the rule every denoising pass asks by, over time
// (a history's texel against this frame's pixel) and across the screen (a tap against its centre).
//
// **One rule, so two filters cannot disagree about where a surface ends.** The accumulator, the
// wavelet and the shadow denoiser each reject what is not the surface in front of them; written
// three times, a shadow would stop at an edge the bounce under it blurs across.

#include "camera.h"
#include "look.h"

/// Where a pixel's surface stood on the previous frame's screen, as the four texels a bilinear fetch
/// of it spans: the lower corner, and how far across the four it lies.
struct HistoryFootprint
{
    ivec2 mLow;
    vec2 mAcross;
};

/// **In the coordinates the motion vector was written against.** The trace aims through
/// `pixel + 0.5 + jitter` and `reprojected` differences against exactly that, so undoing it adds
/// the same offset back — or the history is fetched a fraction of a pixel out, by a different
/// fraction every frame, which is a still image that shakes.
///
/// @param moved the pixel's `CHANNEL_MOTION`.
HistoryFootprint historyFootprint(ivec2 at, vec2 jitter, vec3 moved)
{
    const vec2 before = vec2(at) + 0.5 + jitter + moved.xy;
    const vec2 corner = before - 0.5;
    return HistoryFootprint(ivec2(floor(corner)), fract(corner));
}

/// The `corner`th of the four texels, nought to three: x in the low bit, y in the high one.
ivec2 historyTap(HistoryFootprint footprint, int corner)
{
    return footprint.mLow + ivec2(corner & 1, corner >> 1);
}

/// What the `corner`th texel weighs in the bilinear fetch.
///
/// **Arithmetic on the counter rather than a table read at it** — `atrous.comp`'s `kernelAt` says
/// why a fixed table is not one here.
float historyShare(HistoryFootprint footprint, int corner)
{
    const vec2 share = mix(vec2(1.0) - footprint.mAcross, footprint.mAcross, vec2(ivec2(corner & 1, corner >> 1)));
    return share.x * share.y;
}

/// Whether a history texel belongs to the surface now in front of the pixel.
///
/// **Measured from the eye the history was measured from.** The history's distance is the one the
/// previous frame found, so this pixel's is taken back there by the step `CHANNEL_MOTION` carries.
/// Compared from this frame's eye instead, an eye walking toward a surface nearer than its step
/// over `ACCUMULATE_DEPTH` — two hundred and fifty units at a run of five a frame — finds each
/// frame's history a different surface, and the filter shows one frame's noise.
///
/// @param was the surface the history belongs to: its normal in `xyz`, nought where nothing was
///        accumulated, and its distance in `w`, times `distanceScale` —
///        `HistoryConstants::mDistanceScale` says what those units are and why.
/// @param distance how far this pixel's surface is, in world units.
/// @param moved the pixel's `CHANNEL_MOTION`.
bool heldSurfaceMatches(vec4 was, vec3 normal, float distance, vec3 moved, float distanceScale)
{
    if (dot(was.xyz, was.xyz) <= 0.0)
        return false;

    const float before = (distance + moved.z) * distanceScale;
    return dot(was.xyz, normal) >= ACCUMULATE_FACING
        && abs(was.w - before) <= ACCUMULATE_DEPTH * max(before, distanceScale);
}

/// Where the trace's ray through `pixel` ended up, `away` along it, through `eye`.
///
/// **The trace's own `rayAt` through the eye the trace used, so these are the rays that were
/// actually shaded.** The eye's place drops out because every use of this is a difference between
/// two of them, which is why `Camera` does not carry one.
vec3 positionAlong(Camera eye, ivec2 pixel, float away)
{
    const Ray ray = rayAt(eye, vec2(pixel));
    return ray.mOffset + ray.mDirection * away;
}

/// What one pixel covers `away` along `eye`'s rays: the scale every plane offset is measured against.
/// Never nought, or a surface at the eye would reject every neighbour.
///
/// **A parallel projection's is a constant**, one pixel of the box wide for the whole of the ray,
/// which is the same figure the trace picks its mip level with.
float footprintAlong(Camera eye, float away)
{
    const Cone cone = coneAt(eye);
    return max(cone.mWidth + cone.mSpread * away, 1e-4);
}

/// How nearly a tap faces the way the centre's surface does, as a weight: SVGF's normal test, with
/// its exponent of `ATROUS_NORMAL_POWER`.
///
/// **Clamped above as well as below.** A unit vector normalised in floats has a length just off one,
/// so a dot with a normal that matches — the centre tap's with its own, above all — can pass one, and
/// a hundred and twenty-eight powers of that is a weight too heavy.
float facingWeight(vec3 normal, vec3 there)
{
    return pow(clamp(dot(normal, there), 0.0, 1.0), ATROUS_NORMAL_POWER);
}

/// How far a tap lies off the plane of the centre's surface, as a weight: one in the plane, and
/// falling by `e` for every `ATROUS_PLANE_SIGMA` pixel footprints out of it.
///
/// **A plane-distance test and not SVGF's depth gradient**, which is what the denoisers written
/// since — NRD's among them — settled on, and which handles a grazing floor without a derivative a
/// ray tracer has no rasterizer to hand it.
float coplanarWeight(vec3 normal, vec3 position, vec3 there, float footprint)
{
    const float offset = abs(dot(normal, there - position));
    return exp(-offset / (ATROUS_PLANE_SIGMA * footprint));
}

#endif
