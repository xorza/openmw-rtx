#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SURFACEMATCH_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SURFACEMATCH_GLSL

// Whether two samples of the frame are one surface: the rule every denoising pass asks by, over time
// (a history's texel against this frame's pixel) and across the screen (a tap against its centre).
//
// **One rule, so two filters cannot disagree about where a surface ends.** The accumulator, the
// wavelet and the shadow denoiser each reject what is not the surface in front of them; written
// three times, a shadow would stop at an edge the bounce under it blurs across.

#include "shared/accumulate.h"
#include "camera.h"
#include "gbuffer.h"
#include "look.h"

/// What a surface history holds of a pixel whose `CHANNEL_SURFACE` reads `seen`: the shading
/// normal and the distance from the eye times `distanceScale` (`HistoryConstants::mDistanceScale`),
/// or nought where no surface stands, which no surface matches. **One statement** for the
/// accumulator, which keeps it beside the bounce's mean, and the pane filter, which keeps the
/// nearest layer's.
vec4 heldSurfaceOf(vec2 seen, float distanceScale)
{
    const vec3 normal = unpackSurfaceNormal(seen.x);
    return dot(normal, normal) > 0.0 ? vec4(normal, surfaceDistance(seen.y) * distanceScale) : vec4(0.0);
}

/// Where a pixel's surface stood on the previous frame's screen, as the four texels a bilinear fetch
/// of it spans: the lower corner, and how far across the four it lies.
struct HistoryFootprint
{
    ivec2 mLow;
    vec2 mAcross;
};

