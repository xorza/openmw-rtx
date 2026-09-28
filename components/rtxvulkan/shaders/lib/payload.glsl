#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PAYLOAD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PAYLOAD_GLSL

// What crosses between the launch and the shader a trace runs.
//
// **The whole of what the frame's tail needs, and nothing the launch already holds.** The ray is
// the launch's own, so its origin and direction stay there; whether it hit and how far it went are
// the shader's to say, and travel here.
//
// **What crosses the trace is what it costs**, and this is it: twelve words. Every field the
// tail reads travels, and travels as small as the frame keeps it — the albedo, the scalars and
// the motion vector as halves, which is the width of the channels they are stored in, and the
// normal as the surface channel's own code. What stays whole is the two radiances, because a
// reference is a sum of a thousand frames and a term rounded to a half before the sum does not
// average away. `Answer` is the same record unpacked, which is what the shaders write and the launch
// reads; `packAnswer` and `unpackAnswer` are the whole of the boundary.
//
// **The answers and not the questions.** Where a surface stood last frame is worked out by the
// shader that found it, which has the instance's row, its mesh and where on the triangle the ray
// landed already in hand — so the payload carries the vector a pixel stores, and not the point for
// the launch to reproject, which would read the rows a second time.
//
// **Every word of it flows outwards.** The launch writes nothing here before a trace: what a
// closest-hit shader is told, it reads off its shader-table record (`Shaders::HitRecord`).

#include "records.glsl"

/// Where the payload below sits. A literal at every call, as the extension wants. The any-hit
/// shader a traversal reaches declares none, because it reads none.
#define RTX_PAYLOAD 0

/// What the shader a trace ran hands back to the launch, unpacked: the record a hit or miss shader
/// fills in and the launch's tail reads, and never what crosses the trace itself.
struct Answer
{
    /// What the surface sends back along the ray, before the pane, the water column, the air and
    /// the sprites the launch composites in front of it.
    vec3 mRadiance;

    /// The one bounce this hit gathered, kept apart because the filter runs over it demodulated by
    /// the albedo in `mResponse`, and the composite multiplies the two back together afterwards.
    vec3 mBounced;

    /// What the shading model made of the surface, for the filter and the composite. `noResponse`
    /// where nothing was shaded — a pane, whose response is the surface behind it.
    SurfaceResponse mResponse;

    /// Where what the pixel shows stood on the previous frame's screen, less where it stands on this
    /// one, in pixels: `motionOf` for a surface, `skyMotionOf` for the sky. Nought where nothing
    /// was shaded for the frame to keep — a pane, whose motion is the surface behind it.
    vec2 mMotion;

    /// The miss only: how much of the backdrop the pixel shows through what the sky drew — the star
    /// field behind a sky, and the interface, whole, behind a picture that has none.
    float mBackdropShown;

    /// How much of the surface the ray met is there: what the launch composites a pane by.
    float mOpacity;

    /// Whether the launch peels what was shaded — `peeled` in the hit shader, decided there on the
    /// opacity at full precision. **Carried and not asked again of `mOpacity`**, which crosses as
    /// a half: an opacity within 2^-12 of one rounds up to it, and a launch that asked the half
    /// kept, whole, a pane the hit had already answered with no motion and no response.
    bool mPane;

    /// Whether what was shaded is water, which the glare's query counts as nothing that writes
    /// depth.
    bool mWater;

    /// Whether the ray met a surface, and how far along it: the hit's distance, or the ray's own
    /// end where it met nothing.
    bool mHit;
    float mDistance;
};

/// Everything the launch reads, at what a shader that answered nothing would leave it.
///
/// **What every shader the table names starts from**, because a launch reads every field
/// whatever ran: the sky writes no response, and a field
/// one shader skipped would otherwise carry whatever the last pixel through that lane put there.
Answer noAnswer()
{
    Answer answer;
    answer.mRadiance = vec3(0.0);
    answer.mBounced = vec3(0.0);
    answer.mResponse = noResponse();
    answer.mMotion = vec2(0.0);
    answer.mBackdropShown = 0.0;
    answer.mOpacity = 1.0;
    answer.mPane = false;
    answer.mWater = false;
    answer.mHit = false;
    answer.mDistance = 0.0;

    return answer;
}

/// The record as it crosses the trace: twelve words, laid out once here.
///
/// The flags word carries the backdrop's share as a half in its high bits, and three facts in its
/// low ones: whether the ray hit, whether the launch peels the surface, and whether it is water.
struct VisibilityPayload
{
    vec3 mRadiance;
    vec3 mBounced;

    /// The response's diffuse, then the opacity: four halves in two words.
    uvec2 mHalves;

    /// The response's normal code, `packSurfaceNormal`, as its bits: a whole number or minus one,
    /// never a NaN, so the word comes back as the float it went in as.
    uint mNormal;

    /// The motion vector, a pair of halves: the width `GBUFFER_MOTION` stores it at.
    uint mMotion;

    /// Whole, because the launch places the layers, the water and the surface channel by it.
    float mDistance;

    uint mFlags;
};

const uint ANSWER_WATER = 1u << 0u;
const uint ANSWER_PANE = 1u << 1u;
const uint ANSWER_HIT = 1u << 2u;

VisibilityPayload packAnswer(Answer answer)
{
    VisibilityPayload packed;
    packed.mRadiance = answer.mRadiance;
    packed.mBounced = answer.mBounced;
    packed.mHalves = uvec2(packHalf2x16(answer.mResponse.mDiffuse.rg),
        packHalf2x16(vec2(answer.mResponse.mDiffuse.b, answer.mOpacity)));
    packed.mNormal = floatBitsToUint(answer.mResponse.mNormal);
    packed.mMotion = packHalf2x16(answer.mMotion);
    packed.mDistance = answer.mDistance;
    packed.mFlags = packHalf2x16(vec2(0.0, answer.mBackdropShown)) | (answer.mWater ? ANSWER_WATER : 0u)
        | (answer.mPane ? ANSWER_PANE : 0u) | (answer.mHit ? ANSWER_HIT : 0u);

    return packed;
}

Answer unpackAnswer(VisibilityPayload packed)
{
    const vec2 diffuseRg = unpackHalf2x16(packed.mHalves.x);
    const vec2 diffuseBOpacity = unpackHalf2x16(packed.mHalves.y);

    Answer answer;
    answer.mRadiance = packed.mRadiance;
    answer.mBounced = packed.mBounced;
    answer.mResponse = SurfaceResponse(uintBitsToFloat(packed.mNormal), vec3(diffuseRg, diffuseBOpacity.x));
    answer.mMotion = unpackHalf2x16(packed.mMotion);
    answer.mBackdropShown = unpackHalf2x16(packed.mFlags).y;
    answer.mOpacity = diffuseBOpacity.y;
    answer.mPane = (packed.mFlags & ANSWER_PANE) != 0u;
    answer.mWater = (packed.mFlags & ANSWER_WATER) != 0u;
    answer.mHit = (packed.mFlags & ANSWER_HIT) != 0u;
    answer.mDistance = packed.mDistance;

    return answer;
}

#endif
