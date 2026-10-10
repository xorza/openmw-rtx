#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHARE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHARE_GLSL

// Sums over candidates in fixed point, which every order adds alike: what the traversal's
// see-through shadows and a medium's gathered crossings are summed in.

/// One, in the units an order-free sum over candidates is taken in: twenty fractional bits.
///
/// **A sum over candidates is an integer sum, because the order candidates arrive in is the
/// card's.** The specification's "Ray Intersection Candidate Determination" says *there is no
/// ordering guarantee between operations performed on different intersection candidates*, and a
/// float sum or product rounds differently for every order it is taken in — so a shadow made of
/// three panes came out one bit different from run to run, and the frame hash with it. An integer
/// sum is the same sum in every order. Twenty bits, because the largest term is a colour times a
/// coverage and the sums saturate at four thousand of those, which no stack of shells reaches; and
/// it holds a shadow's logarithm to a relative part in a million.
///
/// **Saturating, so an overflow is a clamp and not a wrap**: `addShare` is the one way a share is
/// summed, and `sharePart` the one way a term is made.
const float SHARE_UNIT = 1048576.0;

/// The largest term a share holds: the largest float under 2^32, which is 2^32 - 256.
///
/// **A term past it is clamped and not converted**, because a float out of a `uint`'s range
/// converts to an undefined value, and what a term carries is not all bounded: a medium's unlit
/// sheet is light and a material's colours are the content's, and nothing holds either under the
/// four thousand and ninety-six a share can name.
const float SHARE_MOST = 4294967040.0;

/// A term of an order-free sum, off a float: nought for one not above nought, a NaN among them,
/// which a comparison refuses where `max` may pass it on, and `SHARE_MOST` for one past it.
uint sharePart(float part)
{
    const float scaled = round(part * SHARE_UNIT);
    return scaled > 0.0 ? uint(min(scaled, SHARE_MOST)) : 0u;
}

uvec3 sharePart(vec3 part)
{
    const vec3 scaled = round(part * SHARE_UNIT);
    return mix(uvec3(0u), uvec3(min(scaled, vec3(SHARE_MOST))), greaterThan(scaled, vec3(0.0)));
}

/// `sharePart` undone, for a sum read back as a float.
float shareTotal(uint total)
{
    return float(total) / SHARE_UNIT;
}

uint addShare(uint total, uint part)
{
    return total + min(part, ~total);
}

uvec3 addShare(uvec3 total, uvec3 part)
{
    return total + min(part, ~total);
}

#endif
