#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

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

        /// The largest difference any one channel showed, out of 255.
        std::uint32_t mWorst = 0;

        bool same() const { return !mMismatched && mDiffering == 0; }

        /// What share of the picture moved, as a percentage.
        double getPercent() const;
    };

    /// Subtracts one picture from another. Mismatched where either is empty or they disagree on
    /// their extents.
    FrameDifference compareFrames(const Rtx::PngImage& before, const Rtx::PngImage& after);

    /// How far a picture stands from the one it should converge to, in levels of 255. A pixel's error
    /// is its worst colour channel, as `FrameDifference` counts a pixel.
    struct PictureError
    {
        /// The two are not the same size, or either is missing.
        bool mMismatched = false;

        double mMean = 0.0;

        /// The least error ninety-nine pixels in a hundred are within: where the specks are, which
        /// the mean spreads over the whole picture.
        std::uint32_t mP99 = 0;
    };

    /// Measures `picture` against `reference`. Mismatched where either is empty or they disagree
    /// on their extents.
    PictureError measureError(const Rtx::PngImage& picture, const Rtx::PngImage& reference);

    /// How many frames `noise` averages into the reference, and into the picture the frame is held
    /// to. **The reference at 256**: a second reference drawn from another sequence stood 5 levels
    /// from it at the 99th percentile at the Seyda Neen pier, a seventh of the bar's 33 there, so
    /// what is left of its own noise does not decide a verdict. **The bar at 16 is a decision, not a
    /// derivation**: the frame a player sees is to be as clean as sixteen samples a pixel, a quarter
    /// of one sample's noise. Moving it moves what the claim says. A frame whose history could not
    /// hold that many is held to what it could — `noiseBarFramesAfter`.
    inline constexpr std::uint32_t sNoiseReferenceFrames = 256;
    inline constexpr std::uint32_t sNoiseBarFrames = 16;

    /// How many frames `noise --strafe` flies its frame into the place over: half a second of world,
    /// which at a strafe of 150 units is a player running.
    inline constexpr std::uint32_t sNoiseStrafeFrames = 30;

    /// How many frames the bar averages for a frame whose history is `frames` long: as many samples a
    /// shown pixel as that history could hold, and never more than `sNoiseBarFrames`.
    ///
    /// **A trace that covers fewer pixels than it shows has fewer samples to show.** An upscaled mode
    /// traces the share `extents` say of the shown pixels, so after the strafe's thirty frames quality
    /// holds at most 13 samples a shown pixel and ultra performance 3, however well each is used.
    /// Held to sixteen, every upscaled mode failed strafed by arithmetic, in the order of their
    /// shares (the pond 2.64 at quality to 3.41 at ultra performance, against 2.54); held to what it
    /// had, the verdict says whether the chain uses its samples as well as plain averaging does.
    /// Rounded down, since the history holds at most that many; never under one.
    std::uint32_t noiseBarFramesAfter(std::uint32_t frames, const Rtx::FrameExtents& extents);

    /// What `noise` names the reference and the bar of a place, after the place's own name.
    inline constexpr std::string_view sNoiseReferenceSuffix = "-reference";
    inline constexpr std::string_view sNoiseBarSuffix = "-averaged";

    /// Reads back the three pictures `noise` wrote of each of `places` into `wrote` and says whether
    /// each frame is as clean as its bar: no worse than the bar against the reference, by the mean
    /// and at the 99th percentile. Returns a process exit status, non-zero where any frame is
    /// noisier than its bar or any picture is missing.
    ///
    /// @param barFrames how many frames the bars averaged, which the report names.
    int judgeNoise(const std::filesystem::path& wrote, std::span<const std::string> places, std::uint32_t barFrames);

    /// Reads back what a run wrote and says what moved since `against`: a directory an earlier run
    /// wrote on this machine, never a corpus in the tree, because the picture is a function of the
    /// driver and the card as much as of the code. `files` are the pictures the run wrote, named
    /// relative to `wrote`, and each is looked for under `against` by the same name. Returns a
    /// process exit status, non-zero where any picture differs and zero where `against` is empty.
    ///
    /// **A frame's picture is measured here and judged by its hashes.** `frames` names the files
    /// among `files` that are frames of the run, and those are subtracted for the figure — where a
    /// difference the hashes reported sits, and how large it is — and never for the status: where
    /// an upscaler reconstructed the frame the picture is the upscaler's, and where none did the
    /// hashes already hold the same bytes. Every other picture is traced without one and is judged
    /// here.
    int compareRuns(const std::filesystem::path& wrote, const std::filesystem::path& against,
        std::span<const std::string> files, std::span<const std::string> frames);
}
