#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <components/misc/result.hpp>
#include <components/rtx/frame/frameextents.hpp>
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

    /// How many frames `noise` averages into the reference, and into the bar. **The reference at
    /// 256**: a second reference drawn from another sequence stood 5 levels from it at the 99th
    /// percentile at the Seyda Neen pier, so what is left of its own noise does not decide a bias.
    /// **The bar at 16 is a decision, not a derivation**: the frame a player sees is to be as clean
    /// as sixteen samples a pixel, a quarter of one sample's noise. Moving it moves what the claim
    /// says. A frame whose history could not hold that many is held to what it could —
    /// `noiseBarFramesAfter`.
    inline constexpr std::uint32_t sNoiseReferenceFrames = 256;
    inline constexpr std::uint32_t sNoiseBarFrames = 16;

    /// How many independent draws `noise` averages into what the frame converges to, and into the
    /// bar's limit: **one count for both**, so each mean holds the same share of the noise it is a
    /// mean of — a picture stands `sqrt(1 + 1/32)` of its own noise from the mean of 32 draws it is
    /// not one of, 1.6% over — and the verdict between the two is even.
    inline constexpr std::uint32_t sNoiseMeanDraws = 32;

    /// How far apart in the sampler's sequence `noise` sets the stops at one place
    /// (`Schedule::mSampleOffset`): past the longest a stop runs, its wait for the world, its
    /// warm-up and a paused world included, so that no two stops draw one sample; and odd, so that a
    /// sequence walked in phases — an upscaler's jitter — puts each stop on a phase of its own.
    inline constexpr std::uint32_t sNoiseSampleStride = 1000003;

    /// The width of the blur `noise` measures a bias at, in pixels: wide enough that how a picture is
    /// reconstructed — a box over each pixel for the reference, the pixel's centre for the bar, FSR's
    /// kernel for an upscaled frame — drops out, and narrow enough that a light leaking a few pixels
    /// does not.
    inline constexpr float sNoiseBiasBlur = 1.5f;

    /// How many frames `noise --strafe` and `--walk` fly the frame into the place over: half a second
    /// of world, which over 150 units is a player running.
    inline constexpr std::uint32_t sNoiseFlightFrames = 30;

    /// How many frames the bar averages for a frame whose history is `frames` long: as many samples a
    /// shown pixel as that history could hold, and never more than `sNoiseBarFrames`.
    ///
    /// **A trace that covers fewer pixels than it shows has fewer samples to show.** An upscaled mode
    /// traces the share `extents` say of the shown pixels, so after a flight's thirty frames quality
    /// holds at most 13 samples a shown pixel and ultra performance 3, however well each is used.
    /// Held to sixteen, every upscaled mode failed strafed by arithmetic, in the order of their
    /// shares (the pond 2.64 at quality to 3.41 at ultra performance, against 2.54); held to what it
    /// had, the verdict says whether the chain uses its samples as well as plain averaging does.
    /// Rounded down, since the history holds at most that many; never under one.
    std::uint32_t noiseBarFramesAfter(std::uint32_t frames, const Rtx::FrameExtents& extents);

    /// How `noise` takes the frame it judges, out of what the line asked.
    struct NoiseFrame
    {
        /// The judged stop's warm-up where the leg sets its own, and nothing where the stop keeps the
        /// one every picture of a place converges over.
        std::optional<std::uint32_t> mWarmup;

        /// How many frames the bar averages: `noiseBarFramesAfter` the history the frame holds.
        std::uint32_t mBarFrames = 0;
    };

    /// The frame `noise` judges: standing after its history converged, flown in over
    /// `sNoiseFlightFrames` where `flies`, or `cut` frames after the cut its stop begins with.
    /// **Each held to what its history could hold**, standing as well: its warm-up is
    /// `sHistoryFrames`, so its history holds `sHistoryFrames + 2` frames, which at ultra
    /// performance is fewer samples a shown pixel than `sNoiseBarFrames`.
    ///
    /// **A stop begins with a cut already** (`Stager::forgetHistory`), and its first frame, the one
    /// the cut resets, draws the world and is not measured. So the frame `cut` frames after it is a
    /// warm-up of `cut - 1`, and its history holds `cut + 1` frames, which is what its bar holds —
    /// where the world stands whole on the stop's first frame, as it does at a place earlier stops
    /// already loaded. A stop that waited for its world says so in the run's notes, and its frame
    /// then holds the frames it waited as well.
    /// That is where the fireflies were reported: the first frames after a door, a load or a
    /// teleport, before a history holds enough to tell a rare bright bounce from the light.
    ///
    /// A cut and a flight are two ways to take the frame, and a line naming both is refused.
    Misc::Result<NoiseFrame, std::string> noiseFrameFor(
        std::uint32_t cut, bool flies, const Rtx::FrameExtents& extents);

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

    /// What `noise` names the pictures of a place, after the place's own name: the reference, the
    /// bar, the bar's limit and the frame's mean. The frame is the place's name alone.
    inline constexpr std::string_view sNoiseReferenceSuffix = "-reference";
    inline constexpr std::string_view sNoiseBarSuffix = "-averaged";
    inline constexpr std::string_view sNoiseBarLimitSuffix = "-averaged-limit";
    inline constexpr std::string_view sNoiseMeanSuffix = "-mean";

    /// One place's frame as `noise` judges it: the place its line names, the name its frame's two
    /// pictures were written under, and the name its bar's three were — the place's own, or another
    /// side's where both sides trace the bar alike (`--versus`).
    struct NoiseSide
    {
        std::string mPlace;
        std::string mFrame;
        std::string mBar;

        /// Whose reference the side is held against: its own where it traced one, and the first
        /// side's where its truth is that one.
        std::string mReference;
    };

    /// What `noise --versus` names the second side's pictures of a place, after the place's name.
    inline constexpr std::string_view sNoiseVersusSuffix = "-versus";

    /// Reads back the five pictures `noise` wrote of each of `places` into `wrote` and says whether
    /// each frame is as clean as its bar: **its noise** — how far it stands from its own mean — no
    /// more than the bar's from the bar's own limit, by the mean and at the 99th percentile. Beside
    /// it, **each one's bias**: how far its mean stands from the reference, blurred by
    /// `sNoiseBiasBlur`. Returns a process exit status, non-zero where any frame is noisier than its
    /// bar, any picture is missing, does not read, or is of another size, or no place was named.
    ///
    /// **Noise against noise, because a frame and its bar are drawn two ways.** Held to the one
    /// reference, each carried the difference between its own reconstruction and the reference's
    /// box over the pixel as well: at the Seyda Neen shore, three native frames of independent draws
    /// stood 0.27 apart and each 1.83 from the reference, and their mean stood 1.83 from it too —
    /// FSR's kernel against the box, which failed every upscaled mode as noise it was not.
    ///
    /// @param barFrames how many frames the bars averaged, which the report names.
    int judgeNoise(const std::filesystem::path& wrote, std::span<const NoiseSide> places, std::uint32_t barFrames);

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
