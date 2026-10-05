#ifndef OPENMW_COMPONENTS_RTX_SHADERS_ACCUMULATE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_ACCUMULATE_H

#include "atrous.h"
#include "camera.h"
#include "hosttypes.h"
#include "look.h"
#include "portable.h"
#include "storageformat.h"

// What the wavelet's temporal half needs. Included verbatim by both sides, for the reason
// `visibility.h` is.

// What each of the three histories is made of, said once for both sides that have to agree.
//
// **The pass's own, and not the G-buffer's.** A channel the trace writes and a history the denoiser
// keeps share nothing but a number of bits, and a history built from a radiance channel's width or
// `GBUFFER_SURFACE` is narrowed silently whenever a channel is narrowed for the trace's sake — with
// the evidence for the history's width lying somewhere else entirely. The paragraph below is that
// evidence.
//
// **Half floats for the surface, because it builds no reference.** What holds the radiance channels
// at full width is an argument about rounding a term before adding it to a thousand others, and a
// normal is compared against a neighbour's.
//
// **Full floats for the mean**, which is the cascade's first level (`atrous.h` says why): a running
// value a half store would round toward nought at every frame.
//
// **And the moments stay full floats whatever the other two do.** `E[l²] - E[l]²` is a difference of
// two numbers that are nearly equal once a pixel has settled, and a format that rounds each of them
// separately loses the whole of what is left.

// **The mean is the cascade's format, by definition and not by agreement**: the cascade's first
// level writes the mean through the one declaration every level writes through, `ATROUS_CHANNEL`,
// and a qualifier that differs from the image's format is undefined values over the whole image.

#define ACCUMULATE_COLOUR ATROUS_CHANNEL
#define ACCUMULATE_SURFACE STORAGE_RGBA16F
#define ACCUMULATE_MOMENTS STORAGE_RGBA32F

