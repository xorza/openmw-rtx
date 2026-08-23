#include "guitextures.hpp"

#include <cassert>

#include "commands.hpp"
#include "device.hpp"
#include "image.hpp"

namespace Rtx
{
    GuiTextures::GuiTextures(const Device& device, CommandPool& pool)
        : mDevice(device)
        , mPool(pool)
    {
    }

    GuiTextures::~GuiTextures() = default;

    std::uint32_t GuiTextures::add(std::uint32_t width, std::uint32_t height)
    {
        auto image = std::make_unique<Image>(mDevice, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            "gui texture");

        // **Cleared rather than left undefined.** A slot is sampleable from the moment it exists, so
        // a batch drawn before the first write shows nothing instead of whatever the memory held —
        // and the pass never has to ask whether a texture is ready.
        mPool.submitAndWait([&](VkCommandBuffer commands) {
            image->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            const VkClearColorValue clear{};
            const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(commands, image->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &whole);

            image->transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });

        if (!mFree.empty())
        {
            const std::uint32_t slot = mFree.back();
            mFree.pop_back();
            mImages[slot] = std::move(image);
            return slot;
        }

        mImages.push_back(std::move(image));
        return static_cast<std::uint32_t>(mImages.size() - 1);
    }

    void GuiTextures::write(std::uint32_t slot, std::span<const std::uint8_t> rgba)
    {
        assert(slot < mImages.size() && mImages[slot] != nullptr && "a write to a slot nothing holds");

        const Image& image = *mImages[slot];
        const VkDeviceSize bytes = VkDeviceSize{ image.getWidth() } * image.getHeight() * 4;
        assert(rgba.size() == bytes && "the whole texture, four bytes a pixel, tightly packed");

        if (mStaging.getSize() < bytes)
            mStaging = HostBuffer(mDevice, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        mStaging.write(rgba);

        mPool.submitAndWait([&](VkCommandBuffer commands) {
            image.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            const VkBufferImageCopy region{
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageExtent = { image.getWidth(), image.getHeight(), 1 },
            };
            vkCmdCopyBufferToImage(
                commands, mStaging.getHandle(), image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
    }

    void GuiTextures::drop(std::uint32_t slot)
    {
        assert(slot < mImages.size() && mImages[slot] != nullptr && "a slot given back twice");

        // Nothing is in flight to hold it: every submit here waits, and so does the one that drew
        // with it.
        mImages[slot].reset();
        mFree.push_back(slot);
    }

    const Image* GuiTextures::getImage(std::uint32_t slot) const
    {
        if (slot >= mImages.size())
            return nullptr;

        return mImages[slot].get();
    }

    void GuiTextures::read(std::uint32_t slot, std::vector<std::uint8_t>& pixels) const
    {
        assert(slot < mImages.size() && mImages[slot] != nullptr && "a read of a slot nothing holds");

        mImages[slot]->read(mPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pixels);
    }

    VkImageView GuiTextures::getView(std::uint32_t slot) const
    {
        if (slot >= mImages.size() || mImages[slot] == nullptr)
            return VK_NULL_HANDLE;

        return mImages[slot]->getView();
    }
}
