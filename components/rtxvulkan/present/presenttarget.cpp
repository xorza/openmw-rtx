#include "presenttarget.hpp"

#include <cassert>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/display/tonepass.hpp>

namespace Rtx
{
    void PresentTarget::resize(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        // The curve writes the picture as a storage image, the debug lines are drawn over it, the
        // interface samples it, and a read back copies it.
        mPicture = Image(device, width, height, TonePass::sTargetFormat,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            "picture");

        // The interface draws what is shown whole, and a present and a read back copy it.
        mShown = Image(device, width, height, TonePass::sTargetFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            "shown");

        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            const VkClearColorValue black{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
            mPicture.clear(commands, Use::sUndefined, black, sResting);
            mShown.clear(commands, Use::sUndefined, black, sResting);
        });

        mDeep = Image();
    }

    Image& PresentTarget::requireDeep(const Device& device)
    {
        assert(isOpen());

        // The curve writes it as a storage image, and a read back copies it. Left undefined: the
        // curve writes it whole before anything reads it.
        if (mDeep.isEmpty())
            mDeep = Image(device, mPicture.getWidth(), mPicture.getHeight(), TonePass::sDeepFormat,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "deep picture");

        return mDeep;
    }
}
