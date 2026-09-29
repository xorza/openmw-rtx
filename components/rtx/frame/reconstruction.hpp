#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

#include "frameextents.hpp"
#include "surfaceview.hpp"
#include "upscale.hpp"

namespace Rtx
{
    /// What put a frame's indirect light back together.
    enum class Denoiser
    {
        /// The raw bounce, as the trace wrote it. What a converged reference is built from, because
        /// a thousand filtered frames converge on the filter's opinion rather than on the truth.
        None,

        /// The à-trous wavelet over the indirect channel.
        Wavelet,
    };

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

    /// What the upscaler is built with, decided once per set of targets: whether one runs, and at
    /// what quality.
    struct Upscaling
    {
        Upscale mMode = Upscale::Off;

        bool operator==(const Upscaling& other) const = default;
    };

    /// What a frame asks of the reconstruction, before the upscaler has its say.
    struct ReconstructionRequest
    {
        /// Whether the wavelet was wanted over the indirect channel.
        bool mFilter = true;

        /// Whether the primary ray was wanted moved inside its pixel.
        bool mJitter = false;

        /// Where the trace's draws come from, where a run names a source. Nothing hands the
        /// choice to `resolve`, which keeps the tile; naming one is the A/B.
        std::optional<NoiseSource> mNoise;

        /// What is added to the texture level bias past the ratio the upscaler sets, in levels,
        /// which a run walks on a sign and a book. Nought is the ratio alone. Without an upscaler
        /// the ratio is nought and this is the whole of the bias, which is what lets a test and an
        /// A/B read a level off the unupscaled path.
        float mLevelEpsilon = 0.0f;

        bool operator==(const ReconstructionRequest& other) const = default;
    };

    /// What actually reconstructs a frame, worked out once from what was asked of it, by a
    /// function of its inputs and nothing else: the renderer drives the frame from what this says
    /// and a report prints the same value, where `mFilter && !upscaling` in the middle of the
    /// frame path answered nobody.
    struct Reconstruction
    {
        Denoiser mDenoiser = Denoiser::None;

        /// What upscaled the frame: off wherever nothing did.
        Upscaling mUpscaling;

        /// Whether the primary ray moved inside its pixel this frame. Always under an upscaler:
        /// reconstruction across several frames of one sample point is reconstruction from one
        /// sample.
        bool mJitter = false;

        /// How many phases the jitter cycles through before it repeats, `jitterPhasesFor`: what the
        /// upscaler's history is written against. Nought where nothing upscales, and the sequence
        /// then runs on without a period.
        std::uint32_t mJitterPhases = 0;

        /// Where the trace drew from: the tile, unless the request named a source.
        NoiseSource mNoise = NoiseSource::BlueNoiseTile;

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

        /// Whether the wavelet ran over the indirect channel — one comparison, because the backend
        /// records the accumulator where this holds.
        bool filtered() const { return mDenoiser == Denoiser::Wavelet; }

        /// Whether an upscaler reconstructed the frame.
        bool upscaled() const { return mUpscaling.mMode != Upscale::Off; }

        /// The whole of the rule, and the only copy of it. An upscaler reconstructs the frame the
        /// trace chain composed, so the wavelet runs under one as it runs without.
        ///
        /// @param extents what the frame is traced at and shown at, read only under an upscaler
        ///        and only for its widths, since the pixels are square.
        static Reconstruction resolve(
            const Upscaling& upscaling, const ReconstructionRequest& asked, const FrameExtents& extents)
        {
            const Denoiser denoiser = asked.mFilter ? Denoiser::Wavelet : Denoiser::None;
            const NoiseSource noise = asked.mNoise.value_or(NoiseSource::BlueNoiseTile);
            if (upscaling.mMode == Upscale::Off)
            {
                return Reconstruction{
                    .mDenoiser = denoiser,
                    .mJitter = asked.mJitter,
                    .mNoise = noise,
                    .mLevelBias = asked.mLevelEpsilon,
                };
            }

            return Reconstruction{
                .mDenoiser = denoiser,
                .mUpscaling = upscaling,
                .mJitter = true,
                .mJitterPhases = jitterPhasesFor(extents.mRenderWidth, extents.mOutputWidth),
                .mNoise = noise,
                .mLevelBias = levelBiasOf(extents, asked.mLevelEpsilon),
            };
        }

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

    /// What a frame is scaled by before the display curve: a fixed scale, the scale the frame
    /// before ended on, or neither to measure it off the frame.
    struct ExposureRule
    {
        std::optional<float> mFixed;

        /// Keep what the frame before ended on, measuring nothing and resetting nothing: the frames a
        /// harness compares with a reference hold the exposure the reference measured, so the two
        /// are mapped by one curve and the scale is derived rather than stated. Not with `mFixed`.
        bool mHeld = false;
    };

    /// Everything a run decides once about how the picture is made, in one bag for both hosts,
    /// handed to the backend inside `RendererOptions` and read there. A frame reads what the run
    /// was handed rather than asking the registry per knob per frame, and only a menu moves it
    /// afterwards: `Renderer::setUpscale` changes `mUpscaling`, which `getProfile` then answers.
    struct RenderProfile
    {
        /// What the upscaler is built with.
        Upscaling mUpscaling;

        /// What every frame asks of the reconstruction, before the upscaler has its say —
        /// `Reconstruction::resolve` is the rule. Jitter is off unless something puts the frames
        /// back together; the filter is off for a reference, because a thousand filtered frames
        /// converge on the filter's opinion. A frame may ask otherwise (`FrameOptions`).
        ReconstructionRequest mReconstruction;

        /// How much of the painted lighting to divide out of a texture. Nought hands the trace
        /// Bethesda's textures with their lighting still in them.
        float mDelight = 1.0f;

        /// How many times longer than it is wide a footprint the eye's texture reads may filter
        /// along: the player's `[General] anisotropy`, held to what the device takes. One or less
        /// reads every footprint at the level of its long axis, which is what a profile that says
        /// nothing asks — the level a cone names is then the level read, and a test can measure it.
        std::uint32_t mAnisotropy = 1;

        /// What every pixel is painted with: the light, or a surface input for a picture of the
        /// maps themselves.
        SurfaceView mShow = SurfaceView::Shaded;

        /// What to scale the frame by before the display curve. A picture wants it measured, and a
        /// reference wants it held still. A frame may ask otherwise, like `mReconstruction`.
        ExposureRule mExposure;

        /// How long to hold the queue after every frame's trace, in milliseconds, or nought to
        /// hold it not at all. A held queue keeps the device that far behind the host, so every
        /// frame is recorded over a frame still running: what makes a hazard that needs the
        /// overlap show on the first frame of every run. `check` sets it, and so does the second
        /// leg of `repeat`. What the hold came to on each frame is the zone `sHoldZone` of the
        /// frame's report, which is what the run's `QueueHeld` check reads it back by.
        double mStressOverlapMs = 0.0;

        static constexpr std::string_view sHoldZone = "stress";

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