/// **At the pixel's centre and not at where the ray went** (decision 1, the hybrid rule): a history
/// texel holds a mean over many frames' samples, each through `i + 0.5 + jitter`, whose centre is
/// `i + 0.5` — so it is fetched at `at + 0.5 + motion`, as NRD fetches its own. Fetched at the
/// jittered point, a still history was resampled a different fraction of a pixel out every frame,
/// a blur of twice the jitter's variance that fed back into itself. What holds one frame's geometry
/// instead is rebuilt through the previous jitter (`heldSurfaceMatches`).
///
/// @param moved the pixel's `CHANNEL_MOTION`.
HistoryFootprint historyFootprint(ivec2 at, vec3 moved)
{
    const vec2 before = vec2(at) + 0.5 + moved.xy;
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

/// A pixel's surface as the history is held to it: where the previous frame's eye saw it, and how
/// far off its plane a texel may stand and still be it. Made once a pixel by `historyPlane`, and
/// every texel any temporal filter takes is held to it by `heldSurfaceMatches`.
struct HistoryPlane
{
    /// The eye the history's texels were traced through: the previous basis, at the arms' spread
    /// where the pixel is on an arm, and the previous jitter, since a held surface is one frame's
    /// geometry (`historyFootprint`).
    Camera mBefore;

    vec3 mNormal;

    /// The pixel's point from that eye, which no history texel is rebuilt without.
    vec3 mAnchor;

    /// In world units: `planeTolerance`, and below nought where there was no previous eye, which
    /// refuses every texel.
    float mTolerance;

    /// `HistoryConstants::mDistanceScale`, which a held distance is divided by.
    float mDistanceScale;
};

/// How far off a pixel's plane a history texel may stand and still be its surface, in world units:
/// ReLAX's `disocclusionThreshold * frustumSize / lerp(0.05, 1, NoV)`, with `ACCUMULATE_PLANE` its
/// threshold and NoV the cosine between the surface and the ray that found it.
///
/// **Looser at a grazing angle, as ReLAX is**: there a step across the surface moves the point far
/// along the ray, and what the motion and the stored distance round by moves it off the plane by as
/// much. The frustum's side is `footprintAlong` times the image's narrower extent.
float planeTolerance(Camera eye, float away, vec3 normal, vec3 direction)
{
    const float side = footprintAlong(eye, away) * float(min(eye.mWidth, eye.mHeight));
    return ACCUMULATE_PLANE * side / mix(0.05, 1.0, abs(dot(normal, direction)));
}

/// The previous frame's eye of a pixel `onArms` (`surfaceOnArms`): the arms' plane over the
/// previous basis, as `previousScreenThrough` reads it, or the world's.
Camera previousEye(HistoryConstants history, bool onArms)
{
    Camera eye = onArms ? history.mEyes.mArms : history.mEyes.mWorld;
    const vec2 spread = onArms ? history.mArmsSpread : vec2(1.0);
    eye.mBasis = history.mPrevious;
    eye.mBasis.mRight *= spread.x;
    eye.mBasis.mUp *= spread.y;
    eye.mJitter = history.mPreviousJitter;
    return eye;
}

/// The plane of the surface at `at` whose `CHANNEL_SURFACE`, or `CHANNEL_PANE_SURFACE`, reads `seen`.
///
/// **Its point from the eye the history was measured from.** `CHANNEL_MOTION` says where it stood
/// on the previous screen and how much farther from that eye, so it is rebuilt there and not at
/// this frame's distance: compared from this frame's eye, an eye walking toward a surface found
/// each frame's history a step off the plane, and the filter showed one frame's noise. **The ray
/// through the previous basis at this frame's jitter**, since the motion is measured from the
/// jittered point this frame's ray was aimed at (`reprojected`). **No history where there was no
/// previous eye**: its basis is nought, a ray through it has no direction, and the tolerance below
/// nought says so to every texel rather than a comparison with whatever that ray came to.
///
/// @param moved the pixel's `CHANNEL_MOTION`.
HistoryPlane historyPlane(HistoryConstants history, ivec2 at, vec2 seen, vec3 moved)
{
    const vec3 normal = unpackSurfaceNormal(seen.x);
    const float away = surfaceDistance(seen.y);
    const Camera eye = eyeOfPixel(seen.y, history.mEyes);
    const Camera before = previousEye(history, surfaceOnArms(seen.y));
    Camera landed = before;
    landed.mJitter = eye.mJitter;
    const Ray ray = rayAt(landed, vec2(at) + moved.xy);
    const vec3 anchor = ray.mOffset + ray.mDirection * (away + moved.z);
    const bool seenBefore = dot(history.mPrevious.mForward, history.mPrevious.mForward) > 0.0;
    const float tolerance = planeTolerance(eye, away, normal, rayAt(eye, vec2(at)).mDirection);
    return HistoryPlane(before, normal, anchor, seenBefore ? tolerance : -1.0, history.mDistanceScale);
}

/// Whether the history texel at `tap`, which holds `was` — its normal in `xyz`, nought where nothing
/// was accumulated, and its distance in `w` times `HistoryConstants::mDistanceScale` — is the
/// surface `plane` stands for: its normal within `ACCUMULATE_FACING` of the plane's, and the point it
/// holds, rebuilt along the previous eye's ray through it, within the plane's tolerance of it. **The
/// one rule** every temporal filter holds a history to.
bool heldSurfaceMatches(vec4 was, ivec2 tap, HistoryPlane plane)
{
    if (dot(was.xyz, was.xyz) <= 0.0)
        return false;

    const vec3 there = positionAlong(plane.mBefore, tap, was.w / plane.mDistanceScale);
    return dot(was.xyz, plane.mNormal) >= ACCUMULATE_FACING
        && abs(dot(plane.mNormal, there - plane.mAnchor)) <= plane.mTolerance;
}

/// What a history fetch takes of each of the four texels `footprint` spans, into a new `vec4`
/// named `shares`: its bilinear share where it is on the screen, is the surface of `plane`
/// (`heldSurfaceMatches` against `heldImage`) and holds a history (`holds`, an expression that may
/// name the tap as `historyAt`), and nought where it is refused. Their sum, before any kernel
/// divides by it, is how much of the footprint the history covers.
///
/// **One gather for every temporal filter**, so the accumulator, the shadow denoiser, the glossy
/// filter and the pane filter cannot come to refuse a tap by four rules: written four times, the
/// shadow's lost its test for a texel that held no history. **A macro because an image is not an
/// argument** these kernels can hand a function, as `RTX_RESOLVE` says of a query; each kernel then
/// weighs its own payload by the shares.
#define RTX_HISTORY_SHARES(shares, footprint, extent, heldImage, holds, plane)                    \
    vec4 shares = vec4(0.0);                                                                       \
    for (int historyCorner = 0; historyCorner < 4; ++historyCorner)                                \
    {                                                                                              \
        const float historyBilinear = historyShare(footprint, historyCorner);                      \
        const ivec2 historyAt = historyTap(footprint, historyCorner);                              \
        if (historyBilinear <= 0.0 || outsideOf(historyAt, extent)                                 \
            || !heldSurfaceMatches(imageLoad(heldImage, historyAt), historyAt, plane) || !(holds)) \
            continue;                                                                              \
        shares[historyCorner] = historyBilinear;                                                   \
    }

/// How much of a history's footprint its matched taps cover: the shares' sum.
float historyCovered(vec4 shares)
{
    return shares.x + shares.y + shares.z + shares.w;
}

/// How nearly a tap faces the way the centre's surface does, as a weight: SVGF's normal test, the
/// cosine to the `power` — `ATROUS_NORMAL_POWER` for the wavelet's levels and the shadow's,
/// `ACCUMULATE_FIX_NORMAL_POWER` for the history fix.
///
/// **Clamped above as well as below.** A unit vector normalised in floats has a length just off one,
/// so a dot with a normal that matches — the centre tap's with its own, above all — can pass one, and
/// a hundred and twenty-eight powers of that is a weight too heavy.
float facingWeight(vec3 normal, vec3 there, float power)
{
    return pow(clamp(dot(normal, there), 0.0, 1.0), power);
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
