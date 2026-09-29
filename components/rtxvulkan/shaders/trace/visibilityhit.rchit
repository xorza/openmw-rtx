#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

// `gl_HitTriangleVertexPositionsEXT`, which is the stage's own reading of what `traversal.glsl`
// reads off a query.
#extension GL_EXT_ray_tracing_position_fetch : require

// The one closest-hit shader the trace's hit table names, compiled three times.
//
// **Picked by traversal and not by a branch.** `SceneAcceleration::placeRow` writes each
// instance's shader-table offset from its material kind, so the hardware follows an index to the
// stage for that kind — and the three stages are this module under three settings of the two
// constants below: which albedo `resolve` is allowed to build, and whether the surface is shaded
// as water. One module is how the three cannot come to disagree about a hit the launch can no
// longer see for itself.
//
// **`LAYERED` folds the layer stack's loop and the four tables it walks out of the two stages no
// terrain can reach.** `WATER` picks the water's answer, and a frame with no sea still binds that
// record — an instance's offset is its material's kind, and a scene can hold water the build was
// told to ignore — so `HAS_SEA` is what says whether it shades as water or as the solid it then is.

#include "camera.h"
#include "scene.h"
#include "visibility.h"

#include "lib/bindings.glsl"
#include "lib/frame.glsl"
#include "lib/hitrecord.glsl"
#include "lib/payload.glsl"
#include "lib/random.glsl"
#include "lib/reproject.glsl"
#include "lib/shading.glsl"
#include "lib/traversal.glsl"
#include "lib/variants.glsl"
#include "lib/water.glsl"

/// The two the hit module is specialized on, after the frame's tuple in `variants.glsl`: whether
/// ground that kept its layer stack can reach this stage, and whether a hit is shaded as water.
/// `VisibilityPass` hands each of the three stages its pair.
layout(constant_id = 5) const bool LAYERED = false;
layout(constant_id = 6) const bool WATER = false;

layout(location = RTX_PAYLOAD) rayPayloadInEXT VisibilityPayload packed;
hitAttributeEXT vec2 barycentrics;

/// What this stage's own builtins say the ray found, in the form `resolve` takes.
///
/// **The cone is the eye's the record names**, opened over the distance the stage was handed:
/// `stageCone` says which eye and why.
///
/// @param bary the stage's `hitAttributeEXT`, which cannot be read through a function boundary and
///        so is passed in.
Hit stageHit(vec2 bary)
{
    vec3 corners[3];
    corners[0] = gl_HitTriangleVertexPositionsEXT[0];
    corners[1] = gl_HitTriangleVertexPositionsEXT[1];
    corners[2] = gl_HitTriangleVertexPositionsEXT[2];

    const Cone cone = stageCone();

    return committedHit(uint(gl_InstanceCustomIndexEXT), uint(gl_PrimitiveID), bary, gl_HitTEXT,
        cone.mWidth + cone.mSpread * gl_HitTEXT, corners, gl_ObjectToWorldEXT);
}

/// The pixel this invocation was launched for.
uvec2 stagePixel()
{
    return gl_LaunchIDEXT.xy;
}

/// Whether the launch has still to peel what this stage found: a surface whose resolved opacity is
/// under one, on a record short of the peel's budget.
///
/// **Decided here, because this is where the number is, and only here.** The opacity is a texture
/// read `resolve` has just made — the launch would have to be handed the material row and read it
/// again. What the launch is handed instead is the answer, `Answer::mPane`, and `mOpacity` to
/// composite by.
///
/// **The launch peels `PEEL_LAYERS` of them and the one after that is a solid.** Without that a
/// shader would look at its own opacity, find one more pane, and shade it as a pane however deep the
/// stack went — so a launch that had run out of layers would draw a hole through the world rather
/// than the surface standing in it.
bool peeled(Surface surface)
{
    return isSeenThrough(surface.mOpacity) && record.mLayer < PEEL_LAYERS;
}

/// Fills the payload in for a pane the launch has still to peel.
///
/// **Direct light, the path's end for everything else, and a lamp sequence of its own.**
/// `bounceLight` draws from the pixel and nothing else, so a pane and the wall behind it would
/// bounce off the same numbers — which is the correlation `SEED_LAMPS_PANE` exists to keep out of the
/// direct term, and there is no such seed to hand a bounce. What a pane gets instead is what a
/// surface a water ray finds gets: `pathEnd`, dimmed by one occlusion ray of its own. A faded actor
/// is a stack of these, and drawn with no indirect term at all they stood in a room where nothing
/// bounced near them. One sequence per layer, because a stack of them shades beside itself as well:
/// the record says which layer this is, and `paneSeed` and `paneAmbientSeed` are the sequences that
/// layer draws from.
void answerPane(inout Answer answer, Surface surface)
{
    const uint key = pixelKey(stagePixel());

    answer.mPane = true;
    answer.mOpacity = surface.mOpacity;
    answer.mRadiance
        = shadeAtPathEnd(surface, key + paneAmbientSeed(record.mLayer), key + paneSeed(record.mLayer), PATH_SEEN);
}

