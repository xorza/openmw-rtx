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
/// **The tile answers a different question and cannot be stretched to this one.** Blue noise is an
/// arrangement *across the screen*: it says how a pixel's draw should differ from its neighbours',
/// which is what makes a single sample per pixel filter well. Resampling needs a *sequence* — a
/// fresh number for each candidate it weighs — and there is no screen-space arrangement of a
/// sequence to arrange. Asking the tile for one would hand back the same number every time and
/// choose the first candidate that beat it, every pixel, every frame.
///
/// So this is an ordinary hashed counter, seeded per pixel and per frame. PCG's output permutation
/// over an LCG state: the state advances by multiplication and the bits are mixed on the way out,
/// which is what keeps low-order structure out of the first few draws — the ones a short reservoir
/// loop actually uses.
uint randomSeed(uint key)
{
    // The frame is mixed in here rather than by the caller, so a sequence advances between frames
    // without anyone having to remember to make it — which is what lets the accumulator in front of
    // the filter see an independent draw each time rather than the same one over and over.
    return seededKey(key, frame.mFrame);
}

/// One number in `[0, 1)` for `pixel`, from this frame's `stream`th draw.
///
/// **Two sources, and the frame says which** — `frame.mNoise`, resolved once per frame with the
/// denoiser, so the branch is uniform and every lane takes the same side.
///
/// **The tile: blue noise across the screen, a low-discrepancy sequence along time.** The tile
/// decides how a pixel's draw differs from its neighbours' — deliberately, so that the error
/// between them alternates rather than clumping into blotches a filter would read as shading. The
/// turn decides how it differs from its own last frame, so the samples a pixel accumulates sweep
/// the interval instead of stumbling about in it. Shifting every value by the same amount and
/// wrapping is Cranley and Patterson's rotation: it moves which pixel holds which number and
/// leaves the arrangement's spectrum where it was.
///
/// **The hash: independent draws with no arrangement.** Every pixel, every frame and every stream
/// seeds a counter of its own, and nothing about one draw says anything about its neighbour's or
/// its own last frame's, which the tile — one sequence, rotated, repeated every sixty-four pixels —
/// is not.
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

/// A direction inside the cone about `axis` that a source subtends, drawn evenly over its solid
/// angle.
///
/// **This is the whole of what a soft shadow is.** A source with a size is not one direction but a
/// cone of them, and a shadow ray drawn from somewhere in that cone rather than down its axis puts
/// a penumbra under every occluder whose width is the source's own size seen from it. Evenly over
/// the solid angle is evenly in the cosine, which is the right draw for a disc of uniform radiance
/// and leaves nothing to weigh the sample by.
///
/// @param sine the sine of the cone's half-angle: the source's radius over its distance, and for
///        the sun a constant. Zero is a point source and returns `axis` exactly, so a light with
///        no size casts a hard edge.
vec3 coneDirection(vec3 axis, float sine, vec2 u)
{
    const float cosine = sqrt(max(1.0 - sine * sine, 0.0));

    // `1 - cos(half-angle)`, written so that it is never a subtraction of two numbers that are
    // nearly equal. The sun is half a degree across and its cosine is 0.99999, so taking that from
    // one spends five of a float's seven digits before the draw has begun — and every one of them
    // is a step of the penumbra it is about to place.
    const float versine = sine * sine / (1.0 + cosine);

    const float drop = u.x * versine;
    const float radius = sqrt(drop * (2.0 - drop));
    const float turn = TAU * u.y;

    const TangentFrame around = frameAbout(axis);
    return around.mTangent * (radius * cos(turn)) + around.mBitangent * (radius * sin(turn)) + axis * (1.0 - drop);
}

/// A direction anywhere on the sphere, drawn evenly over it.
///
/// **For an asker with no direction to face away from**, which is a froxel of the air: it is lit
/// from every side, so what stands over it is a question about the whole sphere rather than about a
/// hemisphere. `cosineDirection` is the surface's answer and `coneDirection` a source's; this is the
/// one for a point that has neither.
///
/// The height is drawn evenly because a sphere's area is even in it — Archimedes' theorem, the same
/// fact `coneDirection` leans on over its cap.
vec3 sphereDirection(vec2 u)
{
    const float height = 1.0 - 2.0 * u.x;
    const float radius = sqrt(max(1.0 - height * height, 0.0));
    const float turn = TAU * u.y;

    return vec3(radius * cos(turn), radius * sin(turn), height);
}

/// A direction about `normal`, drawn with probability proportional to its cosine.
///
/// **The one distribution that cancels the cosine term.** A diffuse surface weights what arrives by
/// `cos / pi` and this draws in exactly that proportion, so the estimator is the incoming radiance
/// itself with no weight left to carry — which is why a single sample is worth anything at all.
///
/// Malley's method: a disc sampled evenly, lifted onto the hemisphere. `sqrt(u.x)` is the disc's
/// radius, so the height off the surface is `sqrt(1 - u.x)` and averages two thirds — which is the
/// number a test can hold this to, and the half a uniform draw would give instead.
vec3 cosineDirection(vec3 normal, vec2 u)
{
    const float radius = sqrt(u.x);
    const float angle = TAU * u.y;

    const TangentFrame around = frameAbout(normal);

    return around.mTangent * (radius * cos(angle)) + around.mBitangent * (radius * sin(angle))
        + normal * sqrt(max(1.0 - u.x, 0.0));
}

#endif
