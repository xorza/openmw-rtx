#include "compare.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/shaders/colour.h>

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
            return !one.mSamples.empty() && !other.mSamples.empty() && one.mWidth == other.mWidth
                && one.mHeight == other.mHeight;
        }

        /// The light of the pixel whose first sample is `at`: the luminance of its channels decoded,
        /// so a ratio of two is a ratio of light and not of the levels the curve encoded it in. **By
        /// the curve itself and not a byte's table**, since a mean stands between the bytes.
        double lightOf(const Rtx::PngImage& image, const std::size_t at)
        {
            const auto decoded = [&](const std::size_t channel) {
                return Rtx::Shaders::decodeSrgb(static_cast<float>(image.mSamples[at + channel]) / 65535.0f);
            };
            return static_cast<double>(
                osg::Vec3f(decoded(0), decoded(1), decoded(2)) * Rtx::Shaders::LUMINANCE_WEIGHTS);
        }

        /// The difference of the pixel whose first sample is `at`: its worst colour channel, on the
        /// sixteen-bit scale. Colour only, because alpha is not the picture.
        std::uint32_t worstChannel(const Rtx::PngImage& one, const Rtx::PngImage& other, const std::size_t at)
        {
            std::uint32_t worst = 0;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto a = static_cast<std::int32_t>(one.mSamples[at + channel]);
                const auto b = static_cast<std::int32_t>(other.mSamples[at + channel]);
                worst = std::max(worst, static_cast<std::uint32_t>(std::abs(a - b)));
            }

            return worst;
        }

        /// How a difference reads on one line.
        std::string describe(const FrameDifference& difference)
        {
            if (difference.mMismatched)
                return "of another size than its reference";

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

        for (std::size_t at = 0; at + 3 < before.mSamples.size(); at += 4)
        {
            const std::uint32_t worst = worstChannel(before, after, at);
            if (worst > 0)
            {
                ++difference.mDiffering;
                difference.mWorst
                    = std::max(difference.mWorst, (worst + Rtx::sSamplesPerLevel - 1) / Rtx::sSamplesPerLevel);
            }
        }

        return difference;
    }

    PictureVerdict judgePicture(const FrameDifference& difference, const PictureRule rule)
    {
        if (rule == PictureRule::Hashed)
            return PictureVerdict::Measured;

        if (difference.mMismatched)
            return PictureVerdict::NoReference;

        if (difference.same())
            return PictureVerdict::Same;

        if (rule == PictureRule::Denoised && difference.mWorst <= sDenoiserNoiseLevels)
            return PictureVerdict::WithinDenoiserNoise;

        return PictureVerdict::Moved;
    }

    PictureError measureError(const Rtx::PngImage& picture, const Rtx::PngImage& reference)
    {
        if (!comparable(picture, reference))
            return PictureError{ .mMismatched = true };

        // On the sixteen-bit scale, so a mean's fraction of a level is counted and not rounded away.
        std::vector<std::uint64_t> counts(65536);
        std::uint64_t sum = 0;
        for (std::size_t at = 0; at + 3 < picture.mSamples.size(); at += 4)
        {
            const std::uint32_t worst = worstChannel(picture, reference, at);
            ++counts[worst];
            sum += worst;
        }

        const std::uint64_t pixels = std::uint64_t{ picture.mWidth } * picture.mHeight;

        // Counted in whole pixels, rounded up: at least ninety-nine in a hundred are within it.
        const std::uint64_t needed = (pixels * 99 + 99) / 100;
        std::uint64_t within = 0;
        std::uint32_t sample = 0;
        while ((within += counts[sample]) < needed)
            ++sample;

        constexpr auto perLevel = static_cast<double>(Rtx::sSamplesPerLevel);
        return PictureError{
            .mMean = static_cast<double>(sum) / perLevel / static_cast<double>(pixels),
            .mP99 = static_cast<double>(sample) / perLevel,
        };
    }

    std::optional<double> fireflyShare(const Rtx::PngImage& picture, const Rtx::PngImage& reference)
    {
        if (!comparable(picture, reference))
            return std::nullopt;

        const double floor = static_cast<double>(Rtx::toLinear(sNoiseFireflyFloor));
        const std::size_t pixels = std::size_t{ picture.mWidth } * picture.mHeight;
        std::size_t fireflies = 0;
        for (std::size_t at = 0; at < pixels * 4; at += 4)
        {
            const double light = lightOf(picture, at);
            const double truth = lightOf(reference, at);
            fireflies += light > sNoiseFireflyRatio * truth && light - truth >= floor ? 1 : 0;
        }

        return 1000.0 * static_cast<double>(fireflies) / static_cast<double>(pixels);
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
                difference[pixel * 3 + channel] = (static_cast<float>(picture.mSamples[pixel * 4 + channel])
                                                      - static_cast<float>(reference.mSamples[pixel * 4 + channel]))
                    / static_cast<float>(Rtx::sSamplesPerLevel);

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

    Misc::Result<void, std::string> checkAgainst(const std::filesystem::path& out, const std::filesystem::path& against)
    {
        if (against.empty() || !std::filesystem::exists(against) || !std::filesystem::exists(out)
            || !std::filesystem::equivalent(out, against))
            return {};

        return Misc::Err{ std::format(
            "--against={} is where this run writes; name another --out", Files::pathToUnicodeString(against)) };
    }

    void clearPictures(const std::filesystem::path& out, const std::span<const WrittenPicture> pictures)
    {
        for (const WrittenPicture& picture : pictures)
            std::filesystem::remove(out / picture.mFile);
    }

    int compareRuns(const std::filesystem::path& wrote, const std::filesystem::path& against,
        const std::span<const WrittenPicture> pictures)
    {
        if (against.empty())
            return 0;

        out() << std::format("{} {} against {}\n", pictures.size(), pictures.size() == 1 ? "picture" : "pictures",
            Files::pathToUnicodeString(against));

        // Asked to compare and given nothing: "nothing moved" would be a pass that compared nothing.
        if (pictures.empty())
        {
            out() << "  this run drew no picture to compare\n";
            return 1;
        }

        std::uint32_t moved = 0;
        std::uint32_t unmatched = 0;
        std::uint32_t noisy = 0;
        std::size_t hashed = 0;

        for (const WrittenPicture& picture : pictures)
        {
            const Misc::Result<Rtx::PngImage, std::string> drawn = Rtx::readPng(wrote / picture.mFile);
            const Misc::Result<Rtx::PngImage, std::string> reference = Rtx::readPng(against / picture.mFile);
            const FrameDifference difference = drawn.isOk() && reference.isOk()
                ? compareFrames(reference.value(), drawn.value())
                : FrameDifference{ .mMismatched = true };
            const PictureVerdict verdict = judgePicture(difference, picture.mRule);

            std::string_view note;
            if (verdict == PictureVerdict::WithinDenoiserNoise)
                note = ", within the denoiser's noise on this card";
            else if (verdict == PictureVerdict::Measured && !difference.same())
                note = ", which the hashes judge";

            const std::string said = !reference.isOk() ? "no reference: " + reference.error()
                : !drawn.isOk()                        ? "not read back: " + drawn.error()
                                                       : describe(difference);
            out() << std::format("  {:<36} {}{}\n", picture.mFile, said, note);

            hashed += picture.mRule == PictureRule::Hashed ? 1 : 0;
            moved += verdict == PictureVerdict::Moved ? 1 : 0;
            unmatched += verdict == PictureVerdict::NoReference ? 1 : 0;
            noisy += verdict == PictureVerdict::WithinDenoiserNoise ? 1 : 0;
        }

        const std::size_t judged = pictures.size() - hashed;
        const std::string_view hashNote = hashed == 0 ? "" : "; the frames are judged by their hashes above";
        const std::string noiseNote
            = noisy == 0 ? std::string() : std::format(", {} within the denoiser's noise on this card", noisy);

        if (moved == 0 && unmatched == 0)
        {
            out() << std::format("  {}{}{}\n",
                noisy == 0 ? "every picture judged here is the same" : "no picture judged here moved", noiseNote,
                hashNote);
            return 0;
        }

        out() << std::format("  {} of {} pictures moved, {} had nothing to compare against{}{}\n", moved, judged,
            unmatched, noiseNote, hashNote);

        return 1;
    }
}
