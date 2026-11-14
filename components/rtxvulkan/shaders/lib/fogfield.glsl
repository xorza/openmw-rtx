#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOGFIELD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOGFIELD_GLSL

// How much air there is at a point: the bank's coverage, read off the field at three scales, and the layer's
// falloff with height. Apart from what the air scatters (`fog.glsl`), which traces, so a pass that only asks
// how much air a path crosses reaches no traversal.

#extension GL_EXT_control_flow_attributes : require

#include "look.h"
#include "bindings.glsl"
#include "frame.glsl"
#include "variants.glsl"

/// The turns `FOG_TURN_MIDDLE` and `FOG_TURN_FINE` state, as the matrices a scale's read is turned
/// by, coarsest first.
const mat2 FOG_TURN[FOG_SCALES] = mat2[FOG_SCALES](mat2(1.0, 0.0, 0.0, 1.0),
    mat2(FOG_TURN_MIDDLE.x, FOG_TURN_MIDDLE.y, -FOG_TURN_MIDDLE.y, FOG_TURN_MIDDLE.x),
    mat2(FOG_TURN_FINE.x, FOG_TURN_FINE.y, -FOG_TURN_FINE.y, FOG_TURN_FINE.x));

/// The field at a place, at one scale, read at whatever level the march can tell apart. A level of the chain
/// is the field averaged over twice the texels of the one under it, so the level a step reaches is the one
/// whose texel is the step's own width.
///
/// @param spacing how far apart the march is sampling here.
/// @param scale which of `VisibilityConstants::mFogOffsets` the read is moved by.
vec2 fogFieldAt(vec3 position, float tile, float spacing, uint scale)
{
    const float texel = tile / float(FOG_FIELD_SIZE);
    const float level = clamp(log2(max(spacing / texel, 1.0)), 0.0, FOG_FIELD_COARSEST);

    return textureLod(fogField, position / tile + frame.mFogOffsets[scale], level).xy;
}

/// The fog's shape at a point: one volume read at three scales, over a domain the coarsest drags.
///
/// Fetched rather than computed: a field hashed at every step is forty hashes at every step of a
/// twenty-four step march, nearly the whole of a trace. Three fetches stand for all of it. The three scales
/// are the fractal: amplitudes halving, frequencies stepping by `FOG_LACUNARITY`, each on its own drift.
float fogShape(vec3 position, float spacing)
{
    // **The coarsest scale is read undisplaced.** What a warp is for is breaking the regularity of
    // the structure inside a bank, and at this scale a bank is the whole shape rather than a lattice
    // with something laid on it.
    const vec2 coarse = fogFieldAt(position, FOG_TILE, spacing, 0u);

    // Two channels of a fetch already taken, so a vector out of a scalar field costs nothing. Horizontal, and
    // the volume does not change that: the vertical shape is the height falloff, which dragging the domain
    // across would blur.
    const vec3 warped = position + vec3((coarse - 0.5) * (FOG_WARP / FOG_FIELD_SPREAD), 0.0);

    float total = coarse.x - 0.5;
    float squares = 1.0;
    float amplitude = 1.0;
    float tile = FOG_TILE;

    [[unroll]] for (uint scale = 1u; scale < FOG_SCALES; ++scale)
    {
        amplitude *= 0.5;
        tile /= FOG_LACUNARITY;

        const vec3 turned = vec3(FOG_TURN[scale] * warped.xy, warped.z);
        total += amplitude * (fogFieldAt(turned, tile, spacing, scale).x - 0.5);
        squares += amplitude * amplitude;
    }

    // **Rescaled by the quadrature sum and not by the plain one**, because the scales are independent draws
    // of one field: a weighted sum of those carries the variance of the weights' squares, so this is what
    // puts the stack back at the spread one scale has.
    return 0.5 + total / sqrt(squares);
}

/// Whether this cell has a water surface for the fog to gather over.
///
/// A dry cell is handed minus infinity, the same sentinel every other depth question reads.
bool fogPools()
{
    return HAS_SEA && !isinf(frame.mWaterLevel);
}

/// The height the fog pools at. Measured from the water, not from the origin: fog gathers over water and
/// drains off high ground, and above the layer there is none. A dry cell falls back to sea level rather
/// than putting the layer infinitely far below the world.
float fogBase()
{
    return fogPools() ? frame.mWaterLevel : FOG_BASE;
}

/// What the coverage band and the water leave of the layer's strength at a point, as a fraction.
///
/// The half of the air that varies along a ray in a way nothing integrates; the other half is the height
/// falloff, which has a closed form (`fogColumn`). Splitting them lets a reader take the layer exactly and
/// sample only what it has to.
///
/// @param spacing how far apart the reader is sampling here, which decides how much of the band it
///        can resolve.
float fogCoverageAt(vec3 position, float spacing)
{
    // **Air only, and under a bay there is none.** The layer pools *at* the water rather than in it, and a
    // point below the surface already has the water's own absorption over it — fog there would be a second
    // medium laid on the first, putting grey between the eye and the seabed twice over. `waterOver` is
    // nought for a dry cell, so this costs one nothing.
    if (waterOver(position) > 0.0)
        return 0.0;

    // Even indoors, and banked out of doors: a room is smaller than one bank and its air is still, so what
    // belongs there is a faint uniform haze. One is what the band averages to, so moving between them never
    // changes how much air there is. A far step keeps its banks (`FOG_FIELD_COARSEST`) rather than trading
    // them for even air, which is a screen that glows wherever a lamp lights it.
    return mix(
        smoothstep(FOG_CLEARING, FOG_SOLID, fogShape(position, spacing)) / FOG_COVERAGE, 1.0, frame.mFogUniform);
}

/// The layer's falloff with height at a point: one at the fog's base and under it, `1/e` a scale height
/// above it.
/// How deep the layer stands is the weather's: `FOG_HEIGHT` is the bank clear weather makes in dead still
/// air, and `mFogLift` is what every other weather does to it.
float fogHeightAt(vec3 position)
{
    return exp(-max(position.z - fogBase(), 0.0) / (FOG_HEIGHT * frame.mFogLift));
}

/// The bank's coverage a stretch of `length` from `from` along `direction` is charged with: read at the
/// stretch's middle, its mean-value point, with the stretch as the spacing the field is resolved at.
float slantCoverage(vec3 from, vec3 direction, float length)
{
    return fogCoverageAt(from + direction * (0.5 * length), max(length, 1.0));
}

/// How much fog stands at a point, as a share of the weather's extinction (`mFogExtinction`): the layer's
/// height falloff times the bank's coverage, nought to one.
///
/// @param spacing how far apart the march is sampling here, which decides how much of the field it
///        can resolve.
float fogDensityAt(vec3 position, float spacing)
{
    return fogHeightAt(position) * fogCoverageAt(position, spacing);
}

#endif
