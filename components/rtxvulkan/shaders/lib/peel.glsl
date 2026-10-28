#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PEEL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PEEL_GLSL

// The eye's walk through what it looks through: the layers it peels, the surface behind them, and the
// media in front of each, for the trace (`visibility.comp`) and the layers' stage (`panes.comp`).

#include "shared/traceconstants.h"
#include "camera.h"
#include "scene.h"
#include "visibility.h"

#include "answer.glsl"
#include "bindings.glsl"
#include "census.glsl"
#include "counts.glsl"
#include "fog.glsl"
#include "frame.glsl"
#include "halfround.glsl"
#include "pixels.glsl"
#include "reproject.glsl"
#include "sky.glsl"
#include "stage.glsl"
#include "traversal.glsl"
#include "underwater.glsl"
#include "variants.glsl"

/// What the media in front of a point along the ray scattered in, and what they let through.
struct MediumAhead
{
    vec3 mScattered;
    vec3 mThrough;
};

/// The media in front of a point `air` along the eye's `ray`, which stands under the water for the
/// first `under` of it (`waterAlong`) and crosses `column` there: the column's light, and past the
/// plane, where the ray rose through it short of `water`, the air charged as a leg's is — the eye's
/// own froxels hold no air in front of a submerged eye (`fogAlong`).
MediumAhead mediumOverColumn(WorldRay ray, float under, float water, float air, WaterColumn column)
{
    const vec4 above = under < water
        ? fogAlongLeg(ray.mFrom + ray.mAlong * under, ray.mAlong, max(air - under, 0.0), under)
        : vec4(0.0, 0.0, 0.0, 1.0);
    return MediumAhead(column.mScattered + column.mTransmittance * above.xyz, column.mTransmittance * above.w);
}

/// The see-through layers peeled off one ray, composited as they are met: what they sent, what they
/// let through, and what the medium in front of the last of them scattered.
struct PaneStack
{
    vec3 mRadiance;
    float mThrough;
    vec3 mScattered;

    /// What was drawn for the layers, `SeenPane`'s drawn light, and their albedos, each by what
    /// reaches the eye of that layer: `CHANNEL_PANE` before it is divided, and `CHANNEL_PANE_ALBEDO`
    /// before the floor. Kept out of `mRadiance`, which holds what they glow with and the air between
    /// them.
    ///
    /// **Their lobes' light is not here but in `mRadiance`, composed unfiltered**: the pane
    /// filter keeps a layer's history whole over every turn of the view, which a reflection does not
    /// survive, and divided by the diffuse albedo it was taken apart by an albedo it is not of. Every
    /// vanilla layer has no lobe, and the upscaler's accumulation takes a replacer's.
    vec3 mDrawn;
    vec3 mAlbedo;

    /// What they glow with and their lobes send, by the same weights: with `mDrawn`, the light that
    /// moves as the layers do, where `mRadiance` has the air between them in it as well.
    vec3 mGlow;

    /// What Night-Eye's lift adds to them by the same weights: `CHANNEL_LIFT`'s layers.
    vec3 mLift;
};

