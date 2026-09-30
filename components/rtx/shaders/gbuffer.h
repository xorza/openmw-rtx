#ifndef OPENMW_COMPONENTS_RTX_SHADERS_GBUFFER_H
#define OPENMW_COMPONENTS_RTX_SHADERS_GBUFFER_H

#include "camera.h"
#include "hosttypes.h"
#include "octahedral.h"
#include "portable.h"
#include "storageformat.h"

// What each channel of the G-buffer is made of, said once for both sides that have to agree.
//
// **A shader's layout qualifier and the format its image was created with are one fact**, and
// written twice they had drifted: the albedo channel moved to half floats and the two shaders that
// declare it went on saying `rgba32f`. What that costs is not a compile error and not a validation
// *error* — the layers report it as a warning, and the warning says "undefined values to the whole
// image, not just the texel being accessed". A whole channel of the frame, silently, on a
// developer's machine only, because a release build has no layers to say anything at all.
//
// **What the backdrop is drawn through is four bytes, because every term of it is a fraction.**
// What is left of the star field or the interface at a pixel is a product of coverages and
// transmittances, and so is what the arms let through beside it, each of them from nought to one by
// construction, so `R8G8B8A8_UNORM` holds the whole range at 1/255 steps — the step a picture's
// alpha is written at anyway. Fog thick enough for that step to show is fog no star is visible
// through. Four megabytes at 1080p against the sixteen a half-float image would take for the same
// four numbers.
//
// **And a motion vector is a half, because a reprojection is now bounded.** It could not be while
// `previousScreen` divided by a distance that approaches nought, which has no bound at all and
// reaches a half float as infinity. `PREVIOUS_SCREEN_REACH` holds it to one screen outside the
// frame either way, so the largest vector a 1920-wide render can carry is 3840 — where a half's
// step is two pixels, on a vector that left the screen twice over. Inside the frame, where a vector
// is read, that step is a sixtieth of a pixel at sixteen and a thousandth at one.
//
// **Its third half is the step in distance**, which a half carries for the same reason: it is never
// longer than the eye and the surface moved in a frame, whatever the surface's distance. A half
// rounds it by 2^-11 of itself, and while the step is shorter than the distance that is a fortieth
// of the `ACCUMULATE_DEPTH` a history is matched within.
//
// Eight bytes a pixel, and 16 MiB of that at 1080p: there is no three-half storage format, and
// the fourth stays nought.
//
// **The surface is one texel of two floats: the normal as a code, and the distance whole.** They are
// what the filters tell two surfaces apart by, and the cascade reads both at every tap — twenty-five
// times a pixel at each of the cascade's levels, the largest read in the frame — so one fetch of eight bytes
// a tap and not two of sixteen. The distance is whole because the plane test measures offsets of a
// fraction of a pixel's footprint at any distance. The normal is `packSurfaceNormal`'s code, twelve
// bits an octahedral axis: 0.06 degrees at the worst against the six degrees of tilt the cascade's
// `pow(dot, 128)` cuts a tap at, so what the rounding moves is a weight's fourth decimal place. A
// float and not a word, because the digest and every reader load the channels as floats, and a
// code under 2^24 is a float exactly.
//
// So each format is one line naming a `storageformat.h` layout, which is both the qualifier the
// shader declares and what the host creates the image as.

// **The radiance channels are the ones with no format here.** How wide they are is a
// run's choice — `Rtx::RadianceWidth` says which run gets which and why — so the host picks
// between `GBUFFER_RADIANCE_SHOWN` and `GBUFFER_RADIANCE_SUMMED` at creation, and every shader that
// reads or writes one declares it with no format at all and lets the load or the store convert.
// `requirements.cpp` asks the device for both halves of that.

#define GBUFFER_RADIANCE_SHOWN STORAGE_RGBA16F
#define GBUFFER_RADIANCE_SUMMED STORAGE_RGBA32F
#define GBUFFER_ALBEDO STORAGE_RGBA16F
#define GBUFFER_SURFACE STORAGE_RG32F
#define GBUFFER_MOTION STORAGE_RGBA16F
#define GBUFFER_LAYER STORAGE_RGBA16F
#define GBUFFER_BACKDROP STORAGE_RGBA8
#define GBUFFER_UPSCALE_MASKS STORAGE_RG8

