#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "hostbuffer.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;
    class Image;

    /// Every texture the GUI draws with, addressed by slot.
    ///
    /// **Not the scene's bindless array, and deliberately nothing like it.** That one is indexed by
    /// material, sized to the world and appended to when a cell arrives; these are a font atlas, a
    /// skin sheet, a map and a video frame — a handful of images with nothing to do with what is
    /// being traced, arriving and leaving as windows open and close.
    ///
    /// **A slot a texture gave back is taken over before the table grows**, so a session of opening
    /// and closing menus does not walk the table upwards forever.
    class GuiTextures
    {
    public:
        GuiTextures(const Device& device, CommandPool& pool);
        ~GuiTextures();

        GuiTextures(const GuiTextures&) = delete;
        GuiTextures& operator=(const GuiTextures&) = delete;

        /// A slot holding a texture of this size, cleared to nothing.
        std::uint32_t add(std::uint32_t width, std::uint32_t height);

        /// The whole texture, four bytes a pixel, tightly packed, row zero first.
        ///
        /// **All of it, because MyGUI's interface has no way to say less.** It hands out a buffer to
        /// fill and takes it back filled; there is no rectangle in that and so none here.
        void write(std::uint32_t slot, std::span<const std::uint8_t> rgba);

        void drop(std::uint32_t slot);

        /// What the pass samples, or null where nothing holds that slot.
        VkImageView getView(std::uint32_t slot) const;

        /// The image itself, for a caller that draws into it rather than handing over pixels, or
        /// null where nothing holds that slot. Kept in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`,
        /// which is where anything that borrows it has to put it back.
        const Image* getImage(std::uint32_t slot) const;

        /// The whole texture in main memory, four bytes a pixel. Costs a transfer off the device.
        void read(std::uint32_t slot, std::vector<std::uint8_t>& pixels) const;

    private:
        const Device& mDevice;
        CommandPool& mPool;

        std::vector<std::unique_ptr<Image>> mImages;
        std::vector<std::uint32_t> mFree;

        /// Grown to the largest texture written so far and reused after that: a video frame arrives
        /// through here once a frame and must not allocate to do it.
        HostBuffer mStaging;
    };
}
