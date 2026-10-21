// The shading of what the eye's ray hit (`visibility.comp`, `panes.comp`): a solid's light, a
// pane's, or the water's rays and the ground under a shore.
//
// **The kernel shades and not a closest-hit stage**, because the trace is ray queries alone: AMD's
// Windows driver answers no ray query in a pipeline that also calls `traceRayEXT`, and the lighting
// traces every one of its rays as a query (`docs/rtx/architecture.md`, "The shaders").
//
// **Water shades as water only where the frame has a sea** (`HAS_SEA`): an instance's kind is its
// material's, and a scene can hold water the build was told to ignore, which then shades as the
// solid or the pane it is.

#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_EYESHADING_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_EYESHADING_GLSL

#include "shared/traceconstants.h"
#include "camera.h"
#include "scene.h"
#include "visibility.h"

#include "lib/bindings.glsl"
#include "lib/frame.glsl"
#include "lib/answer.glsl"
#include "lib/hash.glsl"
#include "lib/reproject.glsl"
#include "lib/shading.glsl"
#include "lib/stage.glsl"
#include "lib/traversal.glsl"
#include "lib/variants.glsl"
#include "lib/water.glsl"

/// What a kernel shades of what the eye meets: the surface behind the layers, which the trace lights,
/// weighing the layers by their opacity alone and measuring the medium in front of the whole path
/// only; or the layers, which `panes.comp` lights each over the medium in front of it, stopping at
/// the surface (`Peel`). **A constant of each kernel**, so each compiles the shading it runs and
/// none of the other's.
const uint PEEL_SHADES_SURFACE = 0u;
const uint PEEL_SHADES_LAYERS = 1u;

/// Whether the kernel has still to peel what the ray found: a surface whose resolved opacity is
/// under one, on a layer short of the peel's budget.
///
/// **Decided here, where the number is, and only here.** The opacity is a texture read `resolve`
/// has just made, at full precision. What the kernel is handed is the answer, `Answer::mPane`, and
/// `mOpacity` to composite by.
///
/// **The kernel peels `PEEL_LAYERS` of them and the one after that is a solid.** Without that a
/// shading would look at its own opacity, find one more pane, and shade it as a pane however deep the
/// stack went — so a kernel that had run out of layers would draw a hole through the world rather
/// than the surface standing in it.
bool peeled(Surface surface, uint layer)
{
    return isSeenThrough(surface.mOpacity) && layer < PEEL_LAYERS;
}

/// How far `image` moves apart from `surfaceMotion` in a frame, `motionsApart`: where the image stood
/// on the previous frame's screen, through the same reprojection the surface's own took, of a point
/// standing still at its place along the eye's ray — or of the sky's direction, where it stands at
/// infinity.
///
/// @param direction,arms the eye's ray, and the eye it was cast through (`eyeSpread`).
float apartFrom(WaterImage image, vec2 surfaceMotion, uvec2 pixel, vec3 direction, uint arms)
{
    const vec2 moved = image.mOnSky
        ? skyMotionOf(pixel, direction)
        : reprojected(pixel, direction * image.mAway + frame.mCameraMotion, eyeSpread(arms));
    return motionsApart(moved, surfaceMotion);
}

