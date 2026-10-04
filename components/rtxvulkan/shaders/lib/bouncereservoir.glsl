#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCERESERVOIR_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCERESERVOIR_GLSL

// A pixel's bounce as the reuse keeps it: one sample of where the bounce landed and what left there
// toward the pixel, what the pixel's diffuse half makes of it, and how a sample found at one visible
// point is worth something at another. What the trace, the temporal pass and the resolve share, so
// the three cannot weigh one sample three ways.

#include "bouncereuse.h"
#include "brdf.h"
#include "colour.h"
#include "octahedral.h"
#include "scene.h"
#include "sharedexponent.glsl"

/// The steps of a sixteen-bit octahedral coordinate: `2 * steps + 1` values in sixteen bits.
const uint BOUNCE_UNIT_STEPS = 32767u;

/// A unit direction in one word, `octahedralCode` at sixteen bits a coordinate. A direction of no
/// length is the caller's to keep apart.
uint packUnit(vec3 direction)
{
    const uvec2 code = octahedralCode(direction, BOUNCE_UNIT_STEPS);
    return code.x | (code.y << 16u);
}

vec3 unpackUnit(uint packed)
{
    return octahedralFromCode(packed & 0xFFFFu, packed >> 16u, BOUNCE_UNIT_STEPS);
}

/// Where a bounce landed and what left there toward the visible point that found it.
struct BounceSample
{
    /// The sample point less the visible point, or a unit direction for the sky (`mSky`).
    vec3 mOffset;
    vec3 mNormal;
    bool mSky;

    /// What leaves the sample point toward the visible point, whole, and the fill's share of it.
    vec3 mRadiance;
    vec3 mFill;
};

/// A sample at infinity along `towards`, whose light is the fill whole, as the sky's is.
BounceSample skySample(vec3 towards, vec3 light)
{
    return BounceSample(towards, vec3(0.0, 0.0, 1.0), true, light, light);
}

/// One sample kept, its unbiased contribution weight, how many candidates it stands for and how
/// many frames it was kept.
struct BounceReservoir
{
    BounceSample mSample;
    float mWeight;
    uint mConfidence;
    uint mAge;
};

BounceReservoir noBounce()
{
    return BounceReservoir(BounceSample(vec3(0.0), vec3(0.0, 0.0, 1.0), false, vec3(0.0), vec3(0.0)), 0.0, 0u, 0u);
}

/// Whether a reservoir holds a sample that is worth anything.
bool holdsBounce(BounceReservoir reservoir)
{
    return reservoir.mConfidence > 0u && reservoir.mWeight > 0.0;
}

GpuBounceReservoir packBounce(BounceReservoir reservoir)
{
    const BounceSample held = reservoir.mSample;
    GpuBounceReservoir packed;
    packed.mOffset = held.mOffset;
    packed.mNormal = held.mSky ? 0u : packUnit(held.mNormal);
    packed.mRadiance = packRgb9e5(held.mRadiance);
    packed.mFill = packRgb9e5(held.mFill);
    packed.mWeight = reservoir.mWeight;
    packed.mState = min(reservoir.mConfidence, 0xFFu) | (min(reservoir.mAge, 0xFFu) << 8u)
        | (held.mSky ? BOUNCE_STATE_SKY : 0u);
    return packed;
}

BounceReservoir unpackBounce(GpuBounceReservoir packed)
{
    const bool sky = (packed.mState & BOUNCE_STATE_SKY) != 0u;
    return BounceReservoir(BounceSample(packed.mOffset, sky ? vec3(0.0, 0.0, 1.0) : unpackUnit(packed.mNormal),
                               sky, unpackRgb9e5(packed.mRadiance), unpackRgb9e5(packed.mFill)),
        packed.mWeight, packed.mState & 0xFFu, (packed.mState >> 8u) & 0xFFu);
}

/// The visible point a pixel's bounce left from, and what its diffuse half makes of a direction.
struct BounceOrigin
{
    /// Where the visible point stands, from the eye of the frame that found it.
    vec3 mOffset;
    vec3 mNormal;

    /// The triangle's normal, where `mPlaned`: what bounds the hemisphere (`behindTheFace`).
    vec3 mPlane;
    bool mPlaned;

