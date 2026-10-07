#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/shaders/gbuffer.h>

#include "frameextents.hpp"
#include "surfaceview.hpp"
#include "upscale.hpp"

namespace Rtx
{
    /// Where the trace's per-pixel draws come from — the shadow ray's place on the source, the
    /// bounce's direction, the fog's and the water's — as against the reservoirs, which step a
    /// hashed counter whichever this says.
    enum class NoiseSource
    {
        /// The blue-noise tile turned by the golden ratio each frame: an arrangement across the
        /// screen, which is what makes one sample per pixel filter well. `Rtx::BlueNoise` says why
        /// the wavelet wants it.
        BlueNoiseTile,

        /// A hashed counter seeded by the pixel, the frame and the stream: independent draws with
        /// no arrangement at all, for the A/B against the tile. Named by a run and never chosen by
        /// `resolve`.
        WhiteHash,
    };

    /// How a `NoiseSource` is spelled on a command line and in a report.
    inline constexpr NamedEnum sNoiseSourceNames{ std::array{
        std::pair{ NoiseSource::BlueNoiseTile, std::string_view("blue-noise") },
        std::pair{ NoiseSource::WhiteHash, std::string_view("white-hash") },
    } };

    /// How the trace draws, which a run names for an A/B and a frame and a picture read alike:
    /// `ReconstructionRequest` asks them and `Reconstruction` carries them as asked.
    struct SamplingSwitches
    {
        /// Where the trace's draws come from: the tile, unless a run names the other, which is the
        /// A/B.
        NoiseSource mNoise = NoiseSource::BlueNoiseTile;

        /// The share of a pixel's light under which a source is never drawn for its shadow bit
        /// (`Shaders::SHADOW_DRAW_FLOOR`), which a run names for the A/B.
        float mShadowFloor = Shaders::SHADOW_DRAW_FLOOR;

        /// How many lamp candidates a point that composes its light draws (`Shaders::LAMP_CANDIDATES`),
        /// nought for every lamp, which a run names for the A/B.
        std::uint32_t mLampCandidates = Shaders::LAMP_CANDIDATES;

        bool operator==(const SamplingSwitches& other) const = default;
    };

    /// How the bounce is filtered past the wavelet, which a run names for an A/B: each read only
    /// where the bounce is filtered, and each at its default where nothing named it.
    struct FilterSwitches
    {
        /// Whether the accumulator holds its slow mean to its fast one (`accumulateclamp.comp`), so a
        /// change of the light on a surface that did not move is followed and not dragged. On unless
        /// a run names it off, which is the A/B.
        bool mAntilag = true;

        /// Whether the wavelet's first level rebuilds a history of a few frames from the surface
        /// around it (`ACCUMULATE_FIX_FRAMES`), so a surface the eye uncovers shows the light beside
        /// it and not one bounce spread into blotches. On unless a run names it off, which is the A/B.
        bool mHistoryFix = true;

        /// Whether a surface the previous frame did not see takes the accumulator's history along the
        /// motion of the occluder that hid it (Zeng et al. 2021's dual motion vector), so what the eye
        /// uncovers starts with the history of the surface beside it. On unless a run names it off,
        /// which is the A/B.
        bool mDualMotion = true;

        /// Whether the accumulator holds a short history of the bounce under the fast means around it
        /// (`ACCUMULATE_RING_FRAMES`), so a bounce that found a small bright thing is not a blotch the
        /// size of a leaf. Off unless a run names it on, which is the A/B: since a lamp's own model
        /// lights nothing by the bounce, it holds no firefly the count can see, and it took a rare
        /// bright bounce's light with it (the glow-lit chamber 1.90 against 2.60 without the ring).
        bool mAntiFirefly = false;

        bool operator==(const FilterSwitches& other) const = default;
    };

    /// Every filter switch off: a picture's, which has no previous frame to hold a history to.
    inline constexpr FilterSwitches sNoFilterSwitches{
        .mAntilag = false, .mHistoryFix = false, .mDualMotion = false, .mAntiFirefly = false
    };

