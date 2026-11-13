#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/shaders/gbuffer.h>

namespace Rtx
{
    namespace
    {
        /// What a frame with nothing upscaling is traced at, which is what it is shown at: the
        /// rule reads the extents under an upscaler alone, and the tests that raise one name
        /// their own.
        constexpr FrameExtents sUnscaled{ .mRenderWidth = 3840, .mOutputWidth = 3840 };

        /// A performance mode, which traces at half the width: exactly minus one level.
        constexpr FrameExtents sHalved{ .mRenderWidth = 1920, .mOutputWidth = 3840 };

        /// What an upscaler and a request decide between them: who denoises, and whether the
        /// sample point moves.
        ///
        /// **Worth a test of its own because it is a function of its inputs and nothing else** — no
        /// device, no scene, no frame.
        TEST(RtxReconstructionTest, anUpscalerKeepsTheDenoiserAskedForAndAlwaysJitters)
        {
            // Nothing upscaling: the two switches mean exactly what they say.
            const Reconstruction wavelet = Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sUnscaled);
            EXPECT_EQ(wavelet.mFilter, LightFilter::Renderer);
            EXPECT_FALSE(wavelet.mJitter);
            EXPECT_EQ(wavelet.mUpscale, Upscale::Off);

            const Reconstruction raw = Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = false, .mJitter = true }, sUnscaled);
            EXPECT_EQ(raw.mFilter, LightFilter::None) << "which is what a converged reference is built from";
            EXPECT_TRUE(raw.mJitter) << "and jitter is what makes that reference antialiased";
            EXPECT_EQ(raw.mSampling.mShadowFloor, Shaders::SHADOW_DRAW_FLOOR)
                << "the default floor where none is named";
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                          ReconstructionRequest{ .mSampling = { .mShadowFloor = 0.25f } }, sUnscaled)
                          .mSampling.mShadowFloor,
                0.25f)
                << "and the one a run names";
            EXPECT_EQ(raw.mSampling.mLampCandidates, Shaders::LAMP_CANDIDATES);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                          ReconstructionRequest{ .mSampling = { .mLampCandidates = 0u } }, sUnscaled)
                          .mSampling.mLampCandidates,
                0u);

            // **The same request, and an upscaler behind it.** The wavelet runs as it was asked,
            // since an upscaler reconstructs the frame the trace chain composed; the frame jitters
            // because reconstruction across frames of one sample point is reconstruction from one
            // sample.
            const Reconstruction upscaled = Reconstruction::resolve(Upscale::Quality, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sHalved);
            EXPECT_EQ(upscaled.mFilter, LightFilter::Renderer);
            EXPECT_TRUE(upscaled.mJitter);
            EXPECT_NE(upscaled.mJitter, wavelet.mJitter) << "the same request, a different jitter";
            EXPECT_EQ(upscaled.mUpscale, Upscale::Quality);

            const Reconstruction unfiltered = Reconstruction::resolve(Upscale::Quality, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = false, .mJitter = false }, sHalved);
            EXPECT_EQ(unfiltered.mFilter, LightFilter::None)
                << "an upscaler that takes a filtered frame filters nothing";
            EXPECT_TRUE(unfiltered.mJitter);
        }

        /// **Who filters the light, cell by cell**: nobody where nothing was asked; the renderer
        /// wherever nothing upscales and under an upscaler that takes a filtered frame; and the
        /// upscaler under one that takes the noisy light, at every upscaling mode — `native` among
        /// them, which upscales nothing and still runs the upscaler.
        TEST(RtxReconstructionTest, theUpscalerFiltersTheLightWhereItTakesItNoisy)
        {
            struct Cell
            {
                Upscale mMode;
                UpscalerInput mInput;
                bool mDenoise;
                LightFilter mFilter;
            };
            constexpr std::array<Cell, 10> sCells{ {
                { Upscale::Off, UpscalerInput::Filtered, false, LightFilter::None },
                { Upscale::Off, UpscalerInput::Filtered, true, LightFilter::Renderer },
                { Upscale::Off, UpscalerInput::Noisy, false, LightFilter::None },
                { Upscale::Off, UpscalerInput::Noisy, true, LightFilter::Renderer },
                { Upscale::Quality, UpscalerInput::Filtered, false, LightFilter::None },
                { Upscale::Quality, UpscalerInput::Filtered, true, LightFilter::Renderer },
                { Upscale::Quality, UpscalerInput::Noisy, true, LightFilter::Upscaler },
                { Upscale::Native, UpscalerInput::Filtered, true, LightFilter::Renderer },
                { Upscale::Native, UpscalerInput::Noisy, true, LightFilter::Upscaler },
                { Upscale::UltraPerformance, UpscalerInput::Noisy, true, LightFilter::Upscaler },
            } };
            for (const Cell& cell : sCells)
            {
                const FrameExtents extents = extentsFor(1920, 1080, cell.mMode);
                const Reconstruction resolved = Reconstruction::resolve(
                    cell.mMode, cell.mInput, ReconstructionRequest{ .mDenoise = cell.mDenoise }, extents);
                EXPECT_EQ(resolved.mFilter, cell.mFilter)
                    << sUpscaleNames.name(cell.mMode) << (cell.mInput == UpscalerInput::Noisy ? " noisy" : " filtered")
                    << (cell.mDenoise ? " asked" : " unasked");
            }

            // **The one rule both a frame and the chain's history read**: the renderer filters
            // wherever nothing upscales, and under a noisy-input upscaler nowhere else.
            for (const Upscale mode : sUpscaleMenu)
            {
                EXPECT_TRUE(rendererFilters(mode, UpscalerInput::Filtered)) << sUpscaleNames.name(mode);
                EXPECT_EQ(rendererFilters(mode, UpscalerInput::Noisy), mode == Upscale::Off)
                    << sUpscaleNames.name(mode);
            }
        }

        /// A noisy-input upscaler cannot run unfiltered: asking it so is the caller's broken contract,
        /// which the harness refuses before any frame (`ToolLine`'s check against the renderer).
        TEST(RtxReconstructionTest, anUnfilteredFrameUnderANoisyInputUpscalerIsABrokenContract)
        {
            Testing::expectAssertDies(
                [] {
                    static_cast<void>(Reconstruction::resolve(Upscale::Quality, UpscalerInput::Noisy,
                        ReconstructionRequest{ .mDenoise = false }, extentsFor(1920, 1080, Upscale::Quality)));
                },
                "an unfiltered frame under an upscaler that filters the light");
        }

        /// **What follows the filter, and what does not**: the tile whoever filters, unless a run names
        /// the hash; the soft edge's dither but where the upscaler filters; no period to the jitter under
        /// a noisy-input upscaler, FSR's at 1280 of 1920 (8 · 1.5² = 18) under the other; the level bias
        /// the ratio's whichever filters, log2(1280 / 1920) = -0.585; and the filter's switches only where
        /// the renderer's filter reads them.
        TEST(RtxReconstructionTest, theDitherTheJitterAndTheSwitchesFollowTheFilter)
        {
            const FrameExtents quality = extentsFor(1920, 1080, Upscale::Quality);
            const ReconstructionRequest turned{ .mFilters = { .mAntilag = false } };
            const Reconstruction noisy
                = Reconstruction::resolve(Upscale::Quality, UpscalerInput::Noisy, turned, quality);
            const Reconstruction filtered
                = Reconstruction::resolve(Upscale::Quality, UpscalerInput::Filtered, turned, quality);

            EXPECT_EQ(noisy.mNoise, NoiseSource::BlueNoiseTile) << "the tile whoever filters, as measured";
            EXPECT_EQ(filtered.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Quality, UpscalerInput::Noisy,
                          ReconstructionRequest{ .mNoise = NoiseSource::WhiteHash }, quality)
                          .mNoise,
                NoiseSource::WhiteHash)
                << "a run that names the hash has it under any filter";

            // **The soft edge's dither where the renderer filters, or nothing does, and the cut where the
            // upscaler does**, each unless a run names the other.
            EXPECT_FALSE(noisy.mSoftEdgeDither);
            EXPECT_TRUE(filtered.mSoftEdgeDither);
            EXPECT_TRUE(Reconstruction::resolve(
                Upscale::Off, UpscalerInput::Noisy, ReconstructionRequest{ .mDenoise = false }, sUnscaled)
                    .mSoftEdgeDither)
                << "nothing filtering, nothing upscaling";
            EXPECT_TRUE(Reconstruction::resolve(
                Upscale::Quality, UpscalerInput::Noisy, ReconstructionRequest{ .mSoftEdgeDither = true }, quality)
                    .mSoftEdgeDither)
                << "named on under the upscaler";
            EXPECT_FALSE(Reconstruction::resolve(
                Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{ .mSoftEdgeDither = false }, sUnscaled)
                    .mSoftEdgeDither)
                << "named off under the renderer";

            EXPECT_EQ(noisy.mJitterPhases, 0u);
            EXPECT_TRUE(noisy.mJitter);
            EXPECT_EQ(filtered.mJitterPhases, 18u);

            EXPECT_NEAR(noisy.mLevelBias, -0.585f, 0.0005f);
            EXPECT_EQ(noisy.mLevelBias, filtered.mLevelBias);

            EXPECT_EQ(noisy.mFilters, sNoFilterSwitches) << "nothing reads the renderer's switches under the upscaler";
            EXPECT_FALSE(filtered.mFilters.mAntilag) << "the renderer's filter reads them as asked";
            EXPECT_TRUE(filtered.mFilters.mHistoryFix);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                          ReconstructionRequest{ .mDenoise = false, .mFilters = { .mAntilag = false } }, sUnscaled)
                          .mFilters,
                sNoFilterSwitches)
                << "nor where nothing filters";
            EXPECT_EQ(Reconstruction::resolve(Upscale::Quality, UpscalerInput::Noisy, ReconstructionRequest{}, quality),
                noisy)
                << "two frames that differ only in a switch nothing reads are one reconstruction";
        }

        /// What the upscaler carries with it: how far every texture level moves for the pixel that
        /// is shown rather than the one traced. The noise stays the tile whatever reconstructs.
        TEST(RtxReconstructionTest, theUpscalerDecidesTheLevelBiasAndARunTheNoiseSource)
        {
            // With no upscaler the ratio's term is nought — the traced pixel is the shown one — and
            // the epsilon is the whole of the bias, which is how a test reads a level off this path.
            const Reconstruction wavelet = Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = true, .mLevelEpsilon = -0.5f }, sUnscaled);
            EXPECT_EQ(wavelet.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_FLOAT_EQ(wavelet.mLevelBias, -0.5f);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                          .mLevelBias,
                0.0f)
                << "and nought where nothing was asked";

            // The bias is log2(render / display), the shown pixel's own level: 1920 over 3840 is
            // exactly minus one; balanced traces 2258 of 3840 and reads log2(0.5880) = -0.7661, which
            // is the number a texture moves by.
            const Reconstruction performance = Reconstruction::resolve(
                Upscale::Performance, UpscalerInput::Filtered, ReconstructionRequest{}, sHalved);
            EXPECT_EQ(performance.mNoise, NoiseSource::BlueNoiseTile) << "the upscaler does not choose the noise";
            EXPECT_FLOAT_EQ(performance.mLevelBias, -1.0f);

            const Reconstruction balanced = Reconstruction::resolve(Upscale::Balanced, UpscalerInput::Filtered,
                ReconstructionRequest{}, FrameExtents{ .mRenderWidth = 2258, .mOutputWidth = 3840 });
            EXPECT_NEAR(balanced.mLevelBias, -0.7661f, 0.0005f);

            // Native traces every pixel, so its level is the traced one: nought exactly.
            const Reconstruction native
                = Reconstruction::resolve(Upscale::Native, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled);
            EXPECT_EQ(native.mLevelBias, 0.0f);

            // The epsilon is added past the ratio, and a request may name the source outright:
            // that is the A/B.
            const Reconstruction tuned = Reconstruction::resolve(Upscale::Performance, UpscalerInput::Filtered,
                ReconstructionRequest{ .mLevelEpsilon = -0.25f, .mNoise = NoiseSource::WhiteHash }, sHalved);
            EXPECT_EQ(tuned.mNoise, NoiseSource::WhiteHash) << "asked for by name";
            EXPECT_FLOAT_EQ(tuned.mLevelBias, -1.25f);
        }

        /// **What FSR traces at and how many phases it cycles, at 1920×1080, by hand.** Each axis is
        /// the output over the mode's ratio, truncated in floats; the phases are `8 * (output /
        /// render)²`, truncated. Balanced: 1920 / 1.7 = 1129.41 and 1080 / 1.7 = 635.29, so 1129×635,
        /// and (1920 / 1129)² × 8 = 23.14, so 23. The phases reach the frame through the
        /// reconstruction, and nothing upscaling has none.
        TEST(RtxReconstructionTest, theUpscalersExtentsAndPhasesAreFsrsArithmetic)
        {
            struct Row
            {
                Upscale mMode;
                std::uint32_t mWidth;
                std::uint32_t mHeight;
                std::uint32_t mPhases;
            };
            constexpr std::array<Row, 5> sTable{ {
                { Upscale::UltraPerformance, 640, 360, 72 },
                { Upscale::Performance, 960, 540, 32 },
                { Upscale::Balanced, 1129, 635, 23 },
                { Upscale::Quality, 1280, 720, 18 },
                { Upscale::Native, 1920, 1080, 8 },
            } };

            for (const Row& row : sTable)
            {
                const FrameExtents extents = extentsFor(1920, 1080, row.mMode);
                EXPECT_EQ(extents.mRenderWidth, row.mWidth) << sUpscaleNames.name(row.mMode);
                EXPECT_EQ(extents.mRenderHeight, row.mHeight) << sUpscaleNames.name(row.mMode);
                EXPECT_EQ(extents.mOutputWidth, 1920u);
                EXPECT_EQ(extents.mOutputHeight, 1080u);
                EXPECT_EQ(jitterPhasesFor(extents.mRenderWidth, extents.mOutputWidth), row.mPhases)
                    << sUpscaleNames.name(row.mMode);

                const Reconstruction resolved
                    = Reconstruction::resolve(row.mMode, UpscalerInput::Filtered, ReconstructionRequest{}, extents);
                EXPECT_EQ(resolved.mJitterPhases, row.mPhases) << sUpscaleNames.name(row.mMode);
            }

            // **The smallest frame traces a pixel in every mode**: the largest ratio is ultra
            // performance's 3, so a side of 3 traces 3 / 3 = 1, and a side of 2 would trace 0.
            static_assert(sSmallestFrameSide == 3);
            for (const Upscale mode : sUpscaleMenu)
            {
                const FrameExtents smallest = extentsFor(sSmallestFrameSide, sSmallestFrameSide, mode);
                EXPECT_GE(smallest.mRenderWidth, 1u) << sUpscaleNames.name(mode);
                EXPECT_GE(smallest.mRenderHeight, 1u) << sUpscaleNames.name(mode);
                EXPECT_TRUE(std::isfinite(
                    Reconstruction::resolve(mode, UpscalerInput::Filtered, ReconstructionRequest{}, smallest)
                        .mLevelBias))
                    << sUpscaleNames.name(mode);
            }

            const FrameExtents unscaled = extentsFor(1920, 1080, Upscale::Off);
            EXPECT_EQ(unscaled.mRenderWidth, 1920u);
            EXPECT_EQ(unscaled.mRenderHeight, 1080u);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, unscaled)
                          .mJitterPhases,
                0u)
                << "nothing upscaling cycles nothing";
        }

        /// A picture — a doll, a map tile — is one frame, denoised as one: the wavelet and the
        /// accumulator's pass-through, no jitter since nothing puts it together across frames, the
        /// tile's noise and the texture level its footprint asks. Every field against the rule for
        /// the same request with nothing upscaling, which is what a picture is.
        TEST(RtxReconstructionTest, aPictureIsOneDenoisedFrameWithNothingMovedInsideItsPixel)
        {
            const Reconstruction picture = Reconstruction::forPicture();
            EXPECT_EQ(picture.mFilter, LightFilter::Renderer);
            EXPECT_EQ(picture.mUpscale, Upscale::Off);
            EXPECT_FALSE(picture.mJitter);
            EXPECT_EQ(picture.mJitterPhases, 0u);
            EXPECT_EQ(picture.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_EQ(picture.mLevelBias, 0.0f);
            EXPECT_FALSE(picture.mSoftEdgeDither) << "nor a frame after it to average a dither with";
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                    .mSoftEdgeDither)
                << "a world's frames are averaged, the upscaler on or off";
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Quality, UpscalerInput::Filtered, ReconstructionRequest{}, sHalved)
                    .mSoftEdgeDither);

            const Reconstruction asked = Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sUnscaled);
            EXPECT_EQ(picture.mFilter, asked.mFilter);
            EXPECT_EQ(picture.mJitter, asked.mJitter);
            EXPECT_EQ(picture.mLevelBias, asked.mLevelBias);
            EXPECT_NE(picture.mFilter, Reconstruction{}.mFilter) << "the default is the raw light";
        }

        /// **The trace composes the frame only where nothing comes after it**: no filter.
        TEST(RtxReconstructionTest, aTraceComposesOnlyWhereNothingFollows)
        {
            for (const bool denoise : { false, true })
                for (const Upscale upscale : { Upscale::Off, Upscale::Quality })
                {
                    const Reconstruction resolved = Reconstruction::resolve(upscale, UpscalerInput::Filtered,
                        ReconstructionRequest{ .mDenoise = denoise }, upscale == Upscale::Off ? sUnscaled : sHalved);
                    const char* const asked = denoise ? "filtered" : "unfiltered";
                    EXPECT_EQ(resolved.mFilter, denoise ? LightFilter::Renderer : LightFilter::None) << asked;
                }

            EXPECT_TRUE(ReconstructionRequest{}.mFilters.mAntilag);
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                    .mFilters.mAntilag);
            EXPECT_FALSE(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mFilters = { .mAntilag = false } }, sUnscaled)
                    .mFilters.mAntilag);
            EXPECT_TRUE(ReconstructionRequest{}.mFilters.mHistoryFix);
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                    .mFilters.mHistoryFix);
            EXPECT_FALSE(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mFilters = { .mHistoryFix = false } }, sUnscaled)
                    .mFilters.mHistoryFix);
            EXPECT_FALSE(Reconstruction::forPicture().mFilters.mHistoryFix);
            EXPECT_TRUE(ReconstructionRequest{}.mFilters.mDualMotion);
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                    .mFilters.mDualMotion);
            EXPECT_FALSE(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mFilters = { .mDualMotion = false } }, sUnscaled)
                    .mFilters.mDualMotion);
            EXPECT_FALSE(Reconstruction::forPicture().mFilters.mDualMotion);
            EXPECT_TRUE(ReconstructionRequest{}.mFilters.mAntiFirefly);
            EXPECT_TRUE(
                Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered, ReconstructionRequest{}, sUnscaled)
                    .mFilters.mAntiFirefly);
            EXPECT_FALSE(Reconstruction::resolve(Upscale::Off, UpscalerInput::Filtered,
                ReconstructionRequest{ .mFilters = { .mAntiFirefly = false } }, sUnscaled)
                    .mFilters.mAntiFirefly);
            EXPECT_FALSE(Reconstruction::forPicture().mFilters.mAntiFirefly);

            // **An unfiltered request keeps what the trace reads and nothing the filters read**: a
            // request with every filter switch turned from its default is the default request once
            // unfiltered, and one switch the trace reads keeps two apart.
            const ReconstructionRequest turned{ .mFilters
                = { .mAntilag = false, .mHistoryFix = false, .mDualMotion = false, .mAntiFirefly = false } };
            EXPECT_FALSE(turned.unfiltered().mDenoise);
            EXPECT_EQ(turned.unfiltered(), ReconstructionRequest{}.unfiltered());
            for (const ReconstructionRequest& traced :
                { ReconstructionRequest{ .mJitter = true }, ReconstructionRequest{ .mNoise = NoiseSource::WhiteHash },
                    ReconstructionRequest{ .mLevelEpsilon = 0.5f } })
                EXPECT_NE(traced.unfiltered(), ReconstructionRequest{}.unfiltered());
        }

        /// The spellings a report and a command line write, and `auto` left to the harness as its
        /// word for no override.
        TEST(RtxReconstructionTest, theNoiseSourcesAreSpelledAsARunWritesThem)
        {
            EXPECT_EQ(sNoiseSourceNames.name(NoiseSource::BlueNoiseTile), "blue-noise");
            EXPECT_EQ(sNoiseSourceNames.name(NoiseSource::WhiteHash), "white-hash");
            EXPECT_EQ(sNoiseSourceNames.named("auto"), std::nullopt)
                << "auto is the harness's word for no override, and not a source";
        }

        /// A menu without DLSS is the whole menu less its last entry, so an interface's labels for
        /// it are the same prefix of its labels; with DLSS it is the whole menu.
        TEST(RtxUpscalerTest, aMenuOffersDlssOnlyWhereItIsAskedTo)
        {
            const std::span<const UpscalerChoice> without = offeredUpscalers(false);
            EXPECT_EQ(std::vector(without.begin(), without.end()),
                (std::vector<UpscalerChoice>{ UpscalerChoice::Auto, UpscalerChoice::Fsr }));
            const std::span<const UpscalerChoice> with = offeredUpscalers(true);
            EXPECT_EQ(std::vector(with.begin(), with.end()),
                (std::vector<UpscalerChoice>{ UpscalerChoice::Auto, UpscalerChoice::Fsr, UpscalerChoice::Dlss }));
        }

        /// The launcher must not save `dlss` for a library that is not installed; that was the
        /// configuration the game refused to start with.
        TEST(RtxUpscalerTest, theLauncherDoesNotKeepDlssWithoutItsLibrary)
        {
            EXPECT_EQ(launcherUpscaler(UpscalerChoice::Dlss, true), UpscalerChoice::Dlss);
            EXPECT_EQ(launcherUpscaler(UpscalerChoice::Dlss, false), UpscalerChoice::Auto);
            EXPECT_EQ(launcherUpscaler(UpscalerChoice::Fsr, false), UpscalerChoice::Fsr);
            EXPECT_EQ(launcherUpscaler(UpscalerChoice::Auto, false), UpscalerChoice::Auto);
        }
    }
}