// **The fast means of the bounce and the fill in one texel of two words**, each in shared-exponent
// `RGB9E5` (`lib/sharedexponent.glsl`), which rounds to nearest. They are read at four taps and over
// a 5×5 square a frame, and four full floats each made the accumulator and its clamp 0.54 ms of a
// guild's frame where they had been 0.25; what a fast mean needs is to follow the light, which half
// a step, at most a part in 512 of its brightest channel, does not move.
#define ACCUMULATE_FAST STORAGE_RG32UI

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `accumulate.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint ACCUMULATE_BIND_INDIRECT = 0;
    const uint ACCUMULATE_BIND_MOTION = 1;
    const uint ACCUMULATE_BIND_SURFACE = 2;
    const uint ACCUMULATE_BIND_HISTORY_COLOUR = 3;
    const uint ACCUMULATE_BIND_HISTORY_SURFACE = 4;
    const uint ACCUMULATE_BIND_HISTORY_MOMENTS = 5;
    const uint ACCUMULATE_BIND_SURFACE_OUT = 6;
    const uint ACCUMULATE_BIND_MOMENTS_OUT = 7;
    const uint ACCUMULATE_BIND_BLENDED_OUT = 8;
    const uint ACCUMULATE_BIND_FILL = 9;
    const uint ACCUMULATE_BIND_HISTORY_FILL = 10;
    const uint ACCUMULATE_BIND_FILL_BLENDED_OUT = 11;
    const uint ACCUMULATE_BIND_HISTORY_FAST = 12;
    const uint ACCUMULATE_BIND_FAST_OUT = 13;
    const uint ACCUMULATE_BINDINGS = 14;

    /// Where `accumulatesurface.comp`, the accumulator with no bounce to average, binds what it reads
    /// and writes in set 0, and how many there are.
    const uint ACCUMULATE_SURFACE_BIND_SURFACE = 0;
    const uint ACCUMULATE_SURFACE_BIND_SURFACE_OUT = 1;
    const uint ACCUMULATE_SURFACE_BINDINGS = 2;

    /// Threads along each edge of the accumulator's workgroup, and of the clamp's.
    const uint ACCUMULATE_WORKGROUP = 8;

    /// Where `accumulateclamp.comp` binds what it reads and writes in set 0, and how many there are.
    const uint ACCUMULATE_CLAMP_BIND_SURFACE = 0;
    const uint ACCUMULATE_CLAMP_BIND_FAST = 1;
    const uint ACCUMULATE_CLAMP_BIND_BLENDED = 2;
    const uint ACCUMULATE_CLAMP_BIND_FILL_BLENDED = 3;
    const uint ACCUMULATE_CLAMP_BIND_SAMPLED = 4;
    const uint ACCUMULATE_CLAMP_BIND_SAMPLED_FILL = 5;
    const uint ACCUMULATE_CLAMP_BIND_FAST_OUT = 6;
    const uint ACCUMULATE_CLAMP_BIND_MOMENTS = 7;
    const uint ACCUMULATE_CLAMP_BINDINGS = 8;

    /// How far either way of a pixel the clamp's square reaches: ReLAX's 5×5.
    const uint ACCUMULATE_CLAMP_REACH = 2;

    /// What the clamp reads that is not an image.
    struct AccumulateClampConstants
    {
        uint mWidth;
        uint mHeight;

        /// One where the slow mean is held to the fast one, nought where the run asked for the A/B
        /// without it (`Reconstruction::mAntilag`): a factor, so both runs take one path.
        uint mAntilag;

        /// One where the slow mean is held under its ring's ceiling (`ringHeldLuminance`), nought
        /// where the run asked for the A/B without it (`Reconstruction::mAntiFirefly`): a factor, as
        /// `mAntilag` is.
        uint mAntiFirefly;
    };

    /// How far the slow mean `slow` is moved toward the fast one `fast`, as a share of the way:
    /// nought where `slow` stands inside `[low, high]` grown to hold `fast`, and otherwise the
    /// share that brings it to that edge. ReLAX's clamping factor (NVIDIA NRD,
    /// `RELAX_HistoryClamping`): the box grown by the centre's own fast mean, so the slow mean is
    /// never moved past it.
    RTX_SHADER float antilagShare(float slow, float fast, float low, float high)
    {
        const float held = clamp(slow, min(low, fast), max(high, fast));
        return slow == fast ? 0.0f : clamp((held - slow) / (fast - slow), 0.0f, 1.0f);
    }

    /// How far a pixel's two means are pushed on toward the samples' mean around it, as a share of
    /// the way from the fast mean, where the clamp moved the slow one by `share`: `gap` is the
    /// luminance of the two means' difference, and `distance` that of the samples' mean's from the
    /// fast one. ReLAX's acceleration (`RELAX_HistoryClamping`): proportional to the clamp, and never
    /// carrying the fast mean past the samples. The slow mean is pushed by the same step, which can
    /// carry it past them.
    RTX_SHADER float antilagAcceleration(float gap, float share, float distance)
    {
        const float push = ACCUMULATE_ACCELERATION * share * gap;
        return distance > 0.0f ? min(push / distance, 1.0f) : 0.0f;
    }

    /// What a pass that keeps a history of the frame's surfaces is handed: the accumulator, which
    /// writes the history a level of the wavelet reads, the pane filter and the shadow denoiser's
    /// temporal half, which read and keep histories of their own over the same pixels. One record,
    /// because all three are filled from one frame by one rule.
    struct HistoryConstants
    {
        /// The eyes the frame was traced with. **The jitter is why this is here**: the motion vector
        /// is written against the jittered pixel centre the ray was actually aimed at, so undoing
        /// it needs the same offset added back; the arms' eye, for the glossy filter, which keeps
        /// this history too and rebuilds a pixel's ray through the eye that cast it.
        Eyes mEyes;

        /// Non-zero where there is no history to reuse — the first frame, a resize, a door walked
        /// through. Every pixel then starts its count again.
        uint mReset;

        /// What a world distance is multiplied by before `surfaceOut` holds it, which is
        /// `ACCUMULATE_DISTANCE_RANGE` over the frame's far plane.
        ///
        /// **Here rather than in `Camera`, because it is a storage scale and not a depth range.**
        /// `camera.h` keeps `mFar` out on the grounds that a filter has no use for what a depth was
        /// written against, and that still holds — what this pass needs is a number that keeps a
        /// stored distance inside a half's proportional range, and it is only derived from the same
        /// value.
        float mDistanceScale;
    };

    /// What the accumulator is handed: the history's record; the previous frame's eye,
    /// `VisibilityConstants::mPrevious`, which a history taken along an occluder's motion is held to
    /// this pixel's plane through; and whether a surface the previous frame did not see takes the
    /// history that way at all (`occluderMotion`), nought or one.
    struct AccumulateConstants
    {
        HistoryConstants mHistory;
        Basis mPrevious;
        uint mDualMotion;
    };

    /// The luminance a slow mean `lit` keeps under the fast means around it: no more than the mean of
    /// the ring, `sum` over `count` pixels, plus `ACCUMULATE_RING_SPREAD` of its deviations, out of
    /// the sum of their squares `squares`. All of it where the ring holds no surface.
    ///
    /// **A ceiling and not ReBLUR's clamp both ways.** A firefly is bright; a floor would lift a
    /// pixel darker than the ring, a twig in front of a lit wall, toward the wall's light. On the
    /// M[FR] guild's tree the floor moved no figure of `noise --strafe=150` or `--walk=150`, so it
    /// was left out rather than kept for nothing.
    RTX_SHADER float ringHeldLuminance(float lit, float sum, float squares, float count)
    {
        if (!(count > 0.0f))
            return lit;
        const float mean = sum / count;
        return min(lit, mean + ACCUMULATE_RING_SPREAD * sqrt(max(squares / count - mean * mean, 0.0f)));
    }

    /// The variance of a slow mean of `frames` frames whose own second moment means nothing yet
    /// (`ACCUMULATE_SETTLED`): the spread of the fast means over the clamp's square, `deviation` of
    /// their luminance, raised for a mean of very few frames (`ACCUMULATE_VARIANCE_BOOST`).
    ///
    /// **Measured where the pixel stands, and not a number**, which is ReLAX's spatial variance
    /// estimate (`spatialVarianceEstimationHistoryThreshold`) and SVGF's before it. A constant is a
    /// radiance, and the trace hands the denoiser radiance with no exposure in it: a variance of one
    /// let every tap of a fresh pixel through where the light stands at a hundredth of one, and
    /// refused nearly every one where it stands at a hundred.
    RTX_SHADER float shortHistoryVariance(float deviation, float frames)
    {
        return deviation * deviation * max(1.0f, ACCUMULATE_VARIANCE_BOOST / (frames + 1.0f));
    }

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(HistoryConstants) == 144, "HistoryConstants must be scalar-packed on every side");
    static_assert(sizeof(AccumulateConstants) == 192, "AccumulateConstants must be scalar-packed on every side");
    static_assert(
        sizeof(AccumulateClampConstants) == 16, "AccumulateClampConstants must be scalar-packed on every side");
    static_assert(ACCUMULATE_RING_REACH >= ACCUMULATE_CLAMP_REACH && ACCUMULATE_RING_HOLE < ACCUMULATE_RING_REACH,
        "the clamp's square and the ring's hole are read out of the ring's square");
#endif

#ifdef RTX_HOST
}
#endif

#endif