    /// What a frame asks of the reconstruction, before the upscaler has its say.
    struct ReconstructionRequest
    {
        /// Whether the denoisers were wanted over the light.
        bool mDenoise = true;

        /// Whether the primary ray was wanted moved inside its pixel.
        bool mJitter = false;

        /// What is added to the texture level bias past the ratio the upscaler sets, in levels,
        /// which a run walks on a sign and a book. Nought is the ratio alone. Without an upscaler
        /// the ratio is nought and this is the whole of the bias, which is what lets a test and an
        /// A/B read a level off the unupscaled path.
        float mLevelEpsilon = 0.0f;

        SamplingSwitches mSampling{};
        FilterSwitches mFilters{};

        /// This request with no filter, every frame a draw of its own, and each switch
        /// only the bounce's filters read at its default: what an unfiltered frame reads of it, so
        /// two requests whose unfiltered frames trace alike compare equal. An unfiltered frame runs
        /// none of the denoiser's passes, so nothing else reads those switches.
        ReconstructionRequest unfiltered() const
        {
            ReconstructionRequest plain = *this;
            plain.mDenoise = false;
            plain.mFilters = FilterSwitches{};
            return plain;
        }

        bool operator==(const ReconstructionRequest& other) const = default;
    };

    /// What actually reconstructs a frame, worked out once from what was asked of it, by a
    /// function of its inputs and nothing else: the renderer drives the frame from what this says
    /// and a report prints the same value.
    struct Reconstruction
    {
        /// Whether the denoisers put the light back together: the temporal filters, and the wavelet
        /// over the bounce. False leaves the raw light as the trace wrote it, which is what a
        /// converged reference is built from, because a thousand filtered frames converge on the
        /// filter's opinion rather than on the truth.
        bool mDenoised = false;

        /// What upscaled the frame: off wherever nothing did.
        Upscale mUpscale = Upscale::Off;

        /// Whether the primary ray moved inside its pixel this frame. Always under an upscaler:
        /// reconstruction across several frames of one sample point is reconstruction from one
        /// sample.
        bool mJitter = false;

        /// How many phases the jitter cycles through before it repeats, `jitterPhasesFor`: what the
        /// upscaler's history is written against. Nought where nothing upscales, and the sequence
        /// then runs on without a period.
        std::uint32_t mJitterPhases = 0;

        /// What every texture level is offset by, in levels: the shown pixel's cone is narrower
        /// than the traced one by the upscaler's ratio, and a level chosen for the traced pixel
        /// reads every texture that much coarser than the picture shows — `log2(render / display)`,
        /// plus the request's epsilon. Where nothing upscales the epsilon stands on its own.
        ///
        /// **The shown pixel's own level, and not FSR's guide's one finer.** The guide has
        /// `log2(render / display) - 1`, for a frame the upscaler accumulates across jitter phases.
        /// Held against the truth, that level was the sharper one and the noisier: at quality the
        /// Seyda Neen shore's frame stood 0.92 from the reference blurred by 1.5 pixels and moved
        /// 0.60 from its own mean, and at the shown pixel's level 0.75 and 0.54 — the pond 1.55 and
        /// 1.30, and every mode and place of the noise suite no worse on either. Half a level
        /// coarser moved the noise down again and the bias up: 0.80, the pond 1.38.
        float mLevelBias = 0.0f;

        /// How the trace drew: the request's, and the defaults for a picture.
        SamplingSwitches mSampling{};

        /// How the bounce was filtered: the request's, read only where it is filtered, and none
        /// for a picture, where every pixel's history is one frame and none has a settled
        /// neighbour to borrow from.
        FilterSwitches mFilters = sNoFilterSwitches;

        /// Whether frames come after this one to average it with — a world's, which the eye, the
        /// upscaler and the filters each take over time — and not a picture, which stands alone.
        /// **What a draw that is right only on average needs**: the eye meets a soft edge's texels
        /// under the cut by their alpha only where this holds (`VisibilityConstants::mSoftEdgeDither`),
        /// since alone such a draw is stipple on a doll's hair.
        bool mAveraged = false;

        /// Whether an upscaler reconstructed the frame.
        bool upscaled() const { return upscales(mUpscale); }

