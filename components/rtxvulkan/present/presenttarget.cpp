#include "presenttarget.hpp"

#include <cassert>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/display/tonepass.hpp>

namespace Rtx
{
    namespace
    {
        /// The curve writes the picture as a storage image, the debug lines are drawn over it, the
        /// interface samples it, and a read back copies it.
        ImageDescription pictureDescription(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = TonePass::sTargetFormat,
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT };
        }

        /// The interface draws what is shown whole, and a present and a read back copy it.
        ImageDescription shownDescription(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = TonePass::sTargetFormat,
                .mUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
        }

        /// The curve writes it as a storage image, and a read back copies it.
        ImageDescription deepDescription(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = TonePass::sDeepFormat,
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        }
    }

    void PresentTarget::resize(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        if (isOpen() && width == mShown.getWidth() && height == mShown.getHeight())
            return;

        mPicture = Image(MemoryUse::Frame, device, pictureDescription(width, height), "picture");
        mShown = Image(MemoryUse::Frame, device, shownDescription(width, height), "shown");

        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            const VkClearColorValue black{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
            mPicture.clear(commands, Use::sUndefined, black, sResting);
            mShown.clear(commands, Use::sUndefined, black, sResting);
        });

        mDeep = Image();
        mShownCurrent = false;
        mDeepCurrent = false;
    }

    void PresentTarget::release()
    {
        mPicture = Image();
        mShown = Image();
        mDeep = Image();
        mShownCurrent = false;
        mDeepCurrent = false;
    }

    VkDeviceSize PresentTarget::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        return Image::bytesFor(device, pictureDescription(width, height))
            + Image::bytesFor(device, shownDescription(width, height))
            + Image::bytesFor(device, deepDescription(width, height));
    }

    Image* PresentTarget::beginPicture(const Device& device, const bool deep)
    {
        assert(isOpen());

        mShownCurrent = false;
        mDeepCurrent = deep;
        if (!deep)
            return nullptr;

        // Left undefined: the curve writes it whole before anything reads it.
        if (mDeep.isEmpty())
            mDeep = Image(
                MemoryUse::Frame, device, deepDescription(mPicture.getWidth(), mPicture.getHeight()), "deep picture");

        return &mDeep;
    }
}
