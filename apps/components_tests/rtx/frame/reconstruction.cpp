#include <array>
#include <cstdint>
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
                Upscale::Off, ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sUnscaled);
            EXPECT_TRUE(wavelet.mDenoised);
            EXPECT_FALSE(wavelet.mJitter);
            EXPECT_EQ(wavelet.mUpscale, Upscale::Off);

            const Reconstruction raw = Reconstruction::resolve(
                Upscale::Off, ReconstructionRequest{ .mDenoise = false, .mJitter = true }, sUnscaled);
            EXPECT_FALSE(raw.mDenoised) << "which is what a converged reference is built from";
            EXPECT_TRUE(raw.mJitter) << "and jitter is what makes that reference antialiased";

            // **The same request, and an upscaler behind it.** The wavelet runs as it was asked,
            // since an upscaler reconstructs the frame the trace chain composed; the frame jitters
            // because reconstruction across frames of one sample point is reconstruction from one
            // sample.
            const Reconstruction upscaled = Reconstruction::resolve(
                Upscale::Quality, ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sHalved);
            EXPECT_TRUE(upscaled.mDenoised);
            EXPECT_TRUE(upscaled.mJitter);
            EXPECT_NE(upscaled.mJitter, wavelet.mJitter) << "the same request, a different jitter";
            EXPECT_EQ(upscaled.mUpscale, Upscale::Quality);

            const Reconstruction unfiltered = Reconstruction::resolve(
                Upscale::Quality, ReconstructionRequest{ .mDenoise = false, .mJitter = false }, sHalved);
            EXPECT_FALSE(unfiltered.mDenoised) << "an upscaler denoises nothing of its own";
            EXPECT_TRUE(unfiltered.mJitter);
        }

        /// What the upscaler carries with it: how far every texture level moves for the pixel that
        /// is shown rather than the one traced. The noise stays the tile whatever reconstructs.
        TEST(RtxReconstructionTest, theUpscalerDecidesTheLevelBiasAndARunTheNoiseSource)
        {
            // With no upscaler the ratio's term is nought — the traced pixel is the shown one — and
            // the epsilon is the whole of the bias, which is how a test reads a level off this path.
            const Reconstruction wavelet = Reconstruction::resolve(
                Upscale::Off, ReconstructionRequest{ .mDenoise = true, .mLevelEpsilon = -0.5f }, sUnscaled);
            EXPECT_EQ(wavelet.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_FLOAT_EQ(wavelet.mLevelBias, -0.5f);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, ReconstructionRequest{}, sUnscaled).mLevelBias, 0.0f)
                << "and nought where nothing was asked";

            // The bias is log2(render / display), the shown pixel's own level: 1920 over 3840 is
            // exactly minus one; balanced traces 2258 of 3840 and reads log2(0.5880) = -0.7661, which
            // is the number a texture moves by.
            const Reconstruction performance
                = Reconstruction::resolve(Upscale::Performance, ReconstructionRequest{}, sHalved);
            EXPECT_EQ(performance.mNoise, NoiseSource::BlueNoiseTile) << "the upscaler does not choose the noise";
            EXPECT_FLOAT_EQ(performance.mLevelBias, -1.0f);

            const Reconstruction balanced = Reconstruction::resolve(
                Upscale::Balanced, ReconstructionRequest{}, FrameExtents{ .mRenderWidth = 2258, .mOutputWidth = 3840 });
            EXPECT_NEAR(balanced.mLevelBias, -0.7661f, 0.0005f);

            // Native traces every pixel, so its level is the traced one: nought exactly.
            const Reconstruction native = Reconstruction::resolve(Upscale::Native, ReconstructionRequest{}, sUnscaled);
            EXPECT_EQ(native.mLevelBias, 0.0f);

            // The epsilon is added past the ratio, and a request may name the source outright:
            // that is the A/B.
            const Reconstruction tuned = Reconstruction::resolve(Upscale::Performance,
                ReconstructionRequest{ .mNoise = NoiseSource::WhiteHash, .mLevelEpsilon = -0.25f }, sHalved);
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

                const Reconstruction resolved = Reconstruction::resolve(row.mMode, ReconstructionRequest{}, extents);
                EXPECT_EQ(resolved.mJitterPhases, row.mPhases) << sUpscaleNames.name(row.mMode);
            }

            const FrameExtents unscaled = extentsFor(1920, 1080, Upscale::Off);
            EXPECT_EQ(unscaled.mRenderWidth, 1920u);
            EXPECT_EQ(unscaled.mRenderHeight, 1080u);
            EXPECT_EQ(Reconstruction::resolve(Upscale::Off, ReconstructionRequest{}, unscaled).mJitterPhases, 0u)
                << "nothing upscaling cycles nothing";
        }

        /// A picture — a doll, a map tile — is one frame, denoised as one: the wavelet and the
        /// accumulator's pass-through, no jitter since nothing puts it together across frames, the
        /// tile's noise and the texture level its footprint asks. Every field against the rule for
        /// the same request with nothing upscaling, which is what a picture is.
        TEST(RtxReconstructionTest, aPictureIsOneDenoisedFrameWithNothingMovedInsideItsPixel)
        {
            const Reconstruction picture = Reconstruction::forPicture();
            EXPECT_TRUE(picture.mDenoised);
            EXPECT_EQ(picture.mUpscale, Upscale::Off);
            EXPECT_FALSE(picture.mJitter);
            EXPECT_EQ(picture.mJitterPhases, 0u);
            EXPECT_EQ(picture.mNoise, NoiseSource::BlueNoiseTile);
            EXPECT_EQ(picture.mLevelBias, 0.0f);

            const Reconstruction asked = Reconstruction::resolve(
                Upscale::Off, ReconstructionRequest{ .mDenoise = true, .mJitter = false }, sUnscaled);
            EXPECT_EQ(picture.mDenoised, asked.mDenoised);
            EXPECT_EQ(picture.mJitter, asked.mJitter);
            EXPECT_EQ(picture.mLevelBias, asked.mLevelBias);
            EXPECT_NE(picture.mDenoised, Reconstruction{}.mDenoised) << "the default is the raw light";
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