// Which binding of `SET_CHANNELS` each channel is.
//
// **The trace declares them and `GBuffer` writes them, and neither had a name for a single one.**
// The shader spelled a number in each layout qualifier and the C++ built its layout and its writes
// by walking an initializer list, so the two agreed only for as long as nobody reordered the list —
// which is a change that compiles, runs, and hands every pass the wrong image.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What the trace resolved on its own: direct light, emission, the sky, water and the fog. The
    /// frame, once composed into it (`VisibilityConstants::mComposed`).
    const uint CHANNEL_DIRECT = 0;

    /// The lamps' diffuse light and the one bounce, demodulated — the channel the wavelet filters.
    const uint CHANNEL_INDIRECT = 1;

    /// What the composite multiplies the bounce back in by.
    const uint CHANNEL_ALBEDO = 2;

    /// The shading normal, `packSurfaceNormal`, and the distance from the eye along the pixel's ray:
    /// what a filter compares surfaces by, and where the eye's view ends.
    const uint CHANNEL_SURFACE = 3;

    /// Where things stood on the previous frame's screen, and how far from the previous eye: in
    /// `xy` the step on the screen in pixels, and in `z` how much farther from the previous eye
    /// the surface stood than it stands from this one — a 2.5D motion vector. The distance is what
    /// tells a history the eye walked toward from a history of another surface.
    const uint CHANNEL_MOTION = 4;

    /// How much of the backdrop a pixel still shows, for the pass that draws it, and what the arms
    /// let through, for the puffs' composite.
    const uint CHANNEL_BACKDROP = 5;

    /// The sprites the trace found in front of the surface, kept apart from it and lit where they
    /// stand: their colour, what they let through, and which eye the pixel's ray left — `packPuffs`.
    /// The light is all the composite takes from here. What a puff's shape is, what hides it and
    /// what the cloud shells add are answered along the shown pixel's own ray by
    /// `spritecomposite.rgen`, so no puff goes through the denoiser or an upscaler, and
    /// nothing the composite draws moves with the jitter the traced grid is sampled at.
    const uint CHANNEL_PUFFS = 6;

    /// What the sun, or a moon at night, adds to what the eye sees as though every ray to it got
    /// through, in `rgb` — the albedo, the lobe and the path's transmittance already in — and in `a`
    /// whether they did, one or nought. What the eye sees is the solid it found, or what the water
    /// reflects and what is seen through it, whose `a` is one of their two bits (`mixSplit`). The
    /// one bit a pixel's shadow is, which the shadow denoiser filters in its place: `rgb` is exact
    /// per pixel, so a texture under a penumbra stays sharp. Nought and one wherever nothing split
    /// it off, which no filter reads as a shadow.
    const uint CHANNEL_SUNLIT = 7;

    /// What the lobe of the solid the eye found reflects of its lamps and its one bounce, whole, times
    /// the path's transmittance, in `rgb`, and the lobe's perceptual roughness in `a`: the glossy
    /// light a PBR replacer's surface sends, which the glossy filter takes over time and nothing
    /// takes across the screen. Nought and `SPECULAR_NO_LOBE` wherever there is no specular half,
    /// which is every vanilla surface.
    const uint CHANNEL_SPECULAR = 8;

    /// The roughness `CHANNEL_SPECULAR` holds where there is no lobe: below every roughness, so the
    /// glossy filter tells a surface with nothing to reflect from a lobe whose draw this frame
    /// returned nought.
    const float SPECULAR_NO_LOBE = -1.0f;

    /// What the see-through layers in front of the surface send of the light a path end drew — the
    /// light arriving at each and its lobe's, each times what the layers and the media in front of it
    /// let through and its own opacity — divided by `CHANNEL_PANE_ALBEDO`, in `rgb`: the channel the
    /// pane filter averages over time. A pane is shaded at the end of a path, one occlusion ray, one
    /// lamp and one sun ray a frame, and composited over the frame, so this is as noisy as a bounce
    /// and nothing else takes it. Nought where no layer stands. What a layer glows with is
    /// deterministic, and stays in `CHANNEL_DIRECT`.
    const uint CHANNEL_PANE = 9;

    /// What `CHANNEL_PANE` is multiplied back by: the layers' albedos, each times the same weight,
    /// in `rgb`, and one in a channel where they sum to under `PANE_ALBEDO_FLOOR`. Demodulated for
    /// the reason the bounce is, so texture a history is reprojected across stays sharp.
    const uint CHANNEL_PANE_ALBEDO = 10;

    /// The nearest layer's own surface, as `CHANNEL_SURFACE` holds the solid's — the normal's code,
    /// and the distance along the ray — and its own motion, as `CHANNEL_MOTION` holds the solid's:
    /// what the pane filter's history is matched and reprojected by. `SURFACE_NO_NORMAL` and nought
    /// where no layer stands.
    const uint CHANNEL_PANE_SURFACE = 11;
    const uint CHANNEL_PANE_MOTION = 12;

    /// What the upscaler is told its motion vector does not describe: the share of the pixel's light
    /// whose image moves apart from `CHANNEL_MOTION`, each share times how far apart —
    /// `motionsApart`. In `r` the see-through layers', which move by the nearest layer's own motion:
    /// FSR's reactive mask, which AMD's documentation names for alpha-blended objects. In `g` what
    /// the water's two rays found, which moves by the parallax of an image the waves bend: its
    /// transparency and composition mask, named for ray-traced reflections. Nought wherever the eye
    /// and the world stand still, so no still picture is touched.
    ///
    /// **A byte a mask**, the width AMD's own masks are stored at: a fraction the upscaler blends by.
    const uint CHANNEL_UPSCALE_MASKS = 13;

    /// How many the set declares, which is the last of them and one more.
    const uint CHANNEL_COUNT = 14;

    /// How far apart, in traced pixels, an image and the motion vector its pixel is handed may move
    /// in one frame before the upscaler is told to trust none of that image's history: half a
    /// pixel, where a bilinear fetch of the history has left the texel it belongs to for another.
    /// At the pond a quarter, a half and a whole pixel measured alike, so the fetch's own reach is
    /// what chooses.
    const float MISMOVED_FULL = 0.5f;

    /// The least albedo `CHANNEL_PANE` is divided by, a channel at a time: under it the layers are
    /// black there, what they send is their lobe's alone, and dividing it by nearly nought would
    /// hand the filter a number the albedo channel's halves cannot bring back. A texel's step.
    const float PANE_ALBEDO_FLOOR = 1.0f / 255.0f;

    /// How many steps either side of nought the surface channel holds an octahedral axis at: twelve
    /// bits an axis, so a code is under 2^24 and a float holds it exactly.
    const uint SURFACE_NORMAL_STEPS = 2047u;
    const uint SURFACE_NORMAL_SPAN = 2u * SURFACE_NORMAL_STEPS + 1u;

    /// The code of a pixel with no surface: below every code a normal packs to, and so apart from
    /// all of them.
    const float SURFACE_NO_NORMAL = -1.0f;

    /// A unit normal as the surface channel's code, or `SURFACE_NO_NORMAL` for nought — the sky's and
    /// a ray's that found nothing, which has no direction to fold.
    RTX_SHADER float packSurfaceNormal(vec3 normal)
    {
        if (!(abs(normal[0]) + abs(normal[1]) + abs(normal[2]) > 0.0f))
            return SURFACE_NO_NORMAL;

        const vec2 square = octahedralSquare(normal);
        return float(octahedralStep(square[0], SURFACE_NORMAL_STEPS)
            + octahedralStep(square[1], SURFACE_NORMAL_STEPS) * SURFACE_NORMAL_SPAN);
    }

    /// The unit normal a code stands for, or nought for `SURFACE_NO_NORMAL`. Selected and not
    /// branched: every tap of the cascade asks, and the lanes of a warp land on both.
    RTX_SHADER vec3 unpackSurfaceNormal(float packed)
    {
        const uint code = uint(max(packed, 0.0f));
        const vec3 unit = octahedralUnit(vec2(octahedralCoordinate(code % SURFACE_NORMAL_SPAN, SURFACE_NORMAL_STEPS),
            octahedralCoordinate(code / SURFACE_NORMAL_SPAN, SURFACE_NORMAL_STEPS)));
        return packed >= 0.0f ? unit : vec3(0.0f, 0.0f, 0.0f);
    }

    /// The surface channel's distance with the arms' flag in its sign — whether the trace drew the
    /// pixel on an arm, whose eye every pass that rebuilds the pixel's ray casts it from again.
    /// **In the sign, because a distance never spends it**, and here because the surface channel is
    /// what every such pass reads at each tap already: carried on the puffs' channel, the flag was a
    /// second fetch of eight bytes a tap in the cascade. A float keeps the sign of nought as well.
    RTX_SHADER float packSurfaceDistance(float distance, bool arms)
    {
        return arms ? -distance : distance;
    }

    /// The distance `packSurfaceDistance` packed.
    RTX_SHADER float surfaceDistance(float packed)
    {
        return abs(packed);
    }

#ifdef RTX_HOST
}
#endif

// What the shading language reads and the host does not, for the reason `RTX_SHADER` gives.
#ifndef RTX_HOST

/// Whether the pixel whose packed distance this is was drawn on an arm — `packSurfaceDistance`.
bool surfaceOnArms(float packed)
{
    return (floatBitsToUint(packed) & 0x80000000u) != 0u;
}

/// The eye the trace cast a pixel's ray from: the arms' where it drew the pixel on an arm, the
/// world's everywhere else. Asked by every pass that rebuilds a pixel's ray, so no two of them can
/// rebuild it through different eyes.
///
/// @param packed the pixel's distance as the surface channel holds it.
Camera eyeOfPixel(float packed, Camera world, Camera arms)
{
    return surfaceOnArms(packed) ? arms : world;
}

#endif

#endif
