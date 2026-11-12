#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_REPROJECT_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_REPROJECT_GLSL

// Where everything in the frame stood on the previous frame's screen.
//
// **Two answers, because two things move differently**: a surface moves with the eye and with itself, the
// sky only when the eye turns. A puff has no answer here, since none is in the frame.

#include "camera.h"

#include "bindings.glsl"
#include "geometry.glsl"

/// How far a point moved between the last frame and this one, in world units.
///
/// **A delta and not the previous position**, which keeps this honest at Morrowind's distances: the two
/// positions are six figures long and nearly equal, so subtracting them on the device loses the step.
vec3 movedBy(GpuInstance instance, vec3 position)
{
    const vec4 rows[3] = instance.mMotion;
    const vec3 was = vec3(dot(rows[0], vec4(position, 1.0)), dot(rows[1], vec4(position, 1.0)),
        dot(rows[2], vec4(position, 1.0)));

    return was - position;
}

/// How far outside the previous screen a reprojection may claim, in screens.
///
/// **A bound, because the divide has none.** A point a hair in front of the previous eye's plane lands on a
/// huge finite coordinate in FP32, handed to an upscaler as a motion vector. One screen of margin on each
/// side keeps the direction it left in and the fact that it left; two screens of motion is past anything an
/// upscaler reuses.
const float PREVIOUS_SCREEN_REACH = 1.0;

/// Where a point standing `was` from the previous eye lands on that eye's screen, and whether it landed
/// anywhere at all.
///
/// **The two are separate fields because a screen coordinate has no spare value**: a negative sentinel is
/// indistinguishable from a point off the left edge, which `reprojected` would read as *this pixel did not
/// move*, a history reuse where a miss is due.
struct PreviousScreen
{
    /// On this frame's grid in the units `rayAt` reads a pixel in (`pixelOfScreen`): nought to the
    /// extent across the previous frame, and outside that where the point left it, bounded by
    /// `PREVIOUS_SCREEN_REACH` screens.
    vec2 mAt;

    /// False where there is no previous frame, and where the point stood behind that eye.
    bool mFound;
};

/// **`screenOf` over the previous basis, and shared for the reason `rayAt` itself is**: a pixel and every
/// froxel of the fog volume reproject through it, and two derivations of one projection can disagree about
/// where the previous frame was.
///
/// **An offset from the eye and never a world position.** `mCameraMotion` is the step between two eyes,
/// differenced on the host where a float still has digits to spare.
///
/// @param spread how much wider the image plane the point projects through is than the eye's own, per axis:
///        one for the eye, and `frame.mArmsSpread` for the arms, since the previous frame carries the eye's
///        basis alone.
PreviousScreen previousScreenThrough(vec3 was, vec2 spread)
{
    // A basis of nothing is what the frame carries where there is no previous frame at all: the first, a
    // resize, a new scene, and any jump a motion vector could not describe.
    const ScreenBasis previous = frame.mPreviousScreen;
    const Screen screen = screenOf(previous, was, spread);
    if (!(dot(previous.mForward, previous.mForward) > 0.0) || !(screen.mAhead > 0.0))
        return PreviousScreen(vec2(0.0), false);

    // On this frame's grid, which the previous frame's channels were written at.
    const vec2 extent = vec2(frame.mEyes.mWorld.mWidth, frame.mEyes.mWorld.mHeight);
    const vec2 at = pixelOfScreen(screen, extent);

    return PreviousScreen(clamp(at, -PREVIOUS_SCREEN_REACH * extent, (1.0 + PREVIOUS_SCREEN_REACH) * extent), true);
}

PreviousScreen previousScreen(vec3 was)
{
    return previousScreenThrough(was, vec2(1.0));
}

/// Where a surface stood on the previous frame's screen, less where it stands on this one, in pixels.
///
/// **Reprojected as an offset and never as a world point**: `direction * distance` is relative to *this* eye
/// and `mCameraMotion` carries it to the last one, so the only large numbers were subtracted on the host,
/// where a float is exact.
///
/// **Both ends are where the surface itself projects**: the near end is the pixel centre plus this frame's
/// jitter, which is where the ray that found the surface was aimed.
vec2 reprojected(uvec2 pixel, vec3 was, vec2 spread)
{
    // **No answer under a parallel projection.** The inverse below divides by the distance along
    // the view axis, which is the perspective divide and not this camera's projection. Nothing that
    // traces one reprojects: a map tile is one frame with no frame before it.
    if (frame.mEyes.mWorld.mOrthographic != 0u)
        return vec2(0.0);

    const PreviousScreen screen = previousScreenThrough(was, spread);
    if (!screen.mFound)
        return vec2(0.0);

    return screen.mAt - (vec2(pixel) + 0.5 + frame.mEyes.mWorld.mJitter);
}

