#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_ANSWER_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_ANSWER_GLSL

// What the eye's shading and the sky answer the kernel for one ray (`EyeRun`, `missedAlong`).
//
// **The whole of what the frame's tail needs, and nothing the kernel already holds.** The ray is
// the kernel's own, so its origin and direction stay there; whether it hit and how far it went are
// the answer's to say.
//
// **The answers and not the questions.** Where a surface stood last frame is worked out by the
// shading that found it, which has the instance's row, its mesh and where on the triangle the ray
// landed already in hand — so the answer carries the vector a pixel stores, and not the point for
// the kernel to reproject, which would read the rows a second time.

#include "gbuffer.h"

#include "records.glsl"

/// What the eye's shading or the sky hands back to the kernel, unpacked: the record
/// `EyeRun` and `missedAlong` fill in and the kernel's tail reads.
struct Answer
{
    /// What the surface sends back along the ray, before the pane, the water column, the air and
    /// the sprites the kernel composites in front of it.
    vec3 mRadiance;

    /// A pane's: what its path end drew, whole, `SeenPane::mDrawn`, which the kernel stacks as it
    /// stands. Nought for everything else, whose bounce a stage of its own traces (`bounce.comp`).
    vec3 mBounced;

    /// A pane's lobe light, whole, `SeenPane::mSpecular`, which the kernel composes with the pane's
    /// glow and no filter takes; and the roughness of a solid's lobe (`SolidLook`), whose light the
    /// direct stage gathers, or `SPECULAR_NO_LOBE`.
    vec3 mSpecular;
    float mRoughness;

    /// What Night-Eye's lift adds to what was shaded, per unit of lift: `liftOf` the surface, and
    /// for a water path the surface it found. Nought for the sky.
    vec3 mLift;

    /// What the shading model made of the surface, for the filter and the composite. `noResponse`
    /// where nothing was shaded. A pane's own, which the kernel stacks into the pane's channels; the
    /// pixel's response is the surface's behind the stack.
    SurfaceResponse mResponse;

    /// What `CHANNEL_MOTION` holds for what the pixel shows: `motionOf` for a surface or a pane,
    /// and `skyMotionOf` for the sky, whose distance does not change.
    vec3 mMotion;

    /// The miss only: how much of the backdrop the pixel shows through what the sky drew — the star
    /// field behind a sky, and the interface, whole, behind a picture that has none.
    float mBackdropShown;

    /// The hit only: what share of what was shaded moves apart from `mMotion`, each share times how
    /// far apart — the water's, whose two rays show images the waves bend. What
    /// `CHANNEL_UPSCALE_MASKS` takes, in `g`.
    float mMisMoved;

    /// How much of the surface the ray met is there: what the kernel composites a pane by.
    float mOpacity;

    /// Whether the kernel peels what was shaded — `peeled` in the shading, decided there once.
    bool mPane;

    /// Whether what was shaded is water, which the glare's query counts as nothing that writes
    /// depth.
    bool mWater;

    /// Whether the ray met a surface, and how far along it: the hit's distance, or the ray's own
    /// end where it met nothing.
    bool mHit;
    float mDistance;
};

/// Everything the kernel reads, at what a shader that answered nothing would leave it.
///
/// **What the shading and the sky start from**, because the kernel reads every field whichever
/// answered: the sky writes no response, and a field one of them skipped would otherwise carry
/// whatever the last pixel through that lane put there.
Answer noAnswer()
{
    Answer answer;
    answer.mRadiance = vec3(0.0);
    answer.mBounced = vec3(0.0);
    answer.mSpecular = vec3(0.0);
    answer.mRoughness = SPECULAR_NO_LOBE;
    answer.mLift = vec3(0.0);
    answer.mResponse = noResponse();
    answer.mMotion = vec3(0.0);
    answer.mBackdropShown = 0.0;
    answer.mMisMoved = 0.0;
    answer.mOpacity = 1.0;
    answer.mPane = false;
    answer.mWater = false;
    answer.mHit = false;
    answer.mDistance = 0.0;

    return answer;
}

#endif