/// What the eye's ray found, shaded: the answer the kernel stacks and composes.
///
/// **A pane** is lit as the end of a path, `VisitRun`, with what was drawn for it as the bounce and
/// the lobe's light, and its own response — `SeenPane`, which the kernel stacks into `CHANNEL_PANE`
/// for the pane filter. **Direct light, the path's end for everything else, and a lamp sequence of
/// its own.** The bounce draws from the pixel and nothing else, so a pane and the wall behind it
/// would bounce off the same numbers — which is the correlation `SEED_LAMPS_PANE` exists to keep out
/// of the direct term, and there is no such seed to hand a bounce. What a pane gets instead is what
/// a surface a water ray finds gets: `pathEnd`, dimmed by one occlusion ray of its own. A faded
/// actor is a stack of these, and drawn with no indirect term at all they stood in a room where
/// nothing bounced near them. One sequence per layer, because a stack of them shades beside itself
/// as well: `mLayer` says which layer this is, and `paneSeed` and `paneAmbientSeed` are the sequences
/// that layer draws from.
///
/// **A solid** is what it is in the filter's terms (`solidLookOf`), and its light the direct stage's
/// (`direct.comp`) and its bounce the bounce's (`bounce.comp`), stages of their own that resolve it
/// again off the hit the trace stores (`storedSolidOf`): the walk names the solid, `mSolid`.
///
/// **Water**: one path a field (`WaterRun`), whose surface replaces the water's as the pixel's and is
/// a solid as the eye's is, its light arriving by the path's weight — twice its share of the water's
/// light, where the pixel splits, times what the leg let through — and the later stages resolving
/// the hit it found (`STAGE_WATER_PATH`). A denoiser needs one continuous surface a pixel, and the
/// water's two paths are two (`docs/rtx/architecture.md`, "Every water pixel splits").
/// **The guides place the surface where its image stands** (`WaterImage`), along the eye's ray, and
/// a reflection's normal turned over the plane with it, so the filters weigh it as the surface it
/// looks like; its motion is the water's own, which the waves bend no less than the image.
///
/// **A walk** (`visibility.comp`, `panes.comp`): the start reads the walk's surface, the eye's own,
/// and hands a layer to the walk that lights it; at water it stops at `EYE_WATER`, for the kernel to
/// run the water's walk in a loop of its own, and reads the path's surface once that walk is whole.
struct EyeRun
{
    /// `RUN_IDLE` once `mAnswer` holds the answer.
    uint mStep;

    /// The ray that found the surface, which eye cast it — one for the arms' and nought for the
    /// world's — and which layer of the peel it is for.
    vec3 mOrigin;
    vec3 mDirection;
    uint mArms;
    uint mLayer;

    /// What the eye's ray met: whether it is shaded as water, where it stood last frame, and how far
    /// along the ray it is.
    bool mWater;
    vec3 mMotion;
    float mDistance;

    Answer mAnswer;

    /// Which solid the answer lit, `STAGE_*`: none, the eye's own, or a water path's; and the
    /// weight its light arrives by, the path's (`WaterRun::mWeight`) and one for the eye's — what the
    /// direct and the bounce's stages light and by how much.
    uint mSolid;
    vec3 mPathWeight;

    /// Where the pixel's guides place its surface along the eye's ray (`WaterImage::mAway` for a water
    /// path's), whether its field follows one path of two, and the water path's image.
    float mGuideDistance;
    bool mSplit;
    WaterImage mImage;

    /// Whether the water path's leg is a column the media stage marches (`WaterRun::mMarches`).
    bool mMarches;
};

const uint EYE_START = 1u;
const uint EYE_PANE = 2u;
const uint EYE_WATER = 3u;

/// @param motion where what the ray met stood last frame, `motionOf`: worked out where the eye's
///        ray is answered, so the walk carries three numbers across the shading and not the hit's
///        transforms. A pane's is its own, which the pane filter reprojects its history by; the
///        pixel's motion stays the surface's behind the stack.
EyeRun eyeRunOf(vec3 origin, vec3 direction, uint arms, uint layer, bool water, vec3 motion, float distance)
{
    EyeRun run;
    run.mStep = EYE_START;
    run.mOrigin = origin;
    run.mDirection = direction;
    run.mArms = arms;
    run.mLayer = layer;
    run.mWater = water;
    run.mMotion = motion;
    run.mDistance = distance;
    run.mAnswer = noAnswer();
    run.mSolid = STAGE_NO_SOLID;
    run.mPathWeight = vec3(1.0);
    run.mGuideDistance = distance;
    run.mSplit = false;
    run.mImage = WaterImage(0.0, 0.0, false);
    run.mMarches = false;
    return run;
}