/// How far a point of a deforming mesh moved between the last frame and this one, in world units, past what
/// its instance's rigid motion says: the pose's own step, carried into the world by the instance's basis
/// and into the previous frame by its motion's.
///
/// **The rigid half and the posed half added, and neither subtracted from a world point.** The previous
/// position is `M · toWorld · q_was`, which is `M · point` plus `M_lin · toWorld_lin · (q_was − q_now)`: the
/// first term is `movedBy`'s exact delta and the second a small vector rotated twice, so the six-figure
/// coordinates never meet a small one.
vec3 deformedBy(GpuInstance instance, GpuMesh mesh, uint primitive, vec2 bary)
{
    // Asked before the corners are looked up: most of the frame is a mesh that stands, and the
    // index block the corners come out of is a dependent load a mesh with no pose has no use for.
    if (mesh.mBindOffset == NO_RUN)
        return vec3(0.0);

    const vec3 step = triangleDeformation(mesh, triangleCorners(mesh, primitive), bary);

    // The step is this frame less the last, and what is wanted is where the point was: the other
    // way round. A step of nought comes out as nought, so a body that did not move adds nothing
    // to the rigid half and no lane leaves early to say so.
    const vec3 back = mat3(toWorldOf(instance)) * -step;
    const vec4 rows[3] = instance.mMotion;

    return vec3(dot(rows[0].xyz, back), dot(rows[1].xyz, back), dot(rows[2].xyz, back));
}

/// What `CHANNEL_MOTION` holds for a surface: where it stood on the previous frame's screen less where it
/// stands on this one, in pixels, and how much farther it stood from the previous eye than `distance`,
/// which is the distance its history was measured at.
///
/// **The distance of the offset `reprojected` projects**, so the eye's walk, the instance's motion and the
/// pose's each move it as they move the point.
///
/// @param spread which image plane the surface projects through — the eye's at one, or the arms'
///        at `frame.mArmsSpread` for a surface the arms' ray found.
/// @param primitive which triangle of the instance's mesh the ray landed on, and `bary` where
///        on it: what a deforming mesh needs to say where that point stood last frame. Read
///        only for a mesh that deforms.
vec3 motionOf(uvec2 pixel, vec3 origin, vec3 direction, float distance, uint instance, uint primitive, vec2 bary,
    vec2 spread)
{
    const vec3 point = origin + direction * distance;

    // One load of the row, which both halves of the motion read.
    const GpuInstance placed = instanceAt(instance);
    const GpuMesh mesh = meshAt(placed.mMesh);

    // **The small steps summed before the long offset takes them**, so a point that stood still
    // adds exact noughts and a moving one rounds its steps against each other and not against a
    // distance thousands of units long.
    const vec3 moved
        = frame.mCameraMotion + movedBy(placed, point) + deformedBy(placed, mesh, primitive, bary);

    const vec3 was = direction * distance + moved;

    // **`|a + m| - |a|` as `m · (2a + m) / (|a + m| + |a|)`**, with `a` the offset from this eye and `m` the
    // step: the same number without the cancellation. **Nought where the sum under it is**: a hit at the eye
    // that did not move has `a` and `m` both nought, where the quotient is `0 / 0`.
    const float across = length(was) + distance;
    const float farther = across > 0.0 ? dot(moved, direction * (2.0 * distance) + moved) / across : 0.0;

    return vec3(reprojected(pixel, was, spread), frame.mEyes.mWorld.mOrthographic != 0u ? 0.0 : farther);
}

/// Where the sky a ray found stood on the previous frame's screen, in pixels.
///
/// **Infinitely far, so the eye's own walk does not carry it and its turn is the whole of it.** Storing
/// nothing here is true of walking and false of looking around: a temporal filter would fetch the sky's
/// history from the pixel it already occupies and smear it on every turn, which a field of stars shows and a
/// gradient hides.
///
/// **Through the world's image plane**, because the arms' eye traces the arms alone.
vec2 skyMotionOf(uvec2 pixel, vec3 direction)
{
    return reprojected(pixel, direction, vec2(1.0));
}

/// How far an image's motion stands from the motion vector its pixel hands the upscaler, as what
/// `CHANNEL_UPSCALE_MASKS` takes of it: in proportion, and one from `MISMOVED_FULL` on.
float motionsApart(vec2 image, vec2 handed)
{
    return min(length(image - handed) / MISMOVED_FULL, 1.0);
}

#endif
