#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TONE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TONE_H

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/storageformat.h>

// `<cstddef>` for the `offsetof` the pinned layout below is checked with, last because only the
// host has it.
#ifdef RTX_HOST
#include <cstddef>
#endif

// What the display pass needs. Included verbatim by both sides, for the reason `sky.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `tone.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint TONE_BIND_COLOUR = 0;
    const uint TONE_BIND_TARGET = 1;
    const uint TONE_BIND_BACKDROP = 2;
    const uint TONE_BIND_EXPOSURE = 3;
    const uint TONE_BIND_BLOOM = 4;
    const uint TONE_BIND_SUN_GLARE = 5;
    const uint TONE_BIND_SURFACE = 6;
    const uint TONE_BIND_LIFT = 7;
    const uint TONE_BINDINGS = 8;

/// What the curve writes the picture as: bytes a display understands. `PresentTarget` makes its
/// image in it.
#define TONE_TARGET_FORMAT STORAGE_RGBA8

/// What a summed frame's picture is written as beside it (`PresentTarget::requireDeep`): a mean of many
/// frames falls between the levels a byte holds, and rounded to one it carries half a level of
/// error into every measure taken against it.
#define TONE_DEEP_FORMAT STORAGE_RGBA16

    /// One step of the store, `TONE_TARGET_FORMAT`'s: what `ToneConstants::mDitherStep` is when the
    /// frame dithers.
    const float TONE_DITHER_STEP = 1.0f / 255.0f;

    /// Threads along each edge of the tone pass's workgroup.
    const uint TONE_WORKGROUP = 8;

    /// What stands behind everything the trace drew: the star field, which a frame that fills a
    /// window draws here over the sky, or the interface, which a picture inside it
    /// (`VisibilityConstants::mTransparentBackground`) is laid over. `ToneConstants::mBackdrop`.
    const uint BACKDROP_STARS = 0u;
    const uint BACKDROP_INTERFACE = 1u;

    /// What the display pass is told: the size of the picture, and what is drawn on it that the
    /// trace could not draw.
    ///
    /// **Its own pass rather than the composite's last line, and the split is what upscaling
    /// needs.** An upscaler reconstructs from scene-referred radiance across several frames; a
    /// picture already squeezed through a display curve has had its highlights flattened into each
    /// other, and no amount of reconstruction gets them back. So the curve has to come after
    /// whatever upscales, at that pass's resolution and not the trace's — which is also why the
    /// sky's point sources are drawn here.
    struct ToneConstants
    {
        /// The frame's sprite tile list and what its tiles can meet, `VisibilityConstants::mTables`'s:
        /// with the traced puffs' arms flag, what says where the composite wrote nothing and the
        /// frame's alpha is not the puffs' — `puffsCoverNothing`. First, so the addresses land
        /// eight-aligned on both sides.
        uint64 mSpriteTileList;
        uint64 mSpritePresence;

        /// The array's texel counts, `VisibilityInputs::mTextureTexels`, which say whether the star
        /// field's sheet stands in.
        uint64 mTextureTexels;

        /// The blue-noise tile, `GpuTables::mBlueNoise`, which the dither under the store draws from.
        uint64 mBlueNoise;

        /// The trace's own extent, which is what `Channel::Backdrop` is written at.
        ///
        /// **Two extents because an upscaler stands between them**, and the other is the camera's.
        /// What this pass writes is one pixel of the picture; what it asks about a pixel — how much
        /// of the backdrop is left in front of what is drawn there — was answered at whatever the
        /// trace ran at, and at `performance` that is a quarter as many pixels.
        uint mTracedWidth;
        uint mTracedHeight;

        /// What the backdrop is, `BACKDROP_*`: what this pass draws through what the trace left of
        /// it and the puffs' transmittance in the frame's alpha.
        ///
        /// **One product and two uses.** Over the star field it is how much of each star a pixel
        /// shows, and the picture is opaque. Over the interface it is how much of the window behind
        /// the picture shows through it, so the picture's alpha is one less it: a pane over nothing
        /// covers what it covers, and a puff laid over nothing is there as much as it is anywhere.
        /// **The picture stays premultiplied**, the curve taken over the colour as it is: a flame
        /// over nothing is light with no coverage at all, which only that form can hold, and the
        /// interface lays the picture down in it.
        uint mBackdrop;

        /// The frame's camera at *this* pass's extent, with no jitter — and so the extent the pass
        /// covers.
        ///
        /// **The same basis and a different grid.** `rayAt` divides by the camera's own extent, so a
        /// camera carrying the output's is what turns an output pixel into the ray it shows. The
        /// jitter is the trace's: it moves a sample inside its pixel so an upscaler can accumulate
        /// several, and a pass that draws once at the resolution it is shown at wants the centre.
        Camera mCamera;

        /// How much of the bloom pyramid is left in the picture, and one texel of its finest level.
        ///
        /// **The lens is applied here because everything before this pass is the trace's own
        /// frame.** A veil written back over the radiance image would be a measurement nobody could
        /// hand compute, and would make the composite mean one thing with an upscaler in the frame
        /// and another without one. `BloomPass` builds the pyramid and this spreads its finest
        /// level over the picture — which also saves the full-resolution pass a separate blend
        /// would cost.
        ///
        /// Nought is no lens, which is what a doll and a map tile are drawn with: the pyramid is the
        /// frame's and neither of those is a frame. The shader samples nothing at all where this is
        /// nought, so what is bound there need not be a pyramid.
        float mBloom RTX_ZERO;
        vec2 mBloomTexel RTX_ZERO;

        /// The star field, drawn here rather than by the trace.
        ///
        /// **A point source is what a temporal upscaler removes**: a star one traced pixel wide is
        /// averaged away across frames, and one the traced grid misses is never there. So the field is drawn where it
        /// is shown, and the trace draws the rest of the sky — and hands this pass `Channel::Backdrop`, because a moon,
        /// a deck, a pane, the water and the air all stand between the field and the eye and none of them is here.
        StarField mStars;

        /// The sun glare fader's wash: its colour, and how much of it this frame lays over the
        /// whole picture before the share of the sun the eye could see — `SunGlarePass`, bound
        /// beside the exposure — is multiplied in. `Rtx::SunGlare::amountFor` folds the angle;
        /// `glare.h` says whose wash this is. Nought for a picture inside the interface.
        vec3 mGlareColour;
        float mGlareAmount;

        /// One over `[Video] gamma`, which `displayGamma` raises the finished picture to: after the
        /// glare, which is the picture's as much as the light is, and before the store, so the darks
        /// it lifts are still float and do not band. One for a picture inside the interface, which
        /// keeps the interface's values as the interface drawn over the frame does.
        float mInverseGamma;

        /// What Night-Eye adds to the ambient, in the engine's colour values, as the rasterizer adds
        /// it to every fragment it lights: `0.7 × magnitude / 100`. Laid over the picture in display
        /// values after the curve, by `CHANNEL_LIFT`, so the meter never sees it. Nought for a
        /// picture inside the interface, which the rasterizer lights by a light of its own.
        vec3 mNightEye;

        /// Where `CHANNEL_LIFT` is read for a pixel of the picture, in traced pixels past where the
        /// pixel's centre lands on the traced grid. **The jitter taken back off where an upscaler
        /// reconstructed the picture at the pixels' centres**, since the trace's samples stand at
        /// the jitter inside theirs; and nought where the picture is the trace's own composite,
        /// whose pixels are those samples.
        vec2 mLiftOffset;

        /// The frame's count, `VisibilityConstants::mFrame`, which turns the dither as the trace's
        /// draws turn.
        uint mFrame;

        /// How far the dither reaches either way: `TONE_DITHER_STEP`, or nought for no dither — a
        /// picture inside the interface, a profile that asks for none (`RenderProfile::mDither`),
        /// and the sixteen-bit picture, whose rounding is under anything measured against it and
        /// which a dither would only add noise to. A factor and not a switch, so every lane takes
        /// the one path.
        float mDitherStep;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    // The host rounds the block up to 224, a multiple of eight for the addresses at its head, and the
    // push range covers four bytes past the shader's block that nothing reads.
    static_assert(offsetof(ToneConstants, mDitherStep) + sizeof(float) == 220,
        "ToneConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
