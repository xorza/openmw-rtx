#include "digestimages.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/shaders/shared/fields.h>

namespace Rtx
{
    DigestImages::DigestImages(const std::array<const Image*, Shaders::DIGEST_IMAGES>& channels,
        const std::uint32_t channelsInFields, const std::array<const Image*, sDenoiserImageCount>& denoiser,
        const VkExtent2D extent)
        : mExtent(extent)
    {
        [[maybe_unused]] const auto reaches = [&](const Image& image, const bool inFields) {
            return image.getWidth() >= (inFields ? Shaders::storedWidth(extent.width) : extent.width)
                && image.getHeight() >= extent.height;
        };

        for (std::size_t at = 0; at < channels.size(); ++at)
        {
            const Image* const image = channels[at];
            const bool inFields = (channelsInFields & (1u << at)) != 0;
            assert((image == nullptr || reaches(*image, inFields)) && "a read past an image's edge");
            assert((image == nullptr || !holdsWords(image->getFormat())) && "a channel of words bound as floats");
            mFloats[at] = image;
            mFloatsInFields |= image != nullptr && inFields ? 1u << at : 0u;
        }

        for (std::size_t at = 0; at < denoiser.size(); ++at)
        {
            const std::size_t image = Shaders::DIGEST_IMAGES + at;
            const Slot slot = slotOf(image);
            const Image* const held = denoiser[at];
            assert((held == nullptr || reaches(*held, true)) && "a read past an image's edge");
            assert((held == nullptr || holdsWords(held->getFormat()) == slot.mWords)
                && "a denoiser image bound as the kind of texel it does not hold");
            if (slot.mWords)
                mWords[slot.mAt] = held;
            else
            {
                mFloats[slot.mAt] = held;
                mFloatsInFields |= held != nullptr ? 1u << slot.mAt : 0u;
            }
            mDenoiserTaken |= held != nullptr ? 1u << at : 0u;
        }
    }

    DigestImages::Run DigestImages::run(const std::uint32_t run, const Image& noFloats, const Image& noWords) const
    {
        assert(run < sRuns);

        // Which image of the frame stands at each place of a kind, for the run to say which it holds.
        constexpr auto sImagesOfKind = [] {
            struct Images
            {
                std::array<std::size_t, sFloats> mFloats{};
                std::array<std::size_t, sWords> mWords{};
            } images;
            for (std::size_t image = 0; image < sCount; ++image)
            {
                const Slot slot = slotOf(image);
                (slot.mWords ? images.mWords[slot.mAt] : images.mFloats[slot.mAt]) = image;
            }
            return images;
        }();

        Run out;
        for (std::uint32_t slot = 0; slot < out.mFloatViews.size(); ++slot)
        {
            const std::size_t at = run * Shaders::DIGEST_FLOAT_SLOTS + slot;
            const Image* const image = at < mFloats.size() ? mFloats[at] : nullptr;
            out.mFloatViews[slot] = (image != nullptr ? *image : noFloats).describeStorage();
            out.mFloatImages[slot] = image != nullptr ? sImagesOfKind.mFloats[at] : sCount;
            out.mFloats |= image != nullptr ? 1u << slot : 0u;
            out.mFloatsInFields |= image != nullptr && (mFloatsInFields & (1u << at)) != 0 ? 1u << slot : 0u;
        }
        for (std::uint32_t slot = 0; slot < out.mWordViews.size(); ++slot)
        {
            const std::size_t at = run * Shaders::DIGEST_WORD_SLOTS + slot;
            const Image* const image = at < mWords.size() ? mWords[at] : nullptr;
            out.mWordViews[slot] = (image != nullptr ? *image : noWords).describeStorage();
            out.mWordImages[slot] = image != nullptr ? sImagesOfKind.mWords[at] : sCount;
            out.mWords |= image != nullptr ? 1u << slot : 0u;
        }
        return out;
    }
}
