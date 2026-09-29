#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RUNNINGMEAN_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RUNNINGMEAN_GLSL

// A light averaged over the frames a pixel's surface held still: the blend the glossy filter and
// the pane filter each keep a history of, over the texels `surfacematch.glsl` says are the surface.
//
// **One statement of the blend, so the two filters cannot come to disagree about what a history
// is worth**: the mean, the second moment of its luminance beside it, and the frame count, the
// accumulator's outlier clamp once a spread has been measured, and ReLAX's weight on the frame.

#include "colour.h"
#include "look.h"

/// A pixel's history as a filter holds it: the mean in `rgb` and the second moment of its
/// luminance in `a`, and how many frames it holds.
struct RunningMean
{
    vec4 mMean;
    float mFrames;
};

/// This frame's `sampled` light with no history under it: one frame, and its own moment.
RunningMean startedMean(vec3 sampled)
{
    const float lit = dot(sampled, LUMINANCE_WEIGHTS);
    return RunningMean(vec4(sampled, lit * lit), 1.0);
}

/// `sampled` blended into `held`, a history `kept` of which is still the light this pixel sees.
///
/// **The accumulator's outlier clamp, for the accumulator's reason**: a firefly is a number of
/// deviations from what the pixel has been seeing, and not a radiance. **Counted by what was kept
/// of the history**, since a history the eye turned from measured another light's spread.
///
/// **ReLAX's `max(1 - confidence, 1 / (1 + frames))`.** The count kept is what the blend made of it,
/// so a history kept in part is as short as its weight says.
///
/// @param kept from nought to one: the glossy filter's share of a reflection the view still sees,
///        and one wherever the light does not turn with the view.
RunningMean blendedMean(vec3 sampled, RunningMean held, float kept)
{
    const float lit = dot(sampled, LUMINANCE_WEIGHTS);
    const float heldLit = dot(held.mMean.rgb, LUMINANCE_WEIGHTS);

    vec3 clamped = sampled;
    if (held.mFrames * kept >= ACCUMULATE_SETTLED && lit > 0.0)
    {
        const float spread = sqrt(max(held.mMean.a - heldLit * heldLit, 0.0));
        const float ceiling = heldLit + ACCUMULATE_SIGMAS * spread;
        if (lit > ceiling)
            clamped = sampled * (ceiling / lit);
    }

    const float alpha = max(1.0 - kept, 1.0 / min(held.mFrames + 1.0, ACCUMULATE_FRAMES));
    const float clampedLit = dot(clamped, LUMINANCE_WEIGHTS);
    return RunningMean(
        vec4(mix(held.mMean.rgb, clamped, alpha), mix(held.mMean.a, clampedLit * clampedLit, alpha)), 1.0 / alpha);
}

#endif
