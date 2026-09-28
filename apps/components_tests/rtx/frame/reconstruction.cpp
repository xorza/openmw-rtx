#include <optional>

#include <gtest/gtest.h>

#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>

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
            const Reconstruction wavelet = Reconstruction::resolve(
                Upscaling{}, ReconstructionRequest{ .mFilter = true, .mJitter = false }, sUnscaled);
            EXPECT_EQ(wavelet.mDenoiser, Denoiser::Wavelet);
            EXPECT_FALSE(wavelet.mJitter);
            EXPECT_EQ(wavelet.mUpscaling.mMode, Upscale::Off);

            const Reconstruction raw = Reconstruction::resolve(
                Upscaling{}, ReconstructionRequest{ .mFilter = false, .mJitter = true }, sUnscaled);
            EXPECT_EQ(raw.mDenoiser, Denoiser::None) << "which is what a converged reference is built from";
            EXPECT_TRUE(raw.mJitter) << "and jitter is what makes that reference antialiased";

            // **The same request, and an upscaler behind it.** The wavelet runs as it was asked,
            // since an upscaler reconstructs the frame the trace chain composed; the frame jitters
            // because reconstruction across frames of one sample point is reconstruction from one
            // sample.
            const Reconstruction upscaled = Reconstruction::resolve(Upscaling{ .mMode = Upscale::Quality },
                ReconstructionRequest{ .mFilter = true, .mJitter = false }, sHalved);
            EXPECT_EQ(upscaled.mDenoiser, Denoiser::Wavelet);
            EXPECT_TRUE(upscaled.mJitter);
            EXPECT_NE(upscaled.mJitter, wavelet.mJitter) << "the same request, a different jitter";
            EXPECT_EQ(upscaled.mUpscaling.mMode, Upscale::Quality);

            const Reconstruction unfiltered = Reconstruction::resolve(Upscaling{ .mMode = Upscale::Quality },
                ReconstructionRequest{ .mFilter = false, .mJitter = false }, sHalved);
            EXPECT_EQ(unfiltered.mDenoiser, Denoiser::None) << "an upscaler denoises nothing of its own";
            EXPECT_TRUE(unfiltered.mJitter);
        }

        /// What the upscaler carries with it: how far every texture level moves for the pixel that
        /// is shown rather than the one traced. The noise stays the tile whatever reconstructs.
        TEST(RtxReconstructionTest, theUpscalerDecidesTheLevelBiasAndARunTheNoiseSource)
        {
            // With no upscaler the ratio's term is nought — the traced pixel is the shown one — and
            // the epsilon is the whole of the bias, which is how a test reads a level off this path.
            const Reconstruction wavelet = Reconstruction::resolve(
                Upscaling{}, ReconstructionRequest{ .mFilter = true, .mLevelEpsilon = -0.5f }, sUnscaled);
            EXPECT_EQ(wavelet.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_FLOAT_EQ(wavelet.mLevelBias, -0.5f);
            EXPECT_EQ(Reconstruction::resolve(Upscaling{}, ReconstructionRequest{}, sUnscaled).mLevelBias, 0.0f)
                << "and nought where nothing was asked";

            // The bias is log2(render / display): 1920 over 3840 is exactly minus one; balanced
            // traces 2227 of 3840 and reads log2(0.58) = -0.7860, which is the number a texture
            // moves by.
            const Reconstruction performance
                = Reconstruction::resolve(Upscaling{ .mMode = Upscale::Performance }, ReconstructionRequest{}, sHalved);
            EXPECT_EQ(performance.mNoise, NoiseSource::BlueNoiseTile) << "the upscaler does not choose the noise";
            EXPECT_FLOAT_EQ(performance.mLevelBias, -1.0f);

            const Reconstruction balanced = Reconstruction::resolve(Upscaling{ .mMode = Upscale::Balanced },
                ReconstructionRequest{}, FrameExtents{ .mRenderWidth = 2227, .mOutputWidth = 3840 });
            EXPECT_NEAR(balanced.mLevelBias, -0.7860f, 0.0005f);

            // The epsilon is added past the ratio, and a request may name the source outright:
            // that is the A/B.
            const Reconstruction tuned = Reconstruction::resolve(Upscaling{ .mMode = Upscale::Performance },
                ReconstructionRequest{ .mNoise = NoiseSource::WhiteHash, .mLevelEpsilon = -0.25f }, sHalved);
            EXPECT_EQ(tuned.mNoise, NoiseSource::WhiteHash) << "asked for by name";
            EXPECT_FLOAT_EQ(tuned.mLevelBias, -1.25f);
        }

        /// The wavelet ran exactly where it was asked for, upscaled or not.
        TEST(RtxReconstructionTest, aFrameIsFilteredWhereTheWaveletWasAskedFor)
        {
            const Reconstruction wavelet
                = Reconstruction::resolve(Upscaling{}, ReconstructionRequest{ .mFilter = true }, sUnscaled);
            const Reconstruction raw
                = Reconstruction::resolve(Upscaling{}, ReconstructionRequest{ .mFilter = false }, sUnscaled);
            const Reconstruction upscaled = Reconstruction::resolve(
                Upscaling{ .mMode = Upscale::Quality }, ReconstructionRequest{ .mFilter = true }, sHalved);

            EXPECT_TRUE(wavelet.filtered());
            EXPECT_FALSE(raw.filtered()) << "nothing denoised it, which is what a reference is built from";
            EXPECT_TRUE(upscaled.filtered()) << "the upscaler reconstructs the filtered frame";
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
    }
}
