#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RANDOM_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RANDOM_GLSL

// Blue noise across the screen, a low-discrepancy sequence along time, and what a pair of those
// numbers becomes when a shadow ray or a bounce asks for a direction.

#include "scene.h"
#include "basis.glsl"
#include "bindings.glsl"
#include "bluenoise.glsl"
#include "hash.glsl"

/// The bounce's pair on its own, for a march that carries one draw along its own steps rather
/// than through the frames: each step turns by the same two irrationals the frames turn by.
const vec2 R2_STEPS = vec2(STREAM_TURN[STREAM_BOUNCE], STREAM_TURN[STREAM_BOUNCE + 1u]);

/// A stream of draws for one pixel, where the tile above gives one.
///
/// **The tile answers a different question and cannot be stretched to this one.** Blue noise arranges draws
/// *across the screen*, which is what lets one sample per pixel filter well; resampling needs a *sequence*, a
/// fresh number per candidate, and a sequence has no screen-space arrangement.
uint randomSeed(uint key)
{
    // The frame is mixed in here so a sequence advances between frames without the caller remembering to, and
    // the accumulator in front of the filter sees an independent draw each time.
    return seededKey(key, frame.mFrame);
}

/// One number in `[0, 1)` for `pixel`, from this frame's `stream`th draw.
///
/// **Two sources, and the frame says which**: `frame.mNoise`, resolved once per frame with the denoiser, so
/// the branch is uniform.
///
/// **The tile: blue noise across the screen, a low-discrepancy sequence along time.** The tile decides how a
/// pixel's draw differs from its neighbours', so their error alternates and does not clump into blotches a
/// filter would read as shading; the turn decides how it differs from its own last frame, so the accumulated
/// samples sweep the interval.
///
/// **The hash: independent draws with no arrangement**: every pixel, frame and stream seeds a counter of its
/// own, which the tile (one sequence, rotated, repeated every sixty-four pixels) is not.
float randomAt(uvec2 pixel, uint stream)
{
    if (frame.mNoise == NOISE_WHITE_HASH)
    {
        uint state = randomSeed(pixelKey(pixel) ^ stream * 0x68E31DA4u);
        return randomNext(state);
    }

    return turnedTile(blueNoiseAt(tileIndex(pixel, stream)), frame.mFrame, stream);
}

/// Two numbers in `[0, 1)` for one pixel, from `stream` and the one after it.
vec2 unitPair(uvec2 pixel, uint stream)
{
    return vec2(randomAt(pixel, stream), randomAt(pixel, stream + 1u));
}

/// `unitPair` for bounce ray `ray` of the frame's `VisibilityConstants::mBounceRays`, counting the first as
/// nought.
///
/// **The tile turned a step a ray**, each frame's rays the next `mBounceRays` steps of the turn: the rays of
/// one frame sweep the interval as the frames of one ray do, so several a frame are a stratified sample.
vec2 unitPairOfRay(uvec2 pixel, uint stream, uint ray)
{
    if (frame.mNoise == NOISE_WHITE_HASH)
    {
        uint state = randomSeed(pixelKey(pixel) ^ stream * 0x68E31DA4u ^ ray * 0x9E3779B9u);
        uint next = randomSeed(pixelKey(pixel) ^ (stream + 1u) * 0x68E31DA4u ^ ray * 0x9E3779B9u);
        return vec2(randomNext(state), randomNext(next));
    }

    const uint step = frame.mFrame * frame.mBounceRays + ray;
    return vec2(turnedTile(blueNoiseAt(tileIndex(pixel, stream)), step, stream),
        turnedTile(blueNoiseAt(tileIndex(pixel, stream + 1u)), step, stream + 1u));
}

/// A direction inside the cone about `axis` that a source subtends, drawn evenly over its solid angle.
///
/// **This is the whole of what a soft shadow is**: a shadow ray drawn from the source's cone rather than down
/// its axis puts a penumbra under every occluder. Even over the solid angle is even in the cosine, the right
/// draw for a disc of uniform radiance, and leaves nothing to weigh the sample by.
///
/// @param sine the sine of the cone's half-angle: the source's radius over its distance, and for
///        the sun a constant. Zero is a point source and returns `axis` exactly, so a light with
///        no size casts a hard edge.
vec3 coneDirection(vec3 axis, float sine, vec2 u)
{
    const float cosine = sqrt(max(1.0 - sine * sine, 0.0));

    // `1 - cos(half-angle)`, written so that it is never a subtraction of two numbers that are nearly
    // equal.
    const float versine = sine * sine / (1.0 + cosine);

    const float drop = u.x * versine;
    const float radius = sqrt(drop * (2.0 - drop));
    const float turn = TAU * u.y;

    const TangentFrame around = frameAbout(axis);
    return around.mTangent * (radius * cos(turn)) + around.mBitangent * (radius * sin(turn)) + axis * (1.0 - drop);
}

/// A direction anywhere on the sphere, drawn evenly over it.
///
/// **For an asker with no direction to face away from**, which is a froxel of the air: it is lit from every
/// side, so what stands over it is a question about the whole sphere rather than about a hemisphere.
vec3 sphereDirection(vec2 u)
{
    const float height = 1.0 - 2.0 * u.x;
    const float radius = sqrt(max(1.0 - height * height, 0.0));
    const float turn = TAU * u.y;

    return vec3(radius * cos(turn), radius * sin(turn), height);
}

/// A direction about `normal`, drawn with probability proportional to its cosine.
///
/// **The one distribution that cancels the cosine term**: a diffuse surface weights by `cos / pi` and this
/// draws in that proportion, so the estimator is the incoming radiance with no weight left to carry.
vec3 cosineDirection(vec3 normal, vec2 u)
{
    const float radius = sqrt(u.x);
    const float angle = TAU * u.y;

    const TangentFrame around = frameAbout(normal);

    return around.mTangent * (radius * cos(angle)) + around.mBitangent * (radius * sin(angle))
        + normal * sqrt(max(1.0 - u.x, 0.0));
}

#endif
