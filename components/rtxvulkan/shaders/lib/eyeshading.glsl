// The shading of what the eye's ray hit (`visibility.comp`, `panes.comp`): a solid's light, a pane's, or
// the water's rays and the ground under a shore.
//
// The kernel shades and not a closest-hit stage, because the trace is ray queries alone: AMD's Windows
// driver answers no ray query in a pipeline that also calls `traceRayEXT`
// (`docs/research/2026-10-19-amd-windows-driver-faults.md`).
//
// Water shades as water only where the frame has a sea (`HAS_SEA`): a scene can hold water the build was
// told to ignore, which then shades as the solid or the pane it is.

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
#include "lib/pixels.glsl"
#include "lib/reproject.glsl"
#include "lib/shading.glsl"
#include "lib/stagerecord.glsl"
#include "lib/traversal.glsl"
#include "lib/variants.glsl"
#include "lib/water.glsl"

/// Whether the kernel has still to peel what the ray found: a surface whose opacity is under one, on a layer
/// short of the peel's budget.
///
/// Decided here, where the number is, and only here: the opacity is a texture read
/// `layerOpacityOf` has just made, at full precision, and the kernel is handed the answer, `Answer::mPane`.
///
/// The kernel peels `PEEL_LAYERS` of them and the one after that is a solid. Otherwise a shading would
/// find one more pane and shade it as a pane however deep the stack went, and a kernel out of layers would
/// draw a hole through the world.
bool peeled(float opacity, uint layer)
{
    return isSeenThrough(opacity) && layer < PEEL_LAYERS;
}

/// How far `image` moves apart from `surfaceMotion` in a frame, `motionsApart`: where the image stood on
/// the previous frame's screen, through the same reprojection the surface's own took, of a point standing
/// still at its place along the eye's ray — or of the sky's direction, where it stands at infinity.
///
/// @param direction,arms the eye's ray, and the eye it was cast through (`eyeSpread`).
float apartFrom(WaterImage image, vec2 surfaceMotion, uvec2 pixel, vec3 direction, uint arms)
{
    const vec2 moved = image.mOnSky
        ? skyMotionOf(pixel, direction)
        : reprojected(pixel, direction * image.mAway + frame.mCameraMotion, eyeSpread(arms));
    return motionsApart(moved, surfaceMotion);
}

/// What the eye's ray found, as the trace answers it with no surface resolved: the answer the kernel composes.
///
/// A solid is named and not read (`mSolid`): the direct stage resolves it off the hit the trace stores, and
/// lights it and writes what it is in the filter's terms (`storedSolidOf`, `direct.comp`), and the bounce's
/// stage bounces off it (`bounce.comp`).
///
/// Water: one path a field (`WaterRun`), whose surface replaces the water's as the pixel's and is a solid
/// as the eye's is, its light arriving by the path's weight — twice its share of the water's light, where
/// the pixel splits, times what the leg let through — and the later stages resolving the hit it found
/// (`STAGE_WATER_PATH`).
///
/// A walk (`visibility.comp`): at water the start stops at `EYE_WATER`, for the kernel to run the water's walk
/// in a loop of its own, and ends once that walk is whole.
struct EyeRun
{
    /// `RUN_IDLE` once `mAnswer` holds the answer.
    uint mStep;

    /// The direction of the ray that found the surface, which eye cast it — one for the arms' and
    /// nought for the world's — and which layer of the peel it is for.
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

    /// Whether the water path's leg is a column the media stage marches (`WaterRun::mMarches`), and whether its
    /// solid is the mirror's (`WaterRun::mMirror`).
    bool mMarches;
    bool mMirror;
};

const uint EYE_START = 1u;
const uint EYE_WATER = 3u;

/// @param motion where what the ray met stood last frame, `motionOf`: worked out where the eye's ray is
///        answered, so the walk carries three numbers across the shading and not the hit's transforms. A
///        solid's to its triangle, which the direct stage measures again to the texel its relief showed.
EyeRun eyeRunOf(vec3 direction, uint arms, uint layer, bool water, vec3 motion, float distance)
{
    EyeRun run;
    run.mStep = EYE_START;
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
    run.mMirror = false;
    return run;
}

/// @param water the water's walk, whole, for `EYE_WATER`: what the kernel ran for the eye where it
///        stopped at water.
void stepEye(inout EyeRun run, WaterRun water, uvec2 pixel)
{
    if (run.mStep == EYE_START)
    {
        if (run.mWater)
        {
            run.mAnswer.mWater = true;
            run.mStep = EYE_WATER;
            return;
        }

        run.mSolid = STAGE_EYE_SOLID;
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
            // **What the path found is the pixel's surface, a solid**, whose light the direct stage gathers
            // by the path's weight (`EyeRun::mPathWeight`), the leg's own light beside it.
            run.mAnswer.mRadiance = water.mScattered;
            run.mGuideDistance = run.mImage.mAway;
            run.mSolid = STAGE_WATER_PATH;
            run.mMirror = water.mMirror;
        }
    }

    run.mAnswer.mMotion = run.mMotion;
    if (run.mWater && UPSCALE_MASKS)
        run.mAnswer.mMisMoved
            = run.mImage.mShare * apartFrom(run.mImage, run.mMotion.xy, pixel, run.mDirection, run.mArms);

    run.mAnswer.mHit = true;
    run.mAnswer.mDistance = run.mDistance;
    run.mStep = RUN_IDLE;
}

/// A layer the layers' stage peeled (`panes.comp`), `surface` lit as the end of a path, `PathEndLight`, with
/// what was drawn for it as the bounce and the lobe's light, and its own response (`pane`, `seenPaneOf`), which
/// the stage stacks into `CHANNEL_PANE`; `motion` is the layer's own, which the pane filter reprojects its
/// history by, and `distance` how far along the eye's ray it stands.
Answer paneAnswerOf(Surface surface, SeenPane pane, vec3 motion, float distance)
{
    Answer answer = noAnswer();
    answer.mOpacity = surface.mOpacity;
    answer.mLift = liftOf(surface);
    answer.mPane = true;
    answer.mRadiance = pane.mGlow;
    answer.mBounced = pane.mDrawn;
    answer.mSpecular = pane.mSpecular;
    answer.mResponse = pane.mResponse;
    answer.mMotion = motion;
    answer.mHit = true;
    answer.mDistance = distance;
    return answer;
}

#endif