/// @param surface the walk's surface: the eye's own when the walk starts, and what a water path found
///        once its ray is answered.
/// @param shades `PEEL_SHADES_SURFACE` for the surface behind the layers, which the trace shades, or
///        `PEEL_SHADES_LAYERS` for a layer the layers' stage peels (`Peel`).
/// @param water the water's walk, whole, for `EYE_WATER`: what the kernel ran for the eye where it
///        stopped at water.
void stepEye(inout EyeRun run, WaterRun water, inout VisitRun visit, Surface surface, uvec2 pixel, uint shades)
{
    const bool layers = shades == PEEL_SHADES_LAYERS;

    bool shaded = false;
    if (run.mStep == EYE_START)
    {
        if (!layers && run.mWater)
        {
            run.mAnswer.mWater = true;
            run.mStep = EYE_WATER;
            return;
        }

        run.mAnswer.mOpacity = surface.mOpacity;
        run.mAnswer.mLift = liftOf(surface);
        if (layers)
        {
            run.mAnswer.mPane = true;
            visit = paneRunOf(pixelKey(pixel), paneAmbientSeed(run.mLayer), paneSeed(run.mLayer));
            run.mStep = EYE_PANE;
            return;
        }

        // **The colour is replaced and the surface is not.** What these views change is what a pixel
        // is painted with; the guides still describe a surface there, and saying otherwise hands every
        // reader of them a frame with no normals in it — which the wavelet reads as "no surface
        // anywhere".
        if (frame.mShow != SHOW_SHADED)
        {
            if (frame.mShow == SHOW_ALBEDO)
                run.mAnswer.mRadiance = surface.mAlbedo;
            else if (frame.mShow == SHOW_NORMAL)
                run.mAnswer.mRadiance = 0.5 + 0.5 * surface.mNormal;
            else if (frame.mShow == SHOW_ROUGHNESS)
                run.mAnswer.mRadiance = vec3(surface.mRoughness);
            else
                run.mAnswer.mRadiance = surface.mSpecular;

            run.mAnswer.mResponse = responseOf(surface, vec3(1.0));
            shaded = true;
        }
        else
        {
            const SolidLook look = solidLookOf(surface, glossOf(surface));
            run.mAnswer.mResponse = look.mResponse;
            run.mAnswer.mRoughness = look.mRoughness;
            run.mSolid = STAGE_EYE_SOLID;
            shaded = true;
        }
    }
    else if (run.mStep == EYE_PANE)
    {
        const SeenPane seen = visit.mPane;
        run.mAnswer.mRadiance = seen.mGlow;
        run.mAnswer.mBounced = seen.mDrawn;
        run.mAnswer.mSpecular = seen.mSpecular;
        run.mAnswer.mResponse = seen.mResponse;
        shaded = true;
    }
    else
    {
        run.mPathWeight = water.mWeight;
        run.mSplit = water.mSplit;
        run.mImage = water.mImage;
        run.mMarches = water.mMarches;
        if (!water.mFound)
        {
            // **A path that found nothing leaves the water the pixel's surface**: the sky a
            // reflection shows, or the column a refraction sank into, by the path's weight.
            run.mAnswer.mRadiance = water.mMissed * water.mWeight + water.mScattered;
            run.mAnswer.mResponse = water.mResponse;
        }
        else
        {
            // **What the path found is the pixel's surface, a solid**, whose light the direct stage
            // gathers by the path's weight (`EyeRun::mPathWeight`), the leg's own light beside it.
            // **A reflection's surface turned over the plane**, as its image stands: the normal the
            // guides hold of a mirrored point.
            const SolidLook look = solidLookOf(surface, glossOf(surface));
            const vec3 normal = unpackSurfaceNormal(look.mResponse.mNormal);
            run.mAnswer.mRadiance = water.mScattered;
            run.mAnswer.mRoughness = look.mRoughness;
            run.mAnswer.mLift = liftOf(surface);
            run.mAnswer.mResponse = look.mResponse;
            run.mAnswer.mResponse.mNormal
                = water.mMirror ? packSurfaceNormal(vec3(normal.xy, -normal.z)) : look.mResponse.mNormal;
            run.mGuideDistance = run.mImage.mAway;
            run.mSolid = STAGE_WATER_PATH;
        }
        shaded = true;
    }

    if (!shaded)
        return;

    run.mAnswer.mMotion = run.mMotion;
    if (run.mWater)
        run.mAnswer.mMisMoved
            = run.mImage.mShare * apartFrom(run.mImage, run.mMotion.xy, pixel, run.mDirection, run.mArms);

    run.mAnswer.mHit = true;
    run.mAnswer.mDistance = run.mDistance;
    run.mStep = RUN_IDLE;
}

#endif
