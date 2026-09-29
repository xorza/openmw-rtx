#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RUNNINGMEAN_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RUNNINGMEAN_GLSL

// A light averaged over the frames a pixel's surface held still: the blend the glossy filter and
// the pane filter each keep a history of, over the texels `surfacematch.glsl` says are the surface.
//
// **One statement of the blend, so the two filters cannot come to disagree about what a history
// is worth**: the mean, the frame count, and ReLAX's weight on the frame. No outlier clamp, for the
// accumulator's reason (`accumulate.comp`).

#include "look.h"

/// A pixel's history as a filter holds it, and as one texel stores it: the mean in `rgb`, and how
/// many frames it holds in `a`, nought where it holds none.
struct RunningMean
{
    vec3 mMean;
    float mFrames;
};

/// The history a texel holds, or the bilinear sum of several over the `weight` they sum to.
RunningMean runningMeanOf(vec4 texel, float weight)
{
    return RunningMean(texel.rgb / weight, texel.a / weight);
}

vec4 texelOf(RunningMean mean)
{
    return vec4(mean.mMean, mean.mFrames);
}

/// This frame's `sampled` light with no history under it: one frame.
RunningMean startedMean(vec3 sampled)
{
    return RunningMean(sampled, 1.0);
}

/// `sampled` blended into `held`, a history `kept` of which is still the light this pixel sees.
///
/// **ReLAX's `max(1 - confidence, 1 / (1 + frames))`.** The count kept is what the blend made of it,
/// so a history kept in part is as short as its weight says.
///
/// @param kept from nought to one: the glossy filter's share of a reflection the view still sees,
///        and one wherever the light does not turn with the view.
RunningMean blendedMean(vec3 sampled, RunningMean held, float kept)
{
    const float alpha = max(1.0 - kept, 1.0 / min(held.mFrames + 1.0, ACCUMULATE_FRAMES));
    return RunningMean(mix(held.mMean, sampled, alpha), 1.0 / alpha);
}

#endif