        /// Whether the trace composes the frame itself (`VisibilityConstants::mComposed`): where
        /// nothing filters the bounce after it.
        bool composedByTrace() const { return !mDenoised; }

        /// The whole of the rule, and the only copy of it. An upscaler reconstructs the frame the
        /// trace chain composed, so the wavelet runs under one as it runs without.
        ///
        /// @param extents what the frame is traced at and shown at, read only under an upscaler
        ///        and only for its widths, since the pixels are square.
        static Reconstruction resolve(
            const Upscale upscale, const ReconstructionRequest& asked, const FrameExtents& extents)
        {
            const bool upscaled = upscales(upscale);
            return Reconstruction{
                .mDenoised = asked.mDenoise,
                .mUpscale = upscale,
                .mJitter = upscaled || asked.mJitter,
                .mJitterPhases = upscaled ? jitterPhasesFor(extents.mRenderWidth, extents.mOutputWidth) : 0u,
                .mLevelBias = upscaled ? levelBiasOf(extents, asked.mLevelEpsilon) : asked.mLevelEpsilon,
                .mSampling = asked.mSampling,
                .mFilters = asked.mFilters,
                .mAveraged = true,
            };
        }

        /// What reconstructs a doll or a map tile: one frame with nothing before it and nothing to
        /// put it together across frames, so denoised as a single frame is, with no jitter, the
        /// tile's noise, no level bias and no draw that is right only on average.
        static Reconstruction forPicture() { return Reconstruction{ .mDenoised = true }; }

    private:
        /// The ratio's levels, and the epsilon the request adds.
        static float levelBiasOf(const FrameExtents& extents, const float epsilon)
        {
            assert(extents.mRenderWidth > 0 && extents.mOutputWidth > 0 && "an upscaler with no extents to bias by");

            return std::log2(static_cast<float>(extents.mRenderWidth) / static_cast<float>(extents.mOutputWidth))
                + epsilon;
        }
    };

    /// How wide the radiance the trace writes is stored: `direct`, `indirect`, `sunlit`,
    /// `specular` and the composite's own frame.
    ///
    /// **Full floats where a reference sums them, and half floats where a frame is shown.** A
    /// reference is a sum of a thousand frames, and rounding every term before adding it only
    /// averages away if the error is random, which it is not — the direct light is all but
    /// identical from frame to frame and the sampler is a low-discrepancy sequence, so in halves
    /// the converged mean of a flat surface comes out low by more than a test's tolerance. A frame
    /// that is shown is never summed: the peak linear radiance a frame of this game reaches is
    /// under nine, which a half carries with four orders of magnitude to spare at a step finer than
    /// the display's — and sixteen bytes a pixel written three times and read six a frame is what
    /// the full width costs a picture that cannot tell.
    enum class RadianceWidth
    {
        Shown,
        Summed,
    };

    /// Where a measured eye stands on a frame with no past: a load, a cut, a new world.
    enum class EyeStart
    {
        /// At a bright day's exposure (`Shaders::EXPOSURE_DAY`), opening toward what the frames
        /// after measure: the game's, whose world arrives over the frames after a load, so a
        /// picture goes from dark to normal and never from bright to normal.
        Day,

        /// At what the frame measures, outright: a measured run's, whose every stop starts with no
        /// past and warms up for less time than an eye takes to open from a day in a room.
        Settled,
    };

    /// The scale measured off the frame, as the eye adapts.
    struct MeasuredExposure
    {
        EyeStart mStart = EyeStart::Day;

        bool operator==(const MeasuredExposure&) const = default;
    };

    /// A scale stated, which nothing measures.
    struct FixedExposure
    {
        float mScale = 1.0f;

        bool operator==(const FixedExposure&) const = default;
    };

    /// What the frame before ended on, measuring nothing and resetting nothing: the frames a harness
    /// compares with a reference hold the exposure the reference measured, so the two are mapped by
    /// one curve and the scale is derived rather than stated.
    struct HeldExposure
    {
        bool operator==(const HeldExposure&) const = default;
    };