/// Fills the payload in for an ordinary lit surface.
void answerSolid(inout Answer answer, Surface surface)
{
    answer.mOpacity = surface.mOpacity;

    // **The colour is replaced and the surface is not.** What these views change is what a pixel is
    // painted with; the guides still describe a surface there, and saying otherwise hands every
    // reader of them a frame with no normals in it — which the wavelet reads as "no surface
    // anywhere".
    if (frame.mShow != SHOW_SHADED)
    {
        if (frame.mShow == SHOW_ALBEDO)
            answer.mRadiance = surface.mAlbedo;
        else if (frame.mShow == SHOW_NORMAL)
            answer.mRadiance = 0.5 + 0.5 * surface.mNormal;
        else if (frame.mShow == SHOW_ROUGHNESS)
            answer.mRadiance = vec3(surface.mRoughness);
        else
            answer.mRadiance = surface.mSpecular;

        answer.mResponse = responseOf(surface);
        return;
    }

    const SeenSolid seen = shadeSolid(surface, stagePixel(), stageCone());
    answer.mRadiance = seen.mDirect;
    answer.mBounced = seen.mBounce;
    answer.mResponse = seen.mResponse;
    answer.mSunlit = seen.mSunlit;
    answer.mSunOpen = seen.mSunOpen > 0.0;
}

/// Fills the payload in for a water surface, and for the ground showing through its last half metre.
///
/// **A pixel of water with no water under it is the ground it stands on, shaded as the ground is.**
/// The surface fades out over the last half metre of depth, and fading toward the bed shaded at the
/// far end of a path — `pathEnd`, the flat ambient — where the dry pixel beside it gathers a real
/// bounce is a waterline drawn as a line between blue and brown however well the surface over it is
/// faded: in fog the ambient is the recorded colour and a bounce finds the sky's. So the bed the dry
/// pixel would have found is traced and shaded the way that pixel is, and the fade mixes the two as
/// one pixel.
///
/// **From a hair short of the surface and not a hair past it.** The waterline is where the ground
/// crosses the plane, so along the last pixel of water the bed lies within the bias of the surface —
/// and a trace that started past it found nothing, kept the whole water, and drew the line it was
/// there to remove as a bright hair. Solids only: nothing solid is nearer than a surface the eye's
/// own trace found first, so what this finds is the bed.
void answerWater(inout Answer answer, Surface surface)
{
    const uvec2 pixel = stagePixel();
    const vec3 origin = gl_WorldRayOriginEXT;
    const vec3 direction = gl_WorldRayDirectionEXT;
    const Cone cone = stageCone();

    answer.mWater = true;

    const WaterShading water = shadeWater(surface, direction, pixel, cone);
    answer.mRadiance = water.mRadiance;
    answer.mResponse = water.mResponse;

    const float shore = water.mShore;
    if (shore >= 1.0)
        return;

    // Drawn, because this is what the eye sees through the water.
    const Surface bed = trace(
        WorldRay(origin, direction), max(surface.mDistance - SHADOW_BIAS, 0.0), cone, solidMask(frame.mRayMask), true);
    if (!bed.mHit)
        return;

    const SeenSolid seen = shadeSolid(bed, pixel, cone);

    // The direct light and the response as a blend, and the bounce whole, since the albedo it is put
    // back against carries the share. The sky's source is the bed's alone, so it takes the bed's
    // share of the blend. The two normals arrive as codes and leave as one, so a shore pixel's is
    // rounded twice — within twice the code's bound, on the few pixels a waterline crosses.
    const vec3 normal = normalize(mix(unpackSurfaceNormal(seen.mResponse.mNormal),
        unpackSurfaceNormal(answer.mResponse.mNormal), shore));
    answer.mRadiance = mix(seen.mDirect, answer.mRadiance, shore);
    answer.mBounced = seen.mBounce;
    answer.mSunlit = seen.mSunlit * (1.0 - shore);
    answer.mSunOpen = seen.mSunOpen > 0.0;
    answer.mResponse = SurfaceResponse(packSurfaceNormal(normal), seen.mResponse.mDiffuse * (1.0 - shore));
}

void main()
{
    Answer answer = noAnswer();

    const Surface surface
        = resolveFor(stageHit(barycentrics), gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT, LAYERED, true);

    const bool pane = !(WATER && HAS_SEA) && peeled(surface);
    if (WATER && HAS_SEA)
        answerWater(answer, surface);
    else if (pane)
        answerPane(answer, surface);
    else
        answerSolid(answer, surface);

    // **Where the surface stood last frame, worked out where the rows already are.** The instance,
    // its mesh and where on the triangle the ray landed are this stage's own, so the launch reads
    // none of them again. A pane has no motion of its own: the pixel keeps the surface behind the
    // stack, and that surface's stage says where it stood.
    if (!pane)
        answer.mMotion = motionOf(stagePixel(), gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT, gl_HitTEXT,
            uint(gl_InstanceCustomIndexEXT), uint(gl_PrimitiveID), barycentrics, gl_ObjectToWorldEXT,
            stageSpread());

    answer.mHit = true;
    answer.mDistance = gl_HitTEXT;
    packed = packAnswer(answer);
}
