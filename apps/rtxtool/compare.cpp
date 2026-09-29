#include "compare.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>

namespace RtxTool
{
    namespace
    {
        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        /// Whether the two have a picture each, of one size, so there is something to subtract.
        bool comparable(const Rtx::PngImage& one, const Rtx::PngImage& other)
        {
            return !one.empty() && !other.empty() && one.mWidth == other.mWidth && one.mHeight == other.mHeight;
        }

        /// The difference of the pixel whose first byte is `at`: its worst colour channel, out of 255.
        /// Colour only, because alpha is not the picture.
        std::uint32_t worstChannel(const Rtx::PngImage& one, const Rtx::PngImage& other, const std::size_t at)
        {
            std::uint32_t worst = 0;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto a = static_cast<std::int32_t>(one.mPixels[at + channel]);
                const auto b = static_cast<std::int32_t>(other.mPixels[at + channel]);
                worst = std::max(worst, static_cast<std::uint32_t>(std::abs(a - b)));
            }

            return worst;
        }

        /// How a difference reads on one line.
        std::string describe(const FrameDifference& difference)
        {
            if (difference.mMismatched)
                return "no reference, or one of a different size";

            if (difference.same())
                return "same";

            return std::format(
                "differs: worst {} of 255 on {:.2f}% of the pixels", difference.mWorst, difference.getPercent());
        }
    }

    double FrameDifference::getPercent() const
    {
        return mTotal == 0 ? 0.0 : static_cast<double>(mDiffering) / static_cast<double>(mTotal) * 100.0;
    }

    FrameDifference compareFrames(const Rtx::PngImage& before, const Rtx::PngImage& after)
    {
        if (!comparable(before, after))
            return FrameDifference{ .mMismatched = true };

        FrameDifference difference;
        difference.mTotal = std::uint64_t{ before.mWidth } * before.mHeight;

        for (std::size_t at = 0; at + 3 < before.mPixels.size(); at += 4)
        {
            const std::uint32_t worst = worstChannel(before, after, at);
            if (worst > 0)
            {
                ++difference.mDiffering;
                difference.mWorst = std::max(difference.mWorst, worst);
            }
        }

        return difference;
    }

    PictureError measureError(const Rtx::PngImage& picture, const Rtx::PngImage& reference)
    {
        if (!comparable(picture, reference))
            return PictureError{ .mMismatched = true };

        std::array<std::uint64_t, 256> counts{};
        std::uint64_t sum = 0;
        for (std::size_t at = 0; at + 3 < picture.mPixels.size(); at += 4)
        {
            const std::uint32_t worst = worstChannel(picture, reference, at);
            ++counts[worst];
            sum += worst;
        }

        const std::uint64_t pixels = std::uint64_t{ picture.mWidth } * picture.mHeight;

        // Counted in whole pixels, rounded up: at least ninety-nine in a hundred are within it.
        const std::uint64_t needed = (pixels * 99 + 99) / 100;
        std::uint64_t within = 0;
        std::uint32_t level = 0;
        while ((within += counts[level]) < needed)
            ++level;

        return PictureError{
            .mMean = static_cast<double>(sum) / static_cast<double>(pixels),
            .mP99 = level,
        };
    }

    std::uint32_t noiseBarFramesAfter(const std::uint32_t frames, const Rtx::FrameExtents& extents)
    {
        const std::uint64_t traced = std::uint64_t{ extents.mRenderWidth } * extents.mRenderHeight;
        const std::uint64_t shown = std::uint64_t{ extents.mOutputWidth } * extents.mOutputHeight;
        const std::uint64_t held = frames * traced / shown;
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(held, 1, sNoiseBarFrames));
    }

    std::optional<double> blurredDifference(
        const Rtx::PngImage& picture, const Rtx::PngImage& reference, const float sigma)
    {
        if (!comparable(picture, reference))
            return std::nullopt;

        const auto radius = static_cast<std::int32_t>(std::ceil(3.0f * sigma));
        std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
        float total = 0.0f;
        for (std::int32_t at = -radius; at <= radius; ++at)
        {
            const float weight = std::exp(-0.5f * static_cast<float>(at * at) / (sigma * sigma));
            kernel[static_cast<std::size_t>(at + radius)] = weight;
            total += weight;
        }
        for (float& weight : kernel)
            weight /= total;

        const auto width = static_cast<std::int32_t>(picture.mWidth);
        const auto height = static_cast<std::int32_t>(picture.mHeight);
        const auto index = [&](std::int32_t x, std::int32_t y) {
            return (static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * picture.mWidth
                       + static_cast<std::size_t>(std::clamp(x, 0, width - 1)))
                * 3;
        };

        std::vector<float> difference(std::size_t{ picture.mWidth } * picture.mHeight * 3);
        for (std::size_t pixel = 0; pixel < std::size_t{ picture.mWidth } * picture.mHeight; ++pixel)
            for (std::size_t channel = 0; channel < 3; ++channel)
                difference[pixel * 3 + channel] = static_cast<float>(picture.mPixels[pixel * 4 + channel])
                    - static_cast<float>(reference.mPixels[pixel * 4 + channel]);

        std::vector<float> across(difference.size());
        for (std::int32_t y = 0; y < height; ++y)
            for (std::int32_t x = 0; x < width; ++x)
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    float sum = 0.0f;
                    for (std::int32_t at = -radius; at <= radius; ++at)
                        sum += kernel[static_cast<std::size_t>(at + radius)] * difference[index(x + at, y) + channel];
                    across[index(x, y) + channel] = sum;
                }

        double worst = 0.0;
        for (std::int32_t y = 0; y < height; ++y)
            for (std::int32_t x = 0; x < width; ++x)
            {
                float pixelWorst = 0.0f;
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    float sum = 0.0f;
                    for (std::int32_t at = -radius; at <= radius; ++at)
                        sum += kernel[static_cast<std::size_t>(at + radius)] * across[index(x, y + at) + channel];
                    pixelWorst = std::max(pixelWorst, std::abs(sum));
                }
                worst += static_cast<double>(pixelWorst);
            }

        return worst / static_cast<double>(std::size_t{ picture.mWidth } * picture.mHeight);
    }

    int judgeNoise(
        const std::filesystem::path& wrote, const std::span<const std::string> places, const std::uint32_t barFrames)
    {
        const auto read = [&](const std::string& place, const std::string_view suffix) {
            return Rtx::readPng(wrote / (place + std::string(suffix) + ".png"));
        };

        std::uint32_t noisier = 0;
        std::uint32_t missing = 0;
        for (const std::string& place : places)
        {
            const Rtx::PngImage reference = read(place, sNoiseReferenceSuffix);
            const Rtx::PngImage frameMean = read(place, sNoiseMeanSuffix);
            const Rtx::PngImage barLimit = read(place, sNoiseBarLimitSuffix);
            const PictureError frame = measureError(read(place, ""), frameMean);
            const PictureError bar = measureError(read(place, sNoiseBarSuffix), barLimit);
            const std::optional<double> frameBias = blurredDifference(frameMean, reference, sNoiseBiasBlur);
            const std::optional<double> barBias = blurredDifference(barLimit, reference, sNoiseBiasBlur);
            if (frame.mMismatched || bar.mMismatched || !frameBias.has_value() || !barBias.has_value())
            {
                out() << std::format("  {:<28} a picture is missing, or of another size\n", place);
                ++missing;
                continue;
            }

            const bool clean = frame.mMean <= bar.mMean && frame.mP99 <= bar.mP99;
            if (!clean)
                ++noisier;

            out() << std::format(
                "  {:<28} noise: frame mean {:.2f} p99 {}, {} averaged mean {:.2f} p99 {} — {}; bias: frame {:.2f}, "
                "{} averaged {:.2f}\n",
                place, frame.mMean, frame.mP99, barFrames, bar.mMean, bar.mP99, clean ? "as clean" : "noisier",
                *frameBias, barFrames, *barBias);
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

    int compareRuns(const std::filesystem::path& wrote, const std::filesystem::path& against,
        const std::span<const std::string> files, const std::span<const std::string> frames)
    {
        if (against.empty())
            return 0;

        out() << std::format("{} {} against {}\n", files.size(), files.size() == 1 ? "picture" : "pictures",
            Files::pathToUnicodeString(against));

        std::uint32_t differing = 0;
        std::uint32_t unmatched = 0;

        for (const std::string& file : files)
        {
            const Rtx::PngImage drawn = Rtx::readPng(wrote / file);
            const Rtx::PngImage reference = Rtx::readPng(against / file);
            const FrameDifference difference = compareFrames(reference, drawn);
            const bool frame = std::find(frames.begin(), frames.end(), file) != frames.end();

            out() << std::format("  {:<36} {}{}\n", file, describe(difference),
                frame && !difference.same() ? ", which the hashes judge" : "");

            if (frame)
                continue;

            if (difference.mMismatched)
                ++unmatched;
            else if (!difference.same())
                ++differing;
        }

        const std::size_t judged = files.size() - frames.size();
        const std::string_view frameNote = frames.empty() ? "" : "; the frames are judged by their hashes above";

        if (differing == 0 && unmatched == 0)
        {
            out() << std::format("  every picture judged here is the same{}\n", frameNote);
            return 0;
        }

        out() << std::format("  {} of {} pictures moved, {} had nothing to compare against{}\n", differing, judged,
            unmatched, frameNote);

        return 1;
    }
}
