// `#pragma once` everywhere else in this tree, and an include guard here for the reason
// `components/rtx/shaders/portable.h` gives.
#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_LIGHTS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_LIGHTS_GLSL

// Which lamps could reach a point, and what one delivers at a distance.
//
// **The two halves every consumer of a lamp must agree on.** Three places accumulate lamps —
// a surface, the air, and a puff of smoke — and they differ in the cosine, the shadow ray
// and the phase function. What they may not differ in is the reach and the falloff.

#include "scene.h"
#include "bindings.glsl"

/// Which lamps could reach `position`, as a range into `lightIndices`.
///
/// **A shading point should not have to ask every lamp in the cell whether it is near.** Walking
/// them all costs the same whether one contributes or none do — and the fog made that unaffordable
/// rather than merely wasteful, since a march asks twenty-four times per pixel where a surface asks
/// once per hit.
///
/// A lamp is binned into every cell its reach touches, so this range is complete: the distance test
/// each caller still makes is a refinement of the answer and never a correction to it. A position
/// outside the grid is one no lamp can reach, which is why falling off the edge returns nothing
/// rather than clamping to the nearest cell's list.
uvec2 lampsReaching(vec3 position)
{
    const vec3 cell = floor((position - grid.mOrigin) * grid.mInverseCell);
    if (any(lessThan(cell, vec3(0.0))) || any(greaterThanEqual(cell, vec3(grid.mSize))))
        return uvec2(0u, 0u);

    const uvec3 at = uvec3(cell);
    // `flat` is what this wants to be called, and GLSL reserves it for interpolation.
    const uint index = (at.z * grid.mSize.y + at.y) * grid.mSize.x + at.x;

    return uvec2(lightOffsets[index], lightOffsets[index + 1u]);
}

/// How much of a light `distance` away arrives, per unit intensity.
///
/// An inverse square windowed to arrive at exactly zero where the light's reach ends. Morrowind's
/// reach is a hard cutoff, and merely clipping an inverse square leaves a visible ring on the floor
/// where it stops. The `+ 1` keeps the singularity at zero distance finite; a lamp is not a point.
float falloff(float distance, float reach)
{
    const float ratio = distance / reach;
    const float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (distance * distance + 1.0);
}

#endif
