#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <apps/rtxtool/instruments/jsontext.hpp>
#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/renderer/png.hpp>

#include "run.hpp"

namespace RtxTool
{
    namespace
    {
        std::ostream& out()
        {
            return Debug::getRawStdout();
        }
    }

    std::uint32_t noiseBarFramesAfter(const std::uint32_t frames, const Rtx::FrameExtents& extents)
    {
        const std::uint64_t traced = std::uint64_t{ extents.mRenderWidth } * extents.mRenderHeight;
        const std::uint64_t shown = std::uint64_t{ extents.mOutputWidth } * extents.mOutputHeight;
        const std::uint64_t held = frames * traced / shown;
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(held, 1, sNoiseBarFrames));
    }

    Misc::Result<NoiseFrame, std::string> noiseFrameFor(
        const std::uint32_t cut, const bool flies, const Rtx::FrameExtents& extents)
    {
        if (cut > 0 && flies)
            return Misc::Err{ std::string(
                "--cut takes the frame standing after a cut, and --strafe and --walk fly it in: name one") };

        if (flies)
            return NoiseFrame{ .mWarmup = std::nullopt,
                .mBarFrames = noiseBarFramesAfter(sNoiseFlightFrames, extents) };
        if (cut > 0)
            return NoiseFrame{ .mWarmup = cut - 1, .mBarFrames = noiseBarFramesAfter(cut + 1, extents) };
        return NoiseFrame{ .mWarmup = std::nullopt, .mBarFrames = noiseBarFramesAfter(sHistoryFrames + 2, extents) };
    }

    int judgeNoise(const std::filesystem::path& wrote, const std::span<const NoiseSide> places,
        const std::uint32_t barFrames, std::vector<NoiseFigures>& measured)
    {
        // A run that measured nothing has not shown that anything is as clean as its bar.
        if (places.empty())
        {
            out() << "  no place was measured\n";
            return 1;
        }

        const auto read = [&](const std::string& name, const std::string_view suffix) {
            return Rtx::readPng(wrote / (name + std::string(suffix) + ".png"));
        };

        std::uint32_t noisier = 0;
        std::uint32_t missing = 0;
        for (const NoiseSide& side : places)
        {
            const std::string& place = side.mPlace;
            const std::array<Misc::Result<Rtx::PngImage, std::string>, 5> pictures{ read(side.mFrame, ""),
                read(side.mFrame, sNoiseMeanSuffix), read(side.mBar, sNoiseBarSuffix),
                read(side.mBar, sNoiseBarLimitSuffix), read(side.mReference, sNoiseReferenceSuffix) };
            const auto unread = std::ranges::find_if(pictures, [](const auto& one) { return !one.isOk(); });
            if (unread != pictures.end())
            {
                out() << std::format("  {:<28} {}\n", place, unread->error());
                ++missing;
                continue;
            }

            const Rtx::PngImage& single = pictures[0].value();
            const Rtx::PngImage& frameMean = pictures[1].value();
            const Rtx::PngImage& barMean = pictures[2].value();
            const Rtx::PngImage& barLimit = pictures[3].value();
            const Rtx::PngImage& reference = pictures[4].value();
            const PictureError frame = measureError(single, frameMean);
            const PictureError bar = measureError(barMean, barLimit);
            const std::optional<double> frameBias = blurredDifference(frameMean, reference, sNoiseBiasBlur);
            const std::optional<double> barBias = blurredDifference(barLimit, reference, sNoiseBiasBlur);
            const std::optional<double> fireflies = fireflyShare(single, reference);
            if (frame.mMismatched || bar.mMismatched || !frameBias.has_value() || !barBias.has_value()
                || !fireflies.has_value())
            {
                out() << std::format("  {:<28} a picture is of another size than the others\n", place);
                ++missing;
                continue;
            }

            const NoiseFigures& figures = measured.emplace_back(NoiseFigures{ .mPlace = place,
                .mNoise = frame,
                .mBarNoise = bar,
                .mBias = *frameBias,
                .mBarBias = *barBias,
                .mFireflies = *fireflies });
            if (!figures.clean())
                ++noisier;

            out() << std::format(
                "  {:<28} noise: frame mean {:.2f} p99 {:.2f}, {} averaged mean {:.2f} p99 {:.2f} — {}; "
                "bias: frame {:.2f}, {} averaged {:.2f}; fireflies {:.2f} in a thousand\n",
                place, frame.mMean, frame.mP99, barFrames, bar.mMean, bar.mP99,
                figures.clean() ? "as clean" : "noisier", figures.mBias, barFrames, figures.mBarBias,
                figures.mFireflies);
        }

        if (noisier == 0 && missing == 0)
        {
            out() << std::format("  every frame is as clean as {} frames averaged\n", barFrames);
            return 0;
        }

        out() << std::format("  {} of {} frames noisier than {} frames averaged, {} could not be measured\n", noisier,
            places.size(), barFrames, missing);
        return 1;
    }

    Misc::Result<void, std::string> writeNoiseRecord(
        const std::filesystem::path& path, const std::span<const std::vector<NoiseFigures>> sides)
    {
        std::ofstream file(path);
        file << "{\"sides\": [";
        for (std::size_t side = 0; side < sides.size(); ++side)
        {
            file << (side == 0 ? "\n  [" : ",\n  [");
            for (std::size_t at = 0; at < sides[side].size(); ++at)
            {
                const NoiseFigures& place = sides[side][at];
                file << (at == 0 ? "\n    " : ",\n    ")
                     << std::format(R"({{"place": {}, "mean": {:.4f}, "p99": {:.4f}, "barMean": {:.4f}, )"
                                    R"("barP99": {:.4f}, "bias": {:.4f}, "barBias": {:.4f}, "fireflies": {:.4f}, )"
                                    R"("clean": {}}})",
                            jsonString(place.mPlace), place.mNoise.mMean, place.mNoise.mP99, place.mBarNoise.mMean,
                            place.mBarNoise.mP99, place.mBias, place.mBarBias, place.mFireflies, place.clean());
            }
            file << "\n  ]";
        }
        file << "\n]}\n";

        // Closed and then asked, because a write the system reports late reports it at the close.
        file.close();
        if (!file)
            return Misc::Err{ "could not write " + Files::pathToUnicodeString(path) };
        return {};
    }

    namespace
    {
        /// **The truth a side is held against traces unfiltered, every frame a draw of its own**
        /// (`ReconstructionRequest::unfiltered`): a frame that reused the ones before it is not one
        /// more sample of the truth. Its indirect light stays the run's, since a traced bounce and
        /// none are two integrands.
        Rtx::ReconstructionRequest referenceOf(const Rtx::ReconstructionRequest& side)
        {
            Rtx::ReconstructionRequest truth = side.unfiltered();
            truth.mJitter = true;
            truth.mSampling.mNoise = Rtx::NoiseSource::WhiteHash;
            // **The truth reads every texture at the level its footprint asks**, whatever the
            // run's epsilon: an epsilon is a knob on the frame, and a reference that moved with
            // it would take the frame's softness for its own and report no bias at all.
            truth.mLevelEpsilon = 0.0f;
            // **And draws every source for its bit**: a floor rides a minor source's light on
            // another's shadow, which is the bias the A/B of the floor measures.
            truth.mSampling.mShadowFloor = 0.0f;
            // And weighs every lamp, which a fixed count of candidates estimates.
            truth.mSampling.mLampCandidates = 0u;
            return truth;
        }
    }

    NoisePlan planNoise(const NoiseAsk& ask)
    {
        const Rtx::ReconstructionRequest& played = ask.mPlayed;
        const std::optional<Rtx::ReconstructionRequest>& versus = ask.mVersus;
        const std::filesystem::path& folder = ask.mFolder;

        // **The bar traces unfiltered too**, for the reason `referenceOf` gives: a frame of the bar
        // is a sample of what the frame converges to. So the other side traces its own only where
        // its unfiltered frames trace otherwise.
        const bool ownBar = versus.has_value() && versus->unfiltered() != played.unfiltered();
        // **And its own reference only where the truth it traces is another**: a switch the truth
        // sets for itself — the jitter, the noise, the level epsilon, the floor, the lamps it
        // weighs — leaves the first side's, which is 256 frames a place not traced twice. Every
        // field the truth keeps is one the unfiltered request keeps, so an own reference comes
        // with an own bar.
        const bool ownReference = versus.has_value() && referenceOf(*versus) != referenceOf(played);
        assert(!ownReference || ownBar);
        const Rtx::ExposureRule held = Rtx::HeldExposure{};

        // One picture of `place` after `frames` frames: their sum where `summed`, and the last of
        // them where not.
        const auto picture
            = [&](const Stop& place, const std::string_view suffix, const std::uint32_t frames, const bool summed,
                  const std::optional<Rtx::ReconstructionRequest>& reconstruction,
                  const std::optional<Rtx::ExposureRule>& exposure, const std::optional<Rtx::Upscale> upscale) {
                  Stop stop = place;
                  stop.mName += suffix;
                  measureFrames(stop, frames);
                  stop.mSchedule.mAccumulate = summed ? frames : 0;
                  stop.mSchedule.mReconstruction = reconstruction;
                  stop.mSchedule.mExposure = exposure;
                  stop.mSchedule.mUpscale = upscale;
                  stop.mActions.mCapture = folder / (stop.mName + ".png");
                  return stop;
              };

        const float strafe = ask.mStrafe;
        const float walk = ask.mWalk;
        const bool flies = strafe > 0.0f || walk != 0.0f;

        // Each leg's frame is held to the samples a shown pixel its history could hold
        // (`noiseFrameFor`), standing as well.
        const Misc::Result<NoiseFrame, std::string> taken = noiseFrameFor(ask.mCut, flies, ask.mExtents);
        if (!taken.isOk())
            throw std::runtime_error(taken.error());
        const NoiseFrame& leg = taken.value();
        const std::uint32_t barFrames = leg.mBarFrames;

        // The frame's own stop, flying in where the line asks: a route that holds the world, so
        // the frame flies through the world the reference stands in (`applyPolicy`).
        const auto frame = [&](const Stop& place, const Rtx::ReconstructionRequest& side) {
            Stop stop = picture(place, "", flies ? sNoiseFlightFrames : 1, false, side, held, std::nullopt);
            if (leg.mWarmup.has_value())
                stop.mSchedule.mSpec.mWarm = BenchSpan{ .mFrames = *leg.mWarmup };
            if (!flies)
                return stop;

            if (!place.mStand.mEye.has_value())
                throw std::runtime_error(
                    std::format("--strafe and --walk need a place that names an eye, and {} names none", place.mName));

            // An eye that started past the point it faces would fly in facing backwards.
            if (walk < 0.0f && -walk >= (place.mStand.getLook() - *place.mStand.mEye) * place.mStand.getLevelAhead())
                throw std::runtime_error(std::format("--walk={} starts past the point {} faces", walk, place.mName));

            Approach approach = stop.mStand.approachFrom(strafe, walk, ask.mStep, sNoiseFlightFrames);
            stop.mStand = std::move(approach.mFrom);
            stop.mSchedule.mRoute = approach.mRoute;
            return stop;
        };

        // **The sample offsets are each stop's place in a whole side**, so the other side draws
        // what the first drew, and an A/B compares two reconstructions of one set of draws. A side
        // that asks what the first asked took the frame to the byte at the glow-lit chamber, and
        // the frame's mean to within 50 bytes of 8.3 million, each by one level, which no figure
        // of the report showed: the card's arithmetic under the wavelet
        // (`docs/rtx/architecture.md`, the denoiser), which two runs differ by too. The places:
        // the reference, the bar, the bar's limit's draws, the frame, the frame's mean's draws.
        constexpr std::size_t stopsASide = 3 + 2 * sNoiseMeanDraws;
        static_assert(stopsASide * std::uint64_t{ sNoiseSampleStride } <= ~std::uint32_t{ 0 },
            "the sample offsets of one place past what a frame number holds");
        const auto offsetAt = [](const std::size_t at, const std::uint32_t later) {
            return static_cast<std::uint32_t>(at * sNoiseSampleStride) + later;
        };

        // `sNoiseMeanDraws` draws of `drawn`, at the places from `at` on, each adding its last
        // frame to the mean `suffix` names, as shown.
        const auto drawMean = [&](const Stop& place, const Stop& drawn, const std::string_view suffix,
                                  const std::size_t at, const std::uint32_t later, std::vector<Stop>& into) {
            for (std::uint32_t draw = 0; draw < sNoiseMeanDraws; ++draw)
            {
                Stop again = drawn;
                again.mName = place.mName + std::string(suffix);
                again.mActions.mCapture.clear();
                again.mActions.mMean = Actions::Mean{
                    .mFile = folder / (place.mName + std::string(suffix) + ".png"),
                    .mOf = sNoiseMeanDraws,
                };
                again.mSchedule.mSampleOffset = offsetAt(at + draw, later);
                into.push_back(std::move(again));
            }
        };

        // One side of a place: its reference where `reference` asks, its bar and the bar's limit
        // where `bar` asks, then its frame and the frame's mean.
        //
        // **The bar and its limit start their samples as much later as their warm-up is shorter**
        // than a filtered picture's, so each measured frame draws what it drew when they warmed
        // over `sHistoryFrames`: what moved is the air's tail alone.
        const auto drawSide = [&](const Stop& place, const Rtx::ReconstructionRequest& side, const bool reference,
                                  const bool bar, std::vector<Stop>& into) {
            if (reference)
            {
                Stop truth = picture(place, sNoiseReferenceSuffix, sNoiseReferenceFrames, true, referenceOf(side),
                    std::nullopt, Rtx::Upscale::Off);
                truth.mActions.mDeepCapture = true;
                truth.mSchedule.mSampleOffset = offsetAt(0, 0);
                into.push_back(std::move(truth));
            }
            if (bar)
            {
                // Unfiltered at a held exposure, so the air's is its one history (`sAirFrames`).
                const std::uint32_t shortened = sHistoryFrames - sAirFrames;
                Stop averaged
                    = picture(place, sNoiseBarSuffix, barFrames, true, side.unfiltered(), held, Rtx::Upscale::Off);
                averaged.mSchedule.mSpec.mWarm = BenchSpan{ .mFrames = sAirFrames };
                averaged.mSchedule.mSampleOffset = offsetAt(1, shortened);
                into.push_back(averaged);
                drawMean(place, averaged, sNoiseBarLimitSuffix, 2, shortened, into);
            }
            Stop judged = frame(place, side);
            judged.mSchedule.mSampleOffset = offsetAt(2 + sNoiseMeanDraws, 0);
            into.push_back(judged);
            drawMean(place, judged, sNoiseMeanSuffix, 3 + sNoiseMeanDraws, 0, into);
        };

        NoisePlan plan;
        plan.mBarFrames = barFrames;
        plan.mOwnBar = ownBar;
        plan.mOwnReference = ownReference;
        plan.mStops.reserve(ask.mPlaces.size() * stopsASide * (versus.has_value() ? 2 : 1));
        plan.mSides.reserve(ask.mPlaces.size());
        plan.mVersusSides.reserve(versus.has_value() ? ask.mPlaces.size() : 0);
        for (const Stop& place : ask.mPlaces)
        {
            plan.mSides.push_back(NoiseSide{
                .mPlace = place.mName, .mFrame = place.mName, .mBar = place.mName, .mReference = place.mName });
            drawSide(place, played, true, true, plan.mStops);
            if (!versus.has_value())
                continue;

            // After the first side's reference, whose exposure every picture after it holds.
            Stop other = place;
            other.mName += sNoiseVersusSuffix;
            plan.mVersusSides.push_back(NoiseSide{ .mPlace = place.mName,
                .mFrame = other.mName,
                .mBar = ownBar ? other.mName : place.mName,
                .mReference = ownReference ? other.mName : place.mName });
            drawSide(other, *versus, ownReference, ownBar, plan.mStops);
        }

        return plan;
    }
}
