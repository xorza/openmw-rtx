#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/misc/result.hpp>
#include <components/rtx/renderer/png.hpp>

namespace RtxTool
{

    /// What two renderings of one picture came to: a magnitude and not a verdict, because "worst 2 of
    /// 255 on 5% of the pixels" is a rounding difference and "worst 37 on 20%" is a bug. `rayAt` is
    /// `precise` so that the driver's second compile of the trace pipeline cannot be the difference.
    struct FrameDifference
    {
        /// The two are not the same size, so there is nothing to subtract. Also what a missing or
        /// unreadable reference reads as.
        bool mMismatched = false;

        /// How many pixels differ in any colour channel, out of how many there are. Colour only,
        /// because a difference confined to alpha is not one anybody can see.
        std::uint64_t mDiffering = 0;
        std::uint64_t mTotal = 0;

        /// The largest difference any one channel showed, out of 255: rounded up where a picture is
        /// a mean, so a difference of any size is at least one.
        std::uint32_t mWorst = 0;

        bool same() const { return !mMismatched && mDiffering == 0; }

        /// What share of the picture moved, as a percentage.
        double getPercent() const;
    };

    /// Subtracts one picture from another. Mismatched where either is empty or they disagree on
    /// their extents.
    FrameDifference compareFrames(const Rtx::PngImage& before, const Rtx::PngImage& after);

    /// How far a picture the wavelet put together may stand from the same picture of another run of
    /// one build, out of 255: **the card's arithmetic under the wavelet, and not the tree's**
    /// (`docs/rtx/architecture.md`, the denoiser). What it was measured to leave is one half-float
    /// step in the filtered light, and one level in the picture. Measured and not derived, so a
    /// card that ever moves a picture further fails the run, which is the answer.
    inline constexpr std::uint32_t sDenoiserNoiseLevels = 1;

    /// What a picture of a run is held to against the same picture of its reference.
    enum class PictureRule
    {
        /// Nothing between the tree and the picture that two runs may disagree about: any
        /// difference is one.
        Exact,

        /// The wavelet put it together — a doll, a map tile, a frame denoised and not upscaled:
        /// within `sDenoiserNoiseLevels` is the card's, and past it a difference.
        Denoised,

        /// A frame whose verdict is its hashes (`FrameHashes`): measured for where a difference
        /// sits and how large it is, and never judged here.
        Hashed,
    };

    /// What one picture came to under its rule.
    enum class PictureVerdict
    {
        Same,
        WithinDenoiserNoise,
        Moved,
        NoReference,
        Measured,
    };

    PictureVerdict judgePicture(const FrameDifference& difference, PictureRule rule);

    /// A picture a run wrote, named relative to where it wrote them, and what it is held to.
    struct WrittenPicture
    {
        std::string mFile;
        PictureRule mRule = PictureRule::Exact;
    };

    /// How far a picture stands from the one it should converge to, in levels of 255. A pixel's error
    /// is its worst colour channel, as `FrameDifference` counts a pixel.
    struct PictureError
    {
        /// The two are not the same size, or either is empty.
        bool mMismatched = false;

        /// The mean of each pixel's worst colour channel, out of 255, in fractions of a level where a
        /// picture is a mean.
        double mMean = 0.0;

        /// The least error ninety-nine pixels in a hundred are within, on the same scale: where the
        /// specks are, which the mean spreads over the whole picture.
        double mP99 = 0.0;
    };

    /// Measures `picture` against `reference`. Mismatched where either is empty or they disagree
    /// on their extents.
    PictureError measureError(const Rtx::PngImage& picture, const Rtx::PngImage& reference);

    /// How far a picture stands from another after both are blurred by a Gaussian of `sigma`
    /// pixels, as the mean over the pixels of the worst colour channel, out of 255 — or nothing
    /// where either is empty or they disagree on their extents. Blurred as one signed difference,
    /// which is the same number, because a blur is linear; the picture's edge is held.
    std::optional<double> blurredDifference(const Rtx::PngImage& picture, const Rtx::PngImage& reference, float sigma);

    /// When `noise` counts a pixel of the frame as a firefly: its light, after the tone curve as the
    /// player sees it, `sNoiseFireflyRatio` times the reference's, and over it by at least the light
    /// of a grey at `sNoiseFireflyFloor` levels of 255. **The floor keeps a dark speck out**: four
    /// times the light of a level-2 corner is a corner at level 8, a ratio a ratio test alone
    /// counts. Sixteen is chosen and not derived, a sixteenth of the display's range in level; moving
    /// it moves what the count says, as `sNoiseBarFrames` moves what the bar says.
    inline constexpr double sNoiseFireflyRatio = 4.0;
    inline constexpr std::uint8_t sNoiseFireflyFloor = 16;

    /// How many of `picture`'s pixels, per thousand, stand over `reference` as fireflies do
    /// (`sNoiseFireflyRatio`, `sNoiseFireflyFloor`), each pixel's light the luminance of its decoded
    /// channels. Nothing where either is empty or they disagree on their extents.
    std::optional<double> fireflyShare(const Rtx::PngImage& picture, const Rtx::PngImage& reference);

    /// Whether a run that writes its pictures into `out` can be compared against `against`, and why
    /// not where it cannot. The pictures are written over their references before the two
    /// directories are read, so one directory named twice judges every picture the same: `shot`
    /// and then `shot --against=shot` is the pair this refuses.
    Misc::Result<void, std::string> checkAgainst(
        const std::filesystem::path& out, const std::filesystem::path& against);

    /// Removes what an earlier run left at each of `pictures` under `out`, ahead of a run that
    /// writes them: a picture this run does not reach then reads as missing, and never as the last
    /// run's, which compared as the same.
    void clearPictures(const std::filesystem::path& out, std::span<const WrittenPicture> pictures);

    /// Reads back what a run wrote and says what moved since `against`: a directory an earlier run
    /// wrote on this machine, never a corpus in the tree, because the picture is a function of the
    /// driver and the card as much as of the code. Each of `pictures` is looked for under `against`
    /// by the name it has under `wrote`, and held to its rule. Returns a process exit status,
    /// non-zero where any picture moved or has no reference, or where there is none to compare, and
    /// zero where `against` is empty.
    ///
    /// **A frame's picture is judged by its hashes where they can judge it.** Where an upscaler
    /// reconstructed the frame the picture is the upscaler's, and where nothing composed it past
    /// the trace the hashes already hold the same bytes. Where the wavelet composed it, the hashes
    /// cannot tell the card's last bit from a change, and the picture is what is held.
    int compareRuns(const std::filesystem::path& wrote, const std::filesystem::path& against,
        std::span<const WrittenPicture> pictures);
}
