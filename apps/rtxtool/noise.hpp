#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/pictureshelf.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/shaders/look.h>

#include "compare.hpp"

namespace RtxTool
{
    /// How many frames an unfiltered picture at a held exposure warms up over: its one history is
    /// the air's, which keeps `FOG_VOLUME_HISTORY` of itself a frame, so this is where the air's
    /// weight on the frame the cut left falls under what `sHistoryFrames` leaves of the
    /// accumulator's, `(1 - 1/N)^4N`: 39 at a history of 0.9 and an `N` of 32, against 128.
    inline const std::uint32_t sAirFrames = static_cast<std::uint32_t>(std::ceil(
        std::log(std::pow(1.0 - 1.0 / static_cast<double>(Rtx::Shaders::ACCUMULATE_FRAMES), double(sHistoryFrames)))
        / std::log(static_cast<double>(Rtx::Shaders::FOG_VOLUME_HISTORY))));

    /// How many frames `noise` averages into the reference, and into the bar. The reference at 256: a second
    /// reference drawn from another sequence stood 5 levels from it at the 99th percentile at the Seyda Neen pier,
    /// so what is left of its own noise does not decide a bias. The bar at 16 is a decision, not a derivation: the
    /// frame a player sees is to be as clean as sixteen samples a pixel. A frame whose history could not hold that
    /// many is held to what it could (`NoiseFrame::mBar`).
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

    /// How `noise` takes the frame it judges, out of what the line asked.
    struct NoiseFrame
    {
        /// The judged stop's warm-up where the leg sets its own, and nothing where the stop keeps the
        /// one every picture of a place converges over.
        std::optional<std::uint32_t> mWarmup;

        /// How many frames the bar averages: as many samples a shown pixel as the frame's history holds, never more
        /// than `sNoiseBarFrames`, settled once the renderer has made the run's extents. A trace that covers fewer
        /// pixels than it shows has fewer samples to show: an upscaled mode traces the share its extents say, so
        /// after a flight's thirty frames quality holds at most 13 samples a shown pixel and ultra performance 3.
        /// Held to sixteen, every upscaled mode failed strafed by arithmetic (the pond 2.64 at quality to 3.41 at
        /// ultra performance, against 2.54); held to what it had, the verdict says whether the chain uses its
        /// samples as well as plain averaging does.
        HeldFrames mBar;
    };

    /// The frame `noise` judges: standing after its history converged, flown in over `sNoiseFlightFrames` where
    /// `flies`, or `cut` frames after the cut its stop begins with, each held to what its history could hold.
    /// Standing too: its warm-up is `sHistoryFrames`, so its history holds `sHistoryFrames + 2` frames, which at
    /// ultra performance is fewer samples a shown pixel than `sNoiseBarFrames`.
    ///
    /// A stop begins with a cut already (`Stager::forgetHistory`), and its first frame, the one the cut resets, is
    /// not measured. So the frame `cut` frames after it is a warm-up of `cut - 1` and its history holds `cut + 1`
    /// frames, which is what its bar holds, where the world stands whole on the stop's first frame; a stop that
    /// waited for its world says so in the run's notes, and its frame then holds the frames it waited as well. That
    /// is where the fireflies were reported: the first frames after a door, a load or a teleport, before a history
    /// can tell a rare bright bounce from the light.
    ///
    /// A cut and a flight are two ways to take the frame, and a line naming both is refused.
    Result<NoiseFrame, std::string> noiseFrameFor(std::uint32_t cut, bool flies);

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

    /// One place's frame as `judgeNoise` measured it, which its line prints and `writeNoiseRecord`
    /// writes: the frame's noise against its own mean and the bar's against the bar's limit, each by
    /// the mean and the 99th percentile, each one's bias against the reference, the frame's
    /// fireflies in a thousand pixels, and its noisiest tile.
    struct NoiseFigures
    {
        std::string mPlace;
        PictureError mNoise;
        PictureError mBarNoise;
        double mBias = 0.0;
        double mBarBias = 0.0;
        double mFireflies = 0.0;
        TileNoise mWorstTile;