    /// What a frame is scaled by before the display curve: one of the three, so no rule says two of
    /// them at once.
    using ExposureRule = std::variant<MeasuredExposure, FixedExposure, HeldExposure>;

    /// Everything a run decides once about how the picture is made, in one bag for both hosts,
    /// handed to the backend inside `RendererOptions` and read there. A frame reads what the run
    /// was handed rather than asking the registry per knob per frame, and only a menu moves it
    /// afterwards: `Renderer::setUpscale` changes `mUpscale`, which `getProfile` then answers.
    struct RenderProfile
    {
        /// What the upscaler is built with, decided once per set of targets: whether one runs, and
        /// at what quality.
        Upscale mUpscale = Upscale::Off;

        /// What every frame asks of the reconstruction, before the upscaler has its say —
        /// `Reconstruction::resolve` is the rule. Jitter is off unless something puts the frames
        /// back together; the filter is off for a reference, because a thousand filtered frames
        /// converge on the filter's opinion. A frame may ask otherwise (`FrameOptions`).
        ReconstructionRequest mReconstruction{};

        /// How much of the painted lighting to divide out of a texture. Nought hands the trace
        /// Bethesda's textures with their lighting still in them.
        float mDelight = 1.0f;

        /// How many times longer than it is wide a footprint the eye's texture reads may filter
        /// along: the player's `[General] anisotropy`, held to what the device takes. One or less
        /// reads every footprint at the level of its long axis, which is what a profile that says
        /// nothing asks — the level a cone names is then the level read, and a test can measure it.
        std::uint32_t mAnisotropy = 1;

        /// The player's `[Video] gamma`, which the frame's picture is raised to one over
        /// (`ToneConstants::mInverseGamma`). One leaves the picture as the curve wrote it, which is
        /// what a profile that says nothing asks, and so what a measured run draws whatever the
        /// player chose.
        float mGamma = 1.0f;

        /// Whether the display curve dithers the frame before its eight-bit store
        /// (`ToneConstants::mDitherStep`), which a player's frame does. Off where a test reads the
        /// curve's bytes exactly, as it fixes the exposure: a dithered byte is the curve's only to
        /// within a step. A frame may ask otherwise (`FrameOptions`).
        bool mDither = true;

        /// Whether an environment map's sheet is part of the colour the light falls on, as
        /// `[Shaders] apply lighting to environment maps` asks the rasterizer, rather than light of
        /// its own past it — `VisibilityConstants::mLitEnvironmentMaps`. Off, the setting's default.
        bool mLitEnvironmentMaps = false;

        /// What every pixel is painted with: the light, or a surface input for a picture of the
        /// maps themselves.
        SurfaceView mShow = SurfaceView::Shaded;

        /// What to scale the frame by before the display curve. A picture wants it measured, and a
        /// reference wants it held still. A frame may ask otherwise, like `mReconstruction`.
        ExposureRule mExposure{};

        /// How long to hold the queue after every frame's trace, in milliseconds, or nought to
        /// hold it not at all. A held queue keeps the device that far behind the host, so every
        /// frame is recorded over a frame still running: what makes a hazard that needs the
        /// overlap show on the first frame of every run. `check` sets it, and so does the second
        /// leg of `repeat`. What the hold came to on each frame is the zone `FrameZone::Stress` of
        /// the frame's report, which is what the run's `QueueHeld` check reads it back by.
        double mStressOverlapMs = 0.0;

        /// Whether the trace keeps a launch per tuple of the frame's facts — the sun, the moons,
        /// the sea, `VisibilityVariant` — or one launch that carries every case. The picture is
        /// the same either way, by the argument `lib/variants.glsl` makes; what differs is the
        /// trace's time in a room and how many launches the driver compiles, sixteen against two.
        /// Off is an experiment — the harness's `--variants=false` — and not a setting.
        bool mSpecializeLaunches = true;

        /// How wide the radiance channels are stored, which `RadianceWidth` says is a question of
        /// whether a run sums its frames or shows them. The reference's width unless a run says
        /// it only shows its frames, so that a run that forgot to say is exact rather than fast.
        RadianceWidth mRadianceWidth = RadianceWidth::Summed;
    };
}
