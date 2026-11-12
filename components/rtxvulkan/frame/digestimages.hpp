#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/shaders/shared/channeldigest.h>

namespace Rtx
{
    class Image;

    /// The frame's images a read of them takes — the digest's (`DigestPass`) and what a counting device keeps
    /// (`NotFiniteKeep`): every channel, the direct one's slot holding the composed frame, then every image of
    /// the denoiser (`DenoiserImage`), each null where the frame has none of it. Bound in runs of
    /// `DIGEST_FLOAT_SLOTS` images of floats and `DIGEST_WORD_SLOTS` of words, since a pass's set is pushed:
    /// the floats, the channels first, then the words, each image at its place in that order.
    class DigestImages
    {
    public:
        static constexpr std::size_t sCount = Shaders::DIGEST_IMAGES + sDenoiserImageCount;

        /// Which of the denoiser's images hold words — fast means, shadow words and moments — which a float
        /// view cannot read, at each one's index. A slot's own and not read off the image, because the layout
        /// binds each kind apart; the constructor holds each image to it.
        static constexpr std::array<bool, sDenoiserImageCount> sDenoiserWords = [] {
            std::array<bool, sDenoiserImageCount> words{};
            for (const DenoiserImage image : { DenoiserImage::AccumulateFast, DenoiserImage::SkyShadowMoments,
                     DenoiserImage::LampShadowMoments, DenoiserImage::PaneFast, DenoiserImage::SpecularFast,
                     DenoiserImage::SkyShadowLevel0, DenoiserImage::LampShadowLevel0, DenoiserImage::SkyShadowLevel1,
                     DenoiserImage::LampShadowLevel1, DenoiserImage::SkyShadowLevel2, DenoiserImage::LampShadowLevel2 })
                words[static_cast<std::size_t>(image)] = true;
            return words;
        }();

        /// Where image `image` of the frame stands, counted as `sCount` counts them: among the floats or
        /// among the words, and its place among its own kind.
        struct Slot
        {
            bool mWords;
            std::uint32_t mAt;
        };
        static constexpr Slot slotOf(const std::size_t image)
        {
            const auto words = [](const std::size_t at) {
                return at >= Shaders::DIGEST_IMAGES && sDenoiserWords[at - Shaders::DIGEST_IMAGES];
            };
            std::uint32_t before = 0;
            for (std::size_t other = 0; other < image; ++other)
                before += words(other) == words(image) ? 1u : 0u;
            return Slot{ .mWords = words(image), .mAt = before };
        }

        /// How many images of each kind the frame binds, and so how many runs a dispatch over every one takes.
        static constexpr auto sWords = static_cast<std::uint32_t>(std::ranges::count(sDenoiserWords, true));
        static constexpr std::uint32_t sFloats = static_cast<std::uint32_t>(sCount) - sWords;
        static constexpr std::uint32_t sRuns
            = std::max((sFloats + Shaders::DIGEST_FLOAT_SLOTS - 1) / Shaders::DIGEST_FLOAT_SLOTS,
                (sWords + Shaders::DIGEST_WORD_SLOTS - 1) / Shaders::DIGEST_WORD_SLOTS);
        static_assert(sFloats <= 32, "a float image past the bits that say which are in fields");

        /// What one run binds: the views of its slots, a stand-in's where nothing is, which image of the
        /// frame each slot holds, `sCount` where none, which slots are bound, a bit each, and which float
        /// slots are stored in fields (`storedOf`), read through the mapping as every word slot is.
        struct Run
        {
            std::array<VkDescriptorImageInfo, Shaders::DIGEST_FLOAT_SLOTS> mFloatViews{};
            std::array<VkDescriptorImageInfo, Shaders::DIGEST_WORD_SLOTS> mWordViews{};
            std::array<std::size_t, Shaders::DIGEST_FLOAT_SLOTS> mFloatImages{};
            std::array<std::size_t, Shaders::DIGEST_WORD_SLOTS> mWordImages{};
            std::uint32_t mFloats = 0;
            std::uint32_t mFloatsInFields = 0;
            std::uint32_t mWords = 0;
        };

        /// @param channels the channels, each read over `extent` at each pixel's place: in its field, as the
        ///        frame lays them, for the channels `channelsInFields` names (a bit at each one's index).
        /// @param denoiser the denoiser's, all in fields, in `GENERAL` as the channels are.
        DigestImages(const std::array<const Image*, Shaders::DIGEST_IMAGES>& channels, std::uint32_t channelsInFields,
            const std::array<const Image*, sDenoiserImageCount>& denoiser, VkExtent2D extent);

        VkExtent2D getExtent() const { return mExtent; }

        /// Which of the denoiser's images the frame has, a bit at each one's index (`FrameDigest::mDenoiserTaken`).
        std::uint32_t getDenoiserTaken() const { return mDenoiserTaken; }

        /// Run `run`, of `sRuns`, with `noFloats` and `noWords` standing in where nothing is bound.
        Run run(std::uint32_t run, const Image& noFloats, const Image& noWords) const;

    private:
        /// The images of each kind in their order, null where the frame has none.
        std::array<const Image*, sFloats> mFloats{};
        std::array<const Image*, sWords> mWords{};
        std::uint32_t mFloatsInFields = 0;
        std::uint32_t mDenoiserTaken = 0;
        VkExtent2D mExtent{};
    };
}
