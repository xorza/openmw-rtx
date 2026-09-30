#include "presenttarget.hpp"

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/display/tonepass.hpp>

namespace Rtx
{
    void PresentTarget::resize(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        // Drawn into as well as written: the tone curve writes it as a storage image and the GUI
        // rasterises over what that left.
        mImage = Image(device, width, height, TonePass::sTargetFormat,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            "target");

        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            const VkClearColorValue black{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
            mImage.clear(commands, Use::sUndefined, black, Use::sAnyGeneral);
        });
    }
}
