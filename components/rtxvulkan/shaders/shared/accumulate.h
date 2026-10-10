#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_ACCUMULATE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_ACCUMULATE_H

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

#include "atrous.h"

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
// **Halves for the mean**, which is the cascade's first level's (`atrous.h` says why), rounded at
// random at every store; the fill's alpha holds how many frames it is of.
//
// **And the moments as the mean and the deviation of the luminance, not its first two moments.**
// `E[l²] - E[l]²` is a difference of two numbers that are nearly equal once a pixel has settled, and
// a half that rounds each of them loses the whole of what is left; the running variance, `S' = (1 -
// a)(S + a(l - mean)²)`, is the same number in exact arithmetic (Finch 2009, eq. 143) and keeps its
// own precision. Its root is kept, which spans half the variance's exponents, so a half holds it
// where a dim pixel's variance would fall under the least normal half. A mixture of texels is formed
// in full floats, as `S + mean²` summed and the square of the mean's sum taken off.

// **The mean is the cascade's format, by definition and not by agreement**: the cascade's first
// level writes the mean through the one declaration every level writes through, `ATROUS_CHANNEL`,
// and a qualifier that differs from the image's format is undefined values over the whole image.

#define ACCUMULATE_COLOUR ATROUS_CHANNEL
#define ACCUMULATE_SURFACE STORAGE_RGBA16F
#define ACCUMULATE_MOMENTS STORAGE_RG16F

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
        /// The eyes the frame was traced with, which the short history's variance rebuilds each
        /// tap's point through (`HistoryConstants::mEyes`).
        Eyes mEyes;

        /// One where the slow mean is held to the fast one, nought where the run asked for the A/B
        /// without it (`Reconstruction::mAntilag`): a factor, so both runs take one path.
        uint mAntilag;

        /// The frame's number, which the clamp's store in place seeds its rounding with.
        uint mFrame;
    };

    /// Where `accumulateclamp.comp`'s specialization constant sits: `ACCUMULATE_RING`, whether the
    /// slow mean is held under its ring's ceiling (`ringHeldLuminance`), as
    /// `Reconstruction::mAntiFirefly` asks. **A constant and not a factor**, because the ring's square
    /// is the most of what the clamp loads, and a factor of nought paid for all of it.
    const uint ACCUMULATE_CLAMP_SPEC_RING = 0u;
    const uint ACCUMULATE_CLAMP_SPEC_COUNT = 1u;

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

    /// The fill of one channel where the clamp turned the slow mean's bounce `slow`, of fill
    /// `slowFill`, into `held`, and moved the rest of the slow mean toward the fast one by `share`
    /// (`antilagShare`): the share of the bounce the fill is, moved from the slow mean's toward the
    /// fast mean's (`fast`, `fastFill`) by `share`, of the bounce as held. Exactly `slowFill` where
    /// the clamp moved nothing.
    ///
    /// **A share of the bounce, and not the fill moved on its own**, because the fill is a part of
    /// the bounce (`CHANNEL_FILL`) and the composite gives the rest of the bounce the diffuse albedo
    /// and the fill the ambient one. The clamp holds each axis of the bounce to its box, and the fill
    /// moved by the luminance's share alone stood over the bounce in a channel whose chroma the box
    /// held: what the composite then added of the bounce went under nought there. A slow mean of no
    /// bounce has no share of its own, and takes the fast mean's.
    RTX_SHADER float clampedFill(float slow, float slowFill, float fast, float fastFill, float held, float share)
    {
        const float fastShare = fast > 0.0f ? clamp(fastFill / fast, 0.0f, 1.0f) : 0.0f;
        const float slowShare = slow > 0.0f ? clamp(slowFill / slow, 0.0f, 1.0f) : fastShare;
        return slowFill + (held - slow) * slowShare + share * held * (fastShare - slowShare);
    }

    /// What a pass that keeps a history of the frame's surfaces is handed: the accumulator, which
    /// writes the history a level of the wavelet reads, and the glossy filter, the pane filter and
    /// the shadow denoiser's temporal half, which read and keep histories of their own over the same
    /// pixels. One record, because all four are filled from one frame by one rule
    /// (`DenoiseFrame::history`) and hold their histories to a pixel's surface by one
    /// (`heldSurfaceMatches`).
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

        /// What the distances the history holds were multiplied by when they were stored: the
        /// frame's that wrote them, which a held distance is divided by (`DenoiseHistory::
        /// exchangeDistanceScale`). **Not this frame's**: a far plane that moved between the two
        /// frames scaled every held distance by the ratio, and the plane test refused or took the
        /// whole history by it.
        float mHeldDistanceScale;

        /// Where inside its pixel the previous frame sampled, `VisibilityConstants::mPreviousJitter`:
        /// what a history holding one frame's geometry was traced through, which a test that
        /// rebuilds the previous ray needs.
        vec2 mPreviousJitter;

        /// The previous frame's eye, `VisibilityConstants::mPrevious`, and how much wider the arms'
        /// plane is over it, `VisibilityConstants::mArmsSpread`: what a pixel's surface and the
        /// history's texels are rebuilt through, from the eye that saw both, to be held to one
        /// plane. All nought where there was no previous frame.
        Basis mPrevious;
        vec2 mArmsSpread;

        /// The frame's number, which a history kept in halves seeds its store's rounding with
        /// (`roundedToHalf`).
        uint mFrame;
    };

    /// What the accumulator is handed: the history's record, and whether a surface the previous
    /// frame did not see takes the history along its occluder's motion (`occluderMotion`), nought
    /// or one.
    struct AccumulateConstants
    {
        HistoryConstants mHistory;
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

    /// The variance of a luminance whose first and second moments are `first` and `second`.
    RTX_SHADER float momentVariance(float first, float second)
    {
        return max(second - first * first, 0.0f);
    }

    /// The second moment of a luminance kept as its mean and deviation (`ACCUMULATE_MOMENTS`), which
    /// a mixture of texels sums.
    RTX_SHADER float secondMoment(float mean, float deviation)
    {
        return deviation * deviation + mean * mean;
    }

    /// The variance of a slow mean of `frames` frames whose own second moment means nothing yet
    /// (`ACCUMULATE_SETTLED`): that of the moments `first` and `second` averaged over the clamp's
    /// square, raised for a mean of very few frames (`ACCUMULATE_VARIANCE_BOOST`).
    ///
    /// **Measured where the pixel stands, and not a number**, which is ReLAX's spatial variance
    /// estimate (`spatialVarianceEstimationHistoryThreshold`) and SVGF's before it. A constant is a
    /// radiance, and the trace hands the denoiser radiance with no exposure in it: a variance of one
    /// let every tap of a fresh pixel through where the light stands at a hundredth of one, and
    /// refused nearly every one where it stands at a hundred.
    ///
    /// **Of the slow means' moments, as ReLAX's is, and not of the fast means' spread**, so it is a
    /// sample's variance as a settled pixel's own is (`momentVariance`): a fast mean of two frames
    /// spreads half as far, and a pixel's variance halved at its third frame and doubled at its
    /// fifth.
    RTX_SHADER float shortHistoryVariance(float first, float second, float frames)
    {
        return momentVariance(first, second) * max(1.0f, ACCUMULATE_VARIANCE_BOOST / (frames + 1.0f));
    }

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(HistoryConstants) == 228, "HistoryConstants must be scalar-packed on every side");
    static_assert(sizeof(AccumulateConstants) == 232, "AccumulateConstants must be scalar-packed on every side");
    static_assert(
        sizeof(AccumulateClampConstants) == 160, "AccumulateClampConstants must be scalar-packed on every side");
    static_assert(ACCUMULATE_RING_REACH >= ACCUMULATE_CLAMP_REACH && ACCUMULATE_RING_HOLE < ACCUMULATE_RING_REACH,
        "the clamp's square and the ring's hole are read out of the ring's square");
#endif

#ifdef RTX_HOST
}
#endif

#endif
