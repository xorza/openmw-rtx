#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCEPAIRS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCEPAIRS_GLSL

// The bounce's spatial reuse, paired (Lin, Kettunen, Wyman 2026, *ReSTIR PT Enhanced*, §3): which
// partner each of a pixel's links names this frame, and whether one visible point sees a sample.
// What `bouncepairs.rgen`, which traces a pair's rays, and `bounceresolve.rgen`, which reads them,
// must answer alike to the bit.

#include "bouncepairing.h"
#include "shared/bouncereuse.h"

#include "bindings.glsl"
#include "bouncereservoir.glsl"
#include "pixels.glsl"
#include "random.glsl"
#include "traversal.glsl"

/// The pixel link `link` of `pixel` pairs it with this frame, which may stand outside the frame.
/// **The texture's turn is the frame's**, the same draw at every pixel, so a pixel's partner reads
/// the same turn and its own step leads back.
ivec2 partnerOf(uvec2 pixel, uint link)
{
    const uint size = link == 0u ? BOUNCE_PAIRING_SIZE_0 : BOUNCE_PAIRING_SIZE_1;
    const uint first = link == 0u ? 0u : BOUNCE_PAIRING_SIZE_0 * BOUNCE_PAIRING_SIZE_0;

    // A key of no pixel's: `randomSeed` mixes the frame in, and the link keeps the two apart.
    uint draws = randomSeed(SEED_BOUNCE_PAIRS ^ (link << 24u));
    const uint turn = min(uint(randomNext(draws) * 8.0), 7u);
    const uvec2 offset = min(uvec2(vec2(randomNext(draws), randomNext(draws)) * float(size)), uvec2(size - 1u));

    const uvec2 texel = pairingTexel(pixel, turn, offset, size);
    const uint word = bouncePairing[first + texel.y * size + texel.x];
    const ivec2 step = ivec2(int(word << 16u) >> 16, int(word) >> 16);
    return ivec2(pixel) + pairedStep(step, turn);
}

/// Whether the visible point `origin` sees the sample along `reach`: a solid in between, or anything
/// over the sky's direction within the world's reach, stops it.
bool bounceSeen(BounceOrigin origin, BounceReach reach)
{
    const float distance = reach.mDistance > 0.0 ? reach.mDistance - SHADOW_BIAS : frame.mReach;
    if (!(distance > SHADOW_BIAS))
        return true;

    return !solidBetween(WorldRay(frame.mOrigin + origin.mOffset, reach.mTowards), SHADOW_BIAS, distance);
}

/// Whether `origin` and `near`, both visible points this frame, are alike enough to reuse each
/// other's samples: `bounceAlike` on their normals and distances, the same both ways round.
bool originsAlike(BounceOrigin origin, BounceOrigin near)
{
    return origin.mKept && near.mKept
        && bounceAlike(dot(near.mNormal, origin.mNormal), length(origin.mOffset), length(near.mOffset));
}

#endif
