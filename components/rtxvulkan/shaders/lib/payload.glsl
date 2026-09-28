#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PAYLOAD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PAYLOAD_GLSL

// What crosses between the launch and the shader an execute runs.
//
// **The whole of what the frame's tail needs, and nothing the hit object already answers.** A
// launch reads the hit itself, its distance and its ray straight off the `hitObjectEXT` through
// `hitObjectIsHitEXT`, `hitObjectGetRayTMaxEXT` and the ray's own getters, so not one word here is
// spent on them.
//
// **What crosses the execute is what it costs**, and this is it: twelve words. Every field the
// tail reads travels, and travels as small as the frame keeps it — the albedo, the scalars and
// the motion vector as halves, which is the width of the channels they are stored in, and the
// normal as one word of octahedral halves. What stays whole is the two radiances, because a
// reference is a sum of a thousand frames and a term rounded to a half before the sum does not
// average away. `Answer` is the same record unpacked, which is what the shaders write and the launch
// reads; `packAnswer` and `unpackAnswer` are the whole of the boundary.
//
// **The answers and not the questions.** Where a surface stood last frame is worked out by the
// shader that found it, which has the instance's row, its mesh and where on the triangle the ray
// landed already in hand — so the payload carries the vector a pixel stores, and not the point for
// the launch to reproject, which would read the rows a second time.
//
// **Every word of it flows outwards.** The launch writes nothing here before an execute: what a
// closest-hit shader is told, it reads off its shader-table record, and `Shaders::HitRecord` says
// what measuring the other direction found.

#include "basis.glsl"
#include "records.glsl"

/// Where the shading payload below sits. A literal at every call, as the extension wants.
#define RTX_PAYLOAD 0

/// Where the payload traversal carries sits, which is a different and far smaller one.
///
/// **Traversal and shading invoke different shaders, so they are given different payloads.** The
/// any-hit shader a traversal reaches tests a cutout and reads nothing at all, where a closest-hit
/// shader fills in every field below — and the whitepaper's own example of when to split them is
/// this one. A traversal handed the shading payload pays register pressure for fields the shader it
/// runs never touches.
#define RTX_TRAVERSAL_PAYLOAD 1

/// What the shader an execute ran hands back to the launch, unpacked: the record a hit shader
/// fills in and the launch's tail reads, and never what crosses the execute itself.
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

    return answer;
}

/// The record as it crosses the execute: twelve words, laid out once here.
///
/// The flags word carries the backdrop's share as a half in its high bits, and three facts in its low
/// ones: whether the launch peels the surface, whether it is water, and whether the response
/// carries a normal at all. The last is what a pane and the sky leave nought, and nought has no
/// direction to pack.
struct VisibilityPayload
{
    vec3 mRadiance;
    vec3 mBounced;

    /// The response's diffuse, then its roughness, then the opacity: five halves in three words.
    uvec3 mHalves;

    /// The response's normal, `packDirection`.
    uint mNormal;

    /// The motion vector, a pair of halves: the width `GBUFFER_MOTION` stores it at.
    uint mMotion;

    uint mFlags;
};

const uint ANSWER_WATER = 1u << 0u;
const uint ANSWER_HAS_NORMAL = 1u << 1u;
const uint ANSWER_PANE = 1u << 2u;

VisibilityPayload packAnswer(Answer answer)
{
    const bool hasNormal = dot(answer.mResponse.mNormal, answer.mResponse.mNormal) > 0.0;

    VisibilityPayload packed;
    packed.mRadiance = answer.mRadiance;
    packed.mBounced = answer.mBounced;
    packed.mHalves = uvec3(packHalf2x16(answer.mResponse.mDiffuse.rg),
        packHalf2x16(vec2(answer.mResponse.mDiffuse.b, answer.mResponse.mRoughness)),
        packHalf2x16(vec2(answer.mOpacity, 0.0)));
    packed.mNormal = hasNormal ? packDirection(answer.mResponse.mNormal) : 0u;
    packed.mMotion = packHalf2x16(answer.mMotion);
    packed.mFlags = packHalf2x16(vec2(0.0, answer.mBackdropShown)) | (answer.mWater ? ANSWER_WATER : 0u)
        | (hasNormal ? ANSWER_HAS_NORMAL : 0u) | (answer.mPane ? ANSWER_PANE : 0u);

    return packed;
}

Answer unpackAnswer(VisibilityPayload packed)
{
    const bool hasNormal = (packed.mFlags & ANSWER_HAS_NORMAL) != 0u;
    const vec2 diffuseRg = unpackHalf2x16(packed.mHalves.x);
    const vec2 diffuseBRoughness = unpackHalf2x16(packed.mHalves.y);
    const float opacity = unpackHalf2x16(packed.mHalves.z).x;

    Answer answer;
    answer.mRadiance = packed.mRadiance;
    answer.mBounced = packed.mBounced;
    answer.mResponse = SurfaceResponse(hasNormal ? unpackDirection(packed.mNormal) : vec3(0.0),
        vec3(diffuseRg, diffuseBRoughness.x), diffuseBRoughness.y);
    answer.mMotion = unpackHalf2x16(packed.mMotion);
    answer.mBackdropShown = unpackHalf2x16(packed.mFlags).y;
    answer.mOpacity = opacity;
    answer.mPane = (packed.mFlags & ANSWER_PANE) != 0u;
    answer.mWater = (packed.mFlags & ANSWER_WATER) != 0u;

    return answer;
}

#endif
