#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HISTORYCLAMP_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HISTORYCLAMP_GLSL

// The anti-lag every running mean keeps: a slow mean held to the box its fast means
// span around the pixel, ReLAX's history clamp (NVIDIA NRD, `RELAX_HistoryClamping`). **One rule**,
// so the bounce, the glossy reflection and a window cannot come to lag by three: each kernel sums
// its square of fast means and asks this.
//
// **Per axis in YCoCg**, as ReLAX clamps: a box over luma and the two chroma differences holds a
// change of hue at one brightness, which a box over luminance alone let through.

#include "shared/accumulate.h"
#include "colour.h"

/// The box of a pixel's fast means: their mean less and plus `ACCUMULATE_CLAMP_SPREAD` deviations,
/// per YCoCg axis.
struct FastBox
{
    vec3 mLow;
    vec3 mHigh;
};

/// The box over `count` fast means whose YCoCg values sum to `sum` and their squares to `squares`.
FastBox fastBoxOf(vec3 sum, vec3 squares, float count)
{
    const vec3 mean = sum / count;
    const vec3 deviation = sqrt(max(squares / count - mean * mean, vec3(0.0)));
    return FastBox(mean - ACCUMULATE_CLAMP_SPREAD * deviation, mean + ACCUMULATE_CLAMP_SPREAD * deviation);
}

/// A slow mean held to its box, and how far its luma moved as a share of the way to the fast mean:
/// what ReLAX moves a history's other data by and pushes both means on in proportion to
/// (`antilagAcceleration`).
struct HeldHistory
{
    vec3 mHeld;
    float mShare;
};

/// The slow mean `slow` held per axis to `box` grown to hold the pixel's own fast mean `fast`, so it
/// is never moved past it: ReLAX's colour box expansion. **Held at nought in RGB**, where a box's
/// corner lies outside the colours.
HeldHistory heldToFast(vec3 slow, vec3 fast, FastBox box)
{
    const vec3 slowAxes = ycocgOf(slow);
    const vec3 fastAxes = ycocgOf(fast);
    const vec3 held = clamp(slowAxes, min(box.mLow, fastAxes), max(box.mHigh, fastAxes));
    return HeldHistory(
        max(rgbOfYcocg(held), vec3(0.0)), antilagShare(slowAxes.x, fastAxes.x, box.mLow.x, box.mHigh.x));
}

#endif