    /// The lobe's reflectance at normal incidence, where `mGlossy`.
    vec3 mReflectance;
    bool mGlossy;

    /// A sheet's far face, as a share of its near one (`Surface::mTransmission`).
    float mTransmission;

    /// Whether the pixel left a bounce at all: a solid the eye found.
    bool mKept;
};

BounceOrigin noOrigin()
{
    return BounceOrigin(vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), false, vec3(0.0), false, 0.0, false);
}

GpuBounceOrigin packOrigin(BounceOrigin origin)
{
    const uvec3 reflectance = uvec3(round(clamp(origin.mReflectance, 0.0, 1.0) * 255.0));

    GpuBounceOrigin packed;
    packed.mOffset = origin.mOffset;
    packed.mNormal = packUnit(origin.mNormal);
    packed.mPlane = origin.mPlaned ? packUnit(origin.mPlane) : 0u;
    packed.mReflectance = reflectance.r | (reflectance.g << 8u) | (reflectance.b << 16u);
    packed.mSheet = uint(round(clamp(origin.mTransmission, 0.0, 1.0) * 65535.0))
        | (origin.mGlossy ? BOUNCE_ORIGIN_GLOSSY : 0u) | (origin.mPlaned ? BOUNCE_ORIGIN_PLANED : 0u)
        | (origin.mKept ? BOUNCE_ORIGIN_KEPT : 0u);
    return packed;
}

BounceOrigin unpackOrigin(GpuBounceOrigin packed)
{
    const bool planed = (packed.mSheet & BOUNCE_ORIGIN_PLANED) != 0u;
    const uvec3 reflectance = uvec3(packed.mReflectance, packed.mReflectance >> 8u, packed.mReflectance >> 16u) & 0xFFu;
    return BounceOrigin(packed.mOffset, unpackUnit(packed.mNormal),
        planed ? unpackUnit(packed.mPlane) : vec3(0.0, 0.0, 1.0), planed, vec3(reflectance) / 255.0,
        (packed.mSheet & BOUNCE_ORIGIN_GLOSSY) != 0u, float(packed.mSheet & 0xFFFFu) / 65535.0,
        (packed.mSheet & BOUNCE_ORIGIN_KEPT) != 0u);
}

/// What the diffuse half of `origin` makes of light arriving from `towards`, per unit albedo and
/// over the solid angle: `D(ω)` — the face it arrives on, the sheet's transmission on the far one,
/// the cosine over pi, and on a glossy near face the share `1 - F` the lobe leaves it. Nought from
/// behind the triangle, as `bounceLight` draws nothing there.
///
/// **The same terms `bounceDraw` weighs its own direction by**, so a pixel's own candidate shaded
/// here is the bounce the trace would have shaded, to the rounding of the stored reflectance.
vec3 diffuseShare(BounceOrigin origin, vec3 towards)
{
    const float facing = dot(origin.mNormal, towards);
    const float face = facing >= 0.0 ? 1.0 : -1.0;
    const bool behind = origin.mPlaned && face * dot(towards, origin.mPlane) <= 0.0;

    // The eye along the visible point's own offset: every eye stands at the frame's origin.
    const vec3 toEye = -normalize(origin.mOffset);
    const vec3 fresnel = fresnelSchlick(origin.mReflectance, specularEdge(origin.mReflectance.g),
        schlickWeight(dot(toEye, normalize(toEye + towards))));
    const vec3 lobeLeaves = origin.mGlossy && face > 0.0 ? vec3(1.0) - fresnel : vec3(1.0);

    const float side = face > 0.0 ? 1.0 : origin.mTransmission;
    return behind ? vec3(0.0) : lobeLeaves * (side * abs(facing) * INV_PI);
}

/// Which way, and how far, a sample stands from a visible point: `towards` unit, and `distance`
/// nought for the sky.
struct BounceReach
{
    vec3 mTowards;
    float mDistance;
};

/// Where `held`, found at `foundAt`, stands from the visible point at `seenAt`, both offsets from
/// one eye.
BounceReach reachOf(BounceSample held, vec3 foundAt, vec3 seenAt)
{
    if (held.mSky)
        return BounceReach(held.mOffset, 0.0);

    const vec3 between = foundAt + held.mOffset - seenAt;
    const float distance = length(between);
    return BounceReach(between / max(distance, 1e-6), distance);
}