        /// Whether the frame is as clean as its bar, by the mean and at the 99th percentile.
        bool clean() const { return mNoise.mMean <= mBarNoise.mMean && mNoise.mP99 <= mBarNoise.mP99; }
    };

    /// What `noise` names its record in the folder it writes its pictures to: `writeNoiseRecord`.
    inline constexpr std::string_view sNoiseRecord = "noise.json";

    /// What `noise --versus` names the second side's pictures of a place, after the place's name.
    inline constexpr std::string_view sNoiseVersusSuffix = "-versus";

    /// Takes the five pictures `noise` wrote of each of `places` into `wrote`, kept on `drawn`, and says whether
    /// each frame is as clean as its bar: its noise, how far it stands from its own mean, no more than the bar's
    /// from the bar's own limit, by the mean and at the 99th percentile. Beside it, each one's bias: how far its
    /// mean stands from the reference, blurred by `sNoiseBiasBlur`. Returns a process exit status, non-zero where
    /// any frame is noisier than its bar, any picture was not written or is of another size, or no place was named.
    ///
    /// Noise against noise, because a frame and its bar are drawn two ways: held to the one reference, each also
    /// carried the difference between its own reconstruction and the reference's box over the pixel (at the Seyda
    /// Neen shore three native frames of independent draws stood 0.27 apart and each 1.83 from the reference, and
    /// so did their mean: FSR's kernel against the box, which failed every upscaled mode as noise it was not).
    ///
    /// @param barFrames how many frames the bars averaged, which the report names.
    /// @param measured where each place measured is appended, for `writeNoiseRecord`.
    int judgeNoise(const PictureShelf& drawn, const std::filesystem::path& wrote, std::span<const NoiseSide> places,
        std::uint32_t barFrames, std::vector<NoiseFigures>& measured);

    /// Writes what each side of a `noise` run measured to `path` as JSON — `{"sides": [[...], ...]}`,
    /// a side a list of its places' figures, the first side first — for a reader that is no person:
    /// `omw noise --ab` reads it, and never the report's sentences.
    Result<void, std::string> writeNoiseRecord(
        const std::filesystem::path& path, std::span<const std::vector<NoiseFigures>> sides);

    /// What `planNoise` plans from: the places, the folder the pictures go to, and what the line
    /// asked of the frame.
    struct NoiseAsk
    {
        std::span<const Stop> mPlaces;
        std::filesystem::path mFolder;

        /// The run's reconstruction, which the first side plays.
        Rtx::ReconstructionRequest mPlayed;

        /// The other side's, where the line names one (`ToolOptions::versus`).
        std::optional<Rtx::ReconstructionRequest> mVersus;

        /// `--strafe` and `--walk`, in world units: where the frame flies in from.
        float mStrafe = 0.0f;
        float mWalk = 0.0f;

        /// `--cut`: the frame taken this many frames after its stop's cut, or nought for none.
        std::uint32_t mCut = 0;

        /// How far the world steps a frame, in seconds (`worldStep`), which a flight is paced by.
        float mStep = 0.0f;
    };

    /// What a `noise` run draws, and under which names its judge takes the pictures.
    struct NoisePlan
    {
        std::vector<Stop> mStops;

        /// Each place's first side, and its other where the line names one, for `judgeNoise`.
        std::vector<NoiseSide> mSides;
        std::vector<NoiseSide> mVersusSides;

        /// How many frames the bars average: `NoiseFrame::mBar`, which the session settles in the bars' stops.
        HeldFrames mBar;

        /// Whether the other side traces a bar and a reference of its own.
        bool mOwnBar = false;
        bool mOwnReference = false;
    };

    /// The stops a `noise` run draws at `ask`'s places, and how its sides read them back: at each
    /// place, a reference, a bar and the bar's limit, the frame and the frame's mean, and with
    /// `--versus` the other side after the first. Nothing of a world: a place is its stop.
    ///
    /// @throws std::runtime_error for a line it refuses: a cut and a flight both, a flight to a place
    ///         that names no eye, or a walk that starts past the point the place faces.
    NoisePlan planNoise(const NoiseAsk& ask);
}
