#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/compare.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/png.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// A picture of one flat colour, so a test can then move exactly the pixels it means to.
        Rtx::PngImage flat(std::uint32_t width, std::uint32_t height, std::uint8_t value)
        {
            Rtx::PngImage image{ width, height, {} };
            image.mPixels.assign(std::size_t{ width } * height * 4, value);
            return image;
        }

        std::uint8_t& channelAt(Rtx::PngImage& image, std::uint32_t x, std::uint32_t y, std::size_t channel)
        {
            return image.mPixels[(std::size_t{ y } * image.mWidth + x) * 4 + channel];
        }

        /// **A shot is refused against the directory it writes**, however the two are spelled: the
        /// pictures land over their references before either is read, so every one would judge the
        /// same. Another directory, one that does not exist yet, and no `--against` at all are not.
        ///
        /// **In a directory of this run's own**, because one shared by name was removed under this
        /// test by another copy of the binary running beside it.
        TEST(RtxCompareTest, aRunIsNotComparedAgainstWhatItWrites)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            std::filesystem::create_directories(root / "shot");
            std::filesystem::create_directories(root / "before");

            const Misc::Result<void, std::string> itself = checkAgainst(root / "shot", root / "shot");
            ASSERT_FALSE(itself.isOk()) << "a run compared against the directory it writes";
            EXPECT_EQ(itself.error(),
                "--against=" + Files::pathToUnicodeString(root / "shot")
                    + " is where this run writes; name another --out");
            EXPECT_FALSE(checkAgainst(root / "shot", root / "before" / ".." / "shot").isOk())
                << "one directory spelled two ways";
            EXPECT_TRUE(checkAgainst(root / "shot", root / "before").isOk());
            EXPECT_TRUE(checkAgainst(root / "shot", root / "missing").isOk())
                << "a reference nobody wrote has nothing to overwrite";
            EXPECT_TRUE(checkAgainst(root / "shot", {}).isOk());

            std::filesystem::remove_all(root);
        }

        /// The whole of what the report says, on a picture whose differences are counted by hand.
        ///
        /// Ten by ten is a hundred pixels, so one pixel is exactly one percent and the fraction has
        /// nothing rounded in it.
        TEST(RtxCompareTest, aDifferenceIsCountedInPixelsAndMeasuredInChannels)
        {
            const Rtx::PngImage before = flat(10, 10, 100);
            Rtx::PngImage after = before;

            EXPECT_TRUE(compareFrames(before, after).same());
            EXPECT_EQ(compareFrames(before, after).mTotal, 100u);

            // One pixel, off by two in green; and a second off by thirty-seven in red, which is what
            // the worst has to come back as.
            channelAt(after, 3, 4, 1) = 102;
            channelAt(after, 7, 1, 0) = 63;

            const FrameDifference difference = compareFrames(before, after);
            EXPECT_FALSE(difference.same());
            EXPECT_FALSE(difference.mMismatched);
            EXPECT_EQ(difference.mDiffering, 2u);
            EXPECT_EQ(difference.mTotal, 100u);
            EXPECT_EQ(difference.mWorst, 37u);
            EXPECT_DOUBLE_EQ(difference.getPercent(), 2.0);
        }

        /// **Colour only, and the reason is that alpha is not the picture.** The tone curve writes a
        /// constant there; a change confined to it is one nobody can see, and counting it would put
        /// a magnitude on a frame that is identical.
        TEST(RtxCompareTest, alphaIsNotPartOfWhatThePictureLooksLike)
        {
            const Rtx::PngImage before = flat(4, 4, 200);
            Rtx::PngImage after = before;
            for (std::uint32_t y = 0; y < 4; ++y)
                for (std::uint32_t x = 0; x < 4; ++x)
                    channelAt(after, x, y, 3) = 0;

            EXPECT_TRUE(compareFrames(before, after).same());
        }

        /// **The error a noise measure reads**, on a hundred pixels whose errors are set by hand: 90
        /// at nought, 8 at 4 (in green), one at 20 (in blue) and one at 155 (red at 255 over 100).
        /// The mean is (8 × 4 + 20 + 155) / 100 = 2.07. Ninety-nine of the hundred are within 20 and
        /// ninety-eight within 4, so the 99th percentile is 20: the one pixel at 155 is the
        /// percentile's to leave out and the mean's to carry. A pixel that differs in alpha alone
        /// counts nothing.
        TEST(RtxCompareTest, anErrorIsItsMeanAndTheLevelNinetyNinePixelsInAHundredAreWithin)
        {
            const Rtx::PngImage reference = flat(10, 10, 100);
            Rtx::PngImage picture = reference;
            for (std::uint32_t x = 0; x < 8; ++x)
                channelAt(picture, x, 0, 1) = 96;
            channelAt(picture, 0, 1, 2) = 120;
            channelAt(picture, 1, 1, 0) = 255;
            channelAt(picture, 2, 1, 3) = 0;

            EXPECT_EQ(measureError(reference, reference).mMean, 0.0);
            EXPECT_EQ(measureError(reference, reference).mP99, 0u);

            const PictureError error = measureError(picture, reference);
            EXPECT_FALSE(error.mMismatched);
            EXPECT_DOUBLE_EQ(error.mMean, (8.0 * 4.0 + 20.0 + 155.0) / 100.0);
            EXPECT_EQ(error.mP99, 20u);

            EXPECT_TRUE(measureError(picture, flat(10, 9, 100)).mMismatched);
            EXPECT_TRUE(measureError(Rtx::PngImage{}, reference).mMismatched);
        }

        /// **The bar holds what the history could.** Thirty frames at 1920 by 1080: native traces
        /// every shown pixel and is held to the sixteen of a still frame; quality traces 1280 by 720,
        /// `30 * 921600 / 2073600` = 13.3 samples a shown pixel, held to 13; ultra performance traces
        /// 640 by 360, 3.3, held to 3. And never nought, however short the history.
        TEST(RtxCompareTest, aStrafedBarAveragesAsManyFramesAsTheHistoryCouldHold)
        {
            const auto after = [](std::uint32_t frames, Rtx::Upscale mode) {
                return noiseBarFramesAfter(frames, Rtx::extentsFor(1920, 1080, mode));
            };

            EXPECT_EQ(after(30, Rtx::Upscale::Off), sNoiseBarFrames);
            EXPECT_EQ(after(30, Rtx::Upscale::Native), sNoiseBarFrames);
            EXPECT_EQ(after(30, Rtx::Upscale::Quality), 13u);
            EXPECT_EQ(after(30, Rtx::Upscale::UltraPerformance), 3u);
            EXPECT_EQ(after(8, Rtx::Upscale::Native), 8u);
            EXPECT_EQ(after(1, Rtx::Upscale::UltraPerformance), 1u);
        }

        /// **A bias is the difference that survives the blur.** A constant offset survives whole, the
        /// picture's edge held; one pixel 127 apart in the middle of a 21 by 21 picture spreads its 127
        /// over the kernel and comes back as `127 / 441` = 0.288 of a level on average, the kernel lying
        /// inside the picture; and a checkerboard 127 either side of the reference is noise at the
        /// finest scale, gone at `sNoiseBiasBlur` and whole under a blur a tenth of a pixel wide.
        TEST(RtxCompareTest, aBiasIsTheDifferenceThatSurvivesTheBlur)
        {
            const Rtx::PngImage reference = flat(21, 21, 128);
            EXPECT_EQ(blurredDifference(reference, reference, sNoiseBiasBlur), 0.0);

            Rtx::PngImage offset = reference;
            for (std::uint32_t y = 0; y < 21; ++y)
                for (std::uint32_t x = 0; x < 21; ++x)
                    channelAt(offset, x, y, 1) = 132;
            EXPECT_NEAR(*blurredDifference(offset, reference, sNoiseBiasBlur), 4.0, 1e-4);

            Rtx::PngImage impulse = reference;
            channelAt(impulse, 10, 10, 0) = 255;
            EXPECT_NEAR(*blurredDifference(impulse, reference, sNoiseBiasBlur), 127.0 / 441.0, 1e-4);

            Rtx::PngImage checker = reference;
            for (std::uint32_t y = 0; y < 21; ++y)
                for (std::uint32_t x = 0; x < 21; ++x)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        channelAt(checker, x, y, channel) = (x + y) % 2 == 0 ? 255 : 1;
            EXPECT_LT(*blurredDifference(checker, reference, sNoiseBiasBlur), 1.0);
            EXPECT_NEAR(*blurredDifference(checker, reference, 0.1f), 127.0, 1e-3);

            EXPECT_FALSE(blurredDifference(reference, flat(21, 20, 128), sNoiseBiasBlur).has_value());
            EXPECT_FALSE(blurredDifference(Rtx::PngImage{}, reference, sNoiseBiasBlur).has_value());
        }

        /// **Each rule on the four differences that tell them apart**: none, one level on one pixel
        /// (`sDenoiserNoiseLevels`, the card's under the wavelet), two levels (one past it), and no
        /// reference. Exact takes only the first; Denoised takes the one level as well; Hashed is
        /// measured and never judged, whatever the picture did.
        TEST(RtxCompareTest, eachPictureIsHeldToItsOwnRule)
        {
            const Rtx::PngImage before = flat(4, 4, 100);
            Rtx::PngImage oneLevel = before;
            channelAt(oneLevel, 1, 2, 0) = 101;
            Rtx::PngImage twoLevels = before;
            channelAt(twoLevels, 1, 2, 0) = 102;

            const FrameDifference none = compareFrames(before, before);
            const FrameDifference one = compareFrames(before, oneLevel);
            const FrameDifference two = compareFrames(before, twoLevels);
            const FrameDifference missing = compareFrames(before, Rtx::PngImage{});
            ASSERT_EQ(one.mWorst, sDenoiserNoiseLevels);
            ASSERT_EQ(two.mWorst, sDenoiserNoiseLevels + 1);

            struct Case
            {
                PictureRule mRule;
                PictureVerdict mNone;
                PictureVerdict mOne;
                PictureVerdict mTwo;
                PictureVerdict mMissing;
            };
            constexpr Case sCases[] = {
                { PictureRule::Exact, PictureVerdict::Same, PictureVerdict::Moved, PictureVerdict::Moved,
                    PictureVerdict::NoReference },
                { PictureRule::Denoised, PictureVerdict::Same, PictureVerdict::WithinDenoiserNoise,
                    PictureVerdict::Moved, PictureVerdict::NoReference },
                { PictureRule::Hashed, PictureVerdict::Measured, PictureVerdict::Measured, PictureVerdict::Measured,
                    PictureVerdict::Measured },
            };

            for (const Case& expected : sCases)
            {
                SCOPED_TRACE(static_cast<int>(expected.mRule));
                EXPECT_EQ(judgePicture(none, expected.mRule), expected.mNone);
                EXPECT_EQ(judgePicture(one, expected.mRule), expected.mOne);
                EXPECT_EQ(judgePicture(two, expected.mRule), expected.mTwo);
                EXPECT_EQ(judgePicture(missing, expected.mRule), expected.mMissing);
            }
        }

        /// Two sizes are not a delta, and neither is a reference that was never written.
        TEST(RtxCompareTest, nothingToSubtractIsSaidRatherThanCountedAsZero)
        {
            const Rtx::PngImage before = flat(10, 10, 100);

            EXPECT_TRUE(compareFrames(before, flat(10, 9, 100)).mMismatched);
            EXPECT_TRUE(compareFrames(before, Rtx::PngImage{}).mMismatched);
            EXPECT_TRUE(compareFrames(Rtx::PngImage{}, before).mMismatched);

            // And a mismatch is never `same`, which is what the exit status is built on.
            EXPECT_FALSE(compareFrames(before, Rtx::PngImage{}).same());
        }

        void writeTo(const std::filesystem::path& file, const Rtx::PngImage& image)
        {
            std::filesystem::create_directories(file.parent_path());
            const Misc::Result<void, std::string> written
                = Rtx::writePng(file, image.mWidth, image.mHeight, image.mPixels);
            ASSERT_TRUE(written.isOk()) << written.error();
        }

        /// **The exit status of `shot --against`, case by case**, over pictures written to a
        /// directory and read back as a run reads them. A picture moved by one level fails under
        /// `Exact` and passes under `Denoised`; a hashed picture is measured and never fails; a
        /// picture with no reference, or a reference with no picture, fails; one failure among
        /// passes fails the run; no reference directory passes without reading anything; and a
        /// comparison asked for over no pictures fails, because it compared nothing.
        TEST(RtxCompareTest, aRunIsJudgedPictureByPictureAndFailsOnWhatItCannotCompare)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path wrote = root / "wrote";
            const std::filesystem::path against = root / "against";

            const Rtx::PngImage before = flat(4, 4, 100);
            Rtx::PngImage oneLevel = before;
            channelAt(oneLevel, 1, 2, 0) = 101;
            Rtx::PngImage moved = before;
            channelAt(moved, 3, 3, 1) = 140;

            writeTo(against / "same.png", before);
            writeTo(wrote / "same.png", before);
            writeTo(against / "one-level.png", before);
            writeTo(wrote / "one-level.png", oneLevel);
            writeTo(against / "moved.png", before);
            writeTo(wrote / "moved.png", moved);
            writeTo(wrote / "no-reference.png", before);
            writeTo(against / "not-drawn.png", before);

            struct Case
            {
                std::vector<WrittenPicture> mPictures;
                int mStatus;
                const char* mWhat;
            };
            const Case cases[] = {
                { { { "same.png", PictureRule::Exact } }, 0, "the same picture" },
                { { { "one-level.png", PictureRule::Exact } }, 1, "one level under the exact rule" },
                { { { "one-level.png", PictureRule::Denoised } }, 0, "one level under the denoiser's rule" },
                { { { "moved.png", PictureRule::Denoised } }, 1, "forty levels under the denoiser's rule" },
                { { { "moved.png", PictureRule::Hashed } }, 0, "a hashed picture that moved" },
                { { { "no-reference.png", PictureRule::Exact } }, 1, "a picture with no reference" },
                { { { "not-drawn.png", PictureRule::Exact } }, 1, "a reference with no picture" },
                { { { "same.png", PictureRule::Exact }, { "moved.png", PictureRule::Exact } }, 1,
                    "one moved picture among the same" },
                { {}, 1, "nothing to compare" },
            };
            for (const Case& expected : cases)
                EXPECT_EQ(compareRuns(wrote, against, expected.mPictures), expected.mStatus) << expected.mWhat;

            const std::vector<WrittenPicture> moving = { { "moved.png", PictureRule::Exact } };
            EXPECT_EQ(compareRuns(wrote, {}, moving), 0) << "no reference directory asks for no comparison";

            std::filesystem::remove_all(root);
        }

        /// A picture of `width` by `height` whose every pixel is `base`, but for the first `count`
        /// pixels, which are `base + raised` in every colour channel.
        Rtx::PngImage raisedAt(
            std::uint32_t width, std::uint32_t height, std::uint8_t base, std::size_t count, std::uint8_t raised)
        {
            Rtx::PngImage image = flat(width, height, base);
            for (std::size_t pixel = 0; pixel < count; ++pixel)
                for (std::size_t channel = 0; channel < 3; ++channel)
                    image.mPixels[pixel * 4 + channel] = static_cast<std::uint8_t>(base + raised);
            return image;
        }

        /// **The exit status of `noise`, case by case**, over the five pictures a place writes. The
        /// frame stands from its mean by the error of `frame` against a flat 100; the bar from its
        /// limit by `bar` against the same. On a 10 by 10 picture:
        /// - every pixel 2 off against the bar's 2: mean 2 against 2, p99 2 against 2, as clean;
        /// - every pixel 3 off against 2: the mean is worse, noisier;
        /// - two pixels 10 off against every pixel 1: mean 0.2 against 1, but the 99th of 100 pixels
        ///   is 10 against 1, noisier, which the mean alone would pass;
        /// - a missing picture, and a picture of another size, fail as not measured;
        /// - no place at all fails, because nothing was measured.
        TEST(RtxCompareTest, aFrameIsAsCleanAsItsBarOnlyByTheMeanAndThe99thPercentileBoth)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const Rtx::PngImage limit = flat(10, 10, 100);

            const auto place = [&](const std::string& name, const Rtx::PngImage& frame, const Rtx::PngImage& bar) {
                writeTo(root / (name + ".png"), frame);
                writeTo(root / (name + std::string(sNoiseMeanSuffix) + ".png"), limit);
                writeTo(root / (name + std::string(sNoiseBarSuffix) + ".png"), bar);
                writeTo(root / (name + std::string(sNoiseBarLimitSuffix) + ".png"), limit);
                writeTo(root / (name + std::string(sNoiseReferenceSuffix) + ".png"), limit);
            };

            place("clean", raisedAt(10, 10, 100, 100, 2), raisedAt(10, 10, 100, 100, 2));
            place("noisier-mean", raisedAt(10, 10, 100, 100, 3), raisedAt(10, 10, 100, 100, 2));
            place("noisier-tail", raisedAt(10, 10, 100, 2, 10), raisedAt(10, 10, 100, 100, 1));
            place("missing", limit, limit);
            std::filesystem::remove(root / ("missing" + std::string(sNoiseBarLimitSuffix) + ".png"));
            place("other-size", flat(10, 9, 100), limit);

            const auto judge = [&](std::vector<std::string> places) { return judgeNoise(root, places, 16); };
            EXPECT_EQ(judge({ "clean" }), 0);
            EXPECT_EQ(judge({ "noisier-mean" }), 1);
            EXPECT_EQ(judge({ "noisier-tail" }), 1) << "a tail the mean hides";
            EXPECT_EQ(judge({ "missing" }), 1);
            EXPECT_EQ(judge({ "other-size" }), 1);
            EXPECT_EQ(judge({ "clean", "noisier-tail" }), 1) << "one noisier place among clean ones";
            EXPECT_EQ(judge({}), 1) << "nothing measured";

            const PictureError tail = measureError(raisedAt(10, 10, 100, 2, 10), limit);
            EXPECT_DOUBLE_EQ(tail.mMean, 0.2);
            EXPECT_EQ(tail.mP99, 10u);

            std::filesystem::remove_all(root);
        }
    }
}