PaneStack noPanes()
{
    return PaneStack(vec3(0.0), 1.0, vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
}

/// One more layer, behind every one already in `stack`: the stretch of medium since the layer in
/// front of it, and then the layer over everything short of it — both behind whatever the layers in
/// front of it left.
void stackLayer(inout PaneStack stack, MediumAhead ahead, Answer layer)
{
    const vec3 reaching = stack.mThrough * ahead.mThrough * layer.mOpacity;
    stack.mDrawn += reaching * layer.mBounced;
    stack.mAlbedo += reaching * layer.mResponse.mDiffuse;
    stack.mGlow += reaching * (layer.mRadiance + layer.mSpecular);
    stack.mLift += reaching * layer.mLift;

    stack.mRadiance
        += stack.mThrough
        * (ahead.mScattered - stack.mScattered + ahead.mThrough * ((layer.mRadiance + layer.mSpecular) * layer.mOpacity));
    stack.mThrough *= 1.0 - layer.mOpacity;
    stack.mScattered = ahead.mScattered;
}

/// The nearest see-through layer's own surface and motion, which the pane filter's history is
/// matched and reprojected by: `CHANNEL_PANE_SURFACE` and `CHANNEL_PANE_MOTION`.
struct NearestPane
{
    float mNormal;

    /// With the arms' flag in its sign, `packSurfaceDistance`: the pane filter rebuilds the layer's
    /// ray through the eye that cast it, as every pass does a solid's.
    float mDistance;

    vec3 mMotion;
};

NearestPane noNearestPane()
{
    return NearestPane(SURFACE_NO_NORMAL, 0.0, vec3(0.0));
}

NearestPane nearestPaneOf(Answer layer, bool arms)
{
    return NearestPane(layer.mResponse.mNormal, packSurfaceDistance(layer.mDistance, arms), layer.mMotion);
}

/// Stores what the layers in front of `at` leave in the pane channels: their drawn light, the world's
/// behind the arms' as their pictures meet, per unit of the albedo that puts it back, that albedo,
/// and the nearest layer's surface and motion. **And what a pixel no layer stands in holds there**,
/// out of empty stacks, which the trace stores at every pixel and the layers' stage over its own
/// tiles. Returns the layers' drawn light.
///
/// **Divided by what puts it back**, so the pane filter averages light and not the texture it lands
/// on — the bounce's demodulation, for the bounce's reason. **Rounded to what `CHANNEL_PANE_ALBEDO`
/// holds before anything is divided by it**, as `specularModulation` is: the light divided by this
/// and the channel the composite multiplies by are one number, so the two meet to the rounding of a
/// product. The floor stands past the least normal half, which the truncation asks.
vec3 storePanes(ivec2 at, uint width, uint swap, PaneStack armsStack, PaneStack panes, NearestPane nearest)
{
    const vec3 paneDrawn = armsStack.mDrawn + armsStack.mThrough * panes.mDrawn;
    const vec3 paneAlbedoSum = armsStack.mAlbedo + armsStack.mThrough * panes.mAlbedo;
    const vec3 paneModulation
        = truncatedToNormalHalf(clamp(paneAlbedoSum, vec3(PANE_ALBEDO_FLOOR), vec3(HALF_LARGEST)));
    RTX_STORE_COUNTED(pane, channelAt(CHANNEL_PANE, at, width, swap), vec4(paneDrawn / paneModulation, 1.0))
    RTX_STORE_COUNTED(paneAlbedo, channelAt(CHANNEL_PANE_ALBEDO, at, width, swap), vec4(paneModulation, 1.0))
    RTX_STORE_COUNTED(paneSurface, channelAt(CHANNEL_PANE_SURFACE, at, width, swap),
        vec4(nearest.mNormal, nearest.mDistance, 0.0, 0.0))
    RTX_STORE_COUNTED(paneMotion, channelAt(CHANNEL_PANE_MOTION, at, width, swap), vec4(nearest.mMotion, 0.0))
    return paneDrawn;
}

/// `stack` over `behind`, the whole path it stands part of the way along — the identity the kernel
/// states where it composites the world's stack. The clamp is what a pair quantised apart by a
/// froxel slice can need and a prefix cannot.
vec3 stackOver(PaneStack stack, vec3 behind)
{
    return stack.mRadiance + stack.mThrough * max(behind - stack.mScattered, vec3(0.0));
}

/// What a ray that met nothing answers: the sky, for the world's eye.
///
/// **Nothing, for the arms' eye. The sky is seen through the world's eye alone**: the arms' eye
/// traces the arms and nothing else, and a ray of it that finds no arm is not shaded — the world's
/// own ray is traced there instead, so whatever reaches the sky reached it through `Eyes::mWorld`,
/// and a pixel beside the arms shades the sky once and not twice.
///
/// **What the backdrop shows through is the sky's to say.** `mBackdropShown` is how much of the
/// star field the sky's own layers left, and the display pass cannot work it out for itself —
/// `backdrop` in `bindings.glsl` says why.
///
/// **Packed and unpacked as every answer is**, so a miss keeps the rounding the channels' halves
/// give a hit.
Answer missedAlong(uvec2 pixel, vec3 origin, vec3 direction, bool shaded)
{
    Answer answer = noAnswer();
    if (shaded)
    {
        // Whatever the answer below: a ray that found water from under it, or a picture's
        // background, reached nothing all the same.
        countMiss();

        // **The sky moves too, and only its turn moves it** — `skyMotionOf` says why storing nothing
        // here is a smear across every camera rotation. Whatever the answer below: the water a ray
        // finds under the surface and a picture's background turn with the eye as the sky does.
        answer.mMotion = vec3(skyMotionOf(pixel, direction), 0.0);

        // **A ray that goes down from under the surface and finds nothing found water, and water is
        // not the sky.** `waterUnbounded` is the whole argument. A picture's background is nothing
        // as well.
        const bool picture = frame.mTransparentBackground != 0u;
        if (!waterUnbounded(false, origin, direction) && !picture)
            answer.mRadiance
                = skyRadiance(origin, direction, pixelBlur(frame.mEyes.mWorld), true, answer.mBackdropShown);

        // **And the interface behind a picture is its backdrop, shown whole**: no sky stands in front
        // of it, so whatever the kernel put in front of this miss is all that covers it.
        answer.mBackdropShown = picture ? 1.0 : answer.mBackdropShown;
    }

    answer.mDistance = frame.mFar;
    return answer;
}

/// The eye's rays a pixel peels its layers along — the arms' first, then the world's, and every
/// layer of the peel behind them — which the trace (`tracePixel`) and the layers' stage
/// (`panes.comp`) both walk with these steps, so the two meet the same layers: the trace shades the
/// surface behind them and weighs them by their opacity alone, and the layers' stage lights the
/// layers and stops at that surface.
struct Peel
{
    /// The eye the next ray is cast through, its ray, and where along it the next one starts.
    ///
    /// **The arms first, through their own eye, and the world only where they are not.** The
    /// rasterizer draws the player's arms under a projection of their own field of view and clears
    /// the depth under them, so they stand in front of everything — a decision about what the
    /// picture is, which `worldMask` says the world's ray keeps out of. A miss of the arms' ray is
    /// not shaded, so a pixel beside the arms pays one traversal on a mask a handful of instances
    /// carry and no second shading of the sky. **To the far plane, as the world's**: a
    /// bound at 512 units, past the arms and the blade, moved the trace by nothing at
    /// `seyda-neen-ship-armed` — 2.82 ms at the median either way, three alternated rounds. The same
    /// rays the wavelet rebuilds come from the same `rayAt`.
    bool mArms;
    vec3 mOrigin;
    vec3 mDirection;
    float mTmin;

    /// Which layer of the peel the next trace is for — one budget for the pixel, whichever eye
    /// spends it. `PEEL_LAYERS` is the one past the last pane the kernel peels, and a surface found
    /// there is drawn as the solid it stands in for whatever its own opacity says (`peeled`).
    uint mLayer;
};

/// The peel's next ray from the eye it is cast through, from the eye itself.
void peelFromEye(inout Peel peel, uvec2 pixel)
{
    const Ray ray = rayAt(eyeOf(uint(peel.mArms)), vec2(pixel));
    peel.mOrigin = frame.mOrigin + ray.mOffset;
    peel.mDirection = ray.mDirection;
    peel.mTmin = 0.0;
}

/// The peel's first ray: the arms' where the frame has arms in it, and the world's where it has none.
Peel peelOf(uvec2 pixel)
{
    Peel peel;
    peel.mArms = frame.mArmsInFrame != 0u;
    peel.mLayer = 0u;
    peelFromEye(peel, pixel);
    return peel;
}

RayAsk peelAsk(Peel peel, uvec2 pixel)
{
    return eyeAsk(peel.mOrigin, peel.mDirection, peel.mTmin, uint(peel.mArms), pixel);
}

/// After a ray that met nothing: whether the peel goes on, as the world's own ray from its own eye
/// where the ray was the arms'. See-through arms are a picture over the world's, never a way into it
/// (`panes.comp`).
bool peelPastArms(inout Peel peel, uvec2 pixel)
{
    if (!peel.mArms)
        return false;
    peel.mArms = false;
    peelFromEye(peel, pixel);
    return true;
}

/// Past a layer met at `distance`, to the next.
///
/// **Just past it along the same line** (`alongPast`), so a layer a unit behind it is met and the
/// layer itself is not. **Held at the clip**, where a layer at the clip would start the ray past its
/// own end: a trace with its `tmin` over its `tmax` is undefined, and one with the two equal is the
/// miss the clip says it is.
void peelPastLayer(inout Peel peel, float distance)
{
    peel.mTmin = min(alongPast(distance), frame.mFar);
    ++peel.mLayer;
}

#endif