/// What the reuse weighs a sample by at a visible point: the luminance of what the point's diffuse
/// half takes of it, `D(ω) L`.
float bounceTarget(BounceOrigin origin, BounceSample held, BounceReach reach)
{
    return dot(diffuseShare(origin, reach.mTowards) * held.mRadiance, LUMINANCE_WEIGHTS);
}

/// `reconnectionJacobian` of shifting `held`, found at `foundAt`, to `seenAt` instead. One for the
/// sky, whose direction is the same from anywhere.
float shiftJacobian(BounceSample held, vec3 foundAt, vec3 seenAt)
{
    if (held.mSky)
        return 1.0;

    const vec3 fromFound = -held.mOffset;
    const vec3 fromSeen = seenAt - (foundAt + held.mOffset);
    return reconnectionJacobian(dot(held.mNormal, fromFound), dot(fromFound, fromFound),
        dot(held.mNormal, fromSeen), dot(fromSeen, fromSeen));
}

/// A reservoir merged one candidate at a time, with the target its kept sample has at the visible
/// point it is merged for.
struct BounceMerge
{
    BounceReservoir mKept;
    float mSum;
    float mKeptTarget;

    /// Which input the kept sample came from, in the order they were offered.
    uint mKeptInput;
    uint mOffered;
};

BounceMerge startMerge()
{
    return BounceMerge(noBounce(), 0.0, 0.0, 0u, 0u);
}

/// Offers one candidate of resampling weight `weight`, standing for `confidence` candidates, and
/// keeps it with its share of the sum so far. `draw` is in `[0, 1)`.
void offer(inout BounceMerge merge, BounceSample held, float weight, float target, uint confidence, uint age,
    float draw)
{
    merge.mSum += weight;
    merge.mKept.mConfidence += confidence;
    if (weight > 0.0 && draw * merge.mSum < weight)
    {
        merge.mKept.mSample = held;
        merge.mKept.mAge = age;
        merge.mKeptTarget = target;
        merge.mKeptInput = merge.mOffered;
    }
    ++merge.mOffered;
}

/// The merged reservoir: `W` is the sum over the kept sample's target, and the confidence is held
/// to the cap. Nothing kept is nothing.
BounceReservoir finishMerge(BounceMerge merge)
{
    BounceReservoir kept = merge.mKept;
    kept.mWeight = merge.mKeptTarget > 0.0 ? merge.mSum / merge.mKeptTarget : 0.0;
    kept.mConfidence = min(kept.mConfidence, BOUNCE_CONFIDENCE_CAP);
    return kept;
}

/// The defensive pairwise MIS weight of the canonical sample (Wyman et al. 2023, Algorithm 7): its
/// own share of the confidences, and against each other input a pair's balance.
///
/// @param target the canonical sample's target at the canonical point.
/// @param others each other input's target for the canonical sample, shifted into its domain and
///        times that shift's Jacobian — `p̂←j` — and nought where the shift failed or is hidden.
/// @param confidences each other input's confidence, nought for an input that took no part.
/// @param total every confidence, the canonical one's included.
float canonicalWeight(float target, uint confidence, float total, vec4 others, vec4 confidences)
{
    const float canonical = float(confidence) * target;
    const float rest = total - float(confidence);
    const vec4 pairs = canonical / max(canonical + rest * others, vec4(1e-30));
    return (float(confidence) + dot(confidences, mix(pairs, vec4(0.0), equal(confidences, vec4(0.0))))) / total;
}

/// The defensive pairwise MIS weight of a non-canonical input's sample, shifted into the canonical
/// domain (Algorithm 7).
///
/// @param target the sample's target at its own point over the shift's Jacobian, `p̂←i`.
/// @param canonical the shifted sample's target at the canonical point.
float neighbourWeight(float target, float canonical, uint confidence, float total, uint canonicalConfidence)
{
    const float rest = (total - float(canonicalConfidence)) * target;
    const float denominator = rest + float(canonicalConfidence) * canonical;
    return denominator > 0.0 ? float(confidence) / total * rest / denominator : 0.0;
}

#endif
