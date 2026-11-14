#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <MyGUI_RenderFormat.h>

#include <components/rtx/renderer/guirenderer.hpp>

#include "slottexture.hpp"

namespace Resource
{
    class ImageManager;
}

namespace MyGUIRtx
{
    /// A picture of the interface's own, made by `createManual` or loaded from a file, and written through
    /// MyGUI's `lock` and `unlock`.
    ///
    /// **The pixels are written once.** The buffer `lock` hands out is the renderer's own, the memory its
    /// copy to the device reads; a buffer of this class's own would put a second copy of every pixel in
    /// front of it. A picture that changes every frame is shared instead (`RenderManager::shareTexture`).
    ///
    /// **The exception is a format the table does not hold.** One, two or three channels are widened on the
    /// way out, so they land in `mPixels` first: the widening has to read them, and the memory the renderer
    /// lends is written far faster than it is read.
    class Texture final : public SlotTexture
    {
    public:
        Texture(std::string name, Rtx::GuiRenderer& renderer, Resource::ImageManager* imageManager);

        void createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format) override;
        void loadFromFile(const std::string& fname) override;

        void destroy() override;

        /// The whole surface, to be filled and handed back with `unlock`.
        ///
        /// **What comes back holds no picture.** `TextureUsage::Write` is a promise to fill the
        /// buffer, which every caller in this fork keeps: at four channels the bytes are the
        /// renderer's own and hold whatever it last copied out of them. Reading them is slow as well
        /// as wrong — see the class comment.
        void* lock(MyGUI::TextureUsage access) override;
        void unlock() override;
        bool isLocked() const override { return mLocked; }

        MyGUI::PixelFormat getFormat() const override { return mFormat; }
        MyGUI::TextureUsage getUsage() const override { return mUsage; }
        size_t getNumElemBytes() const override { return mNumElemBytes; }

    private:
        /// Forgets the format with the slot, so that a second `createManual` starts clean.
        void release();

        /// Widens `mPixels` into the renderer's own bytes and sends them, four channels out of
        /// however few MyGUI asked for.
        void widen();

        Resource::ImageManager* mImageManager;

        MyGUI::PixelFormat mFormat = MyGUI::PixelFormat::Unknow;
        MyGUI::TextureUsage mUsage = MyGUI::TextureUsage::Default;
        std::size_t mNumElemBytes = 0;

        /// What MyGUI fills between a lock and its unlock at fewer than four channels, and for a slot the
        /// renderer refused at any count; empty otherwise.
        std::vector<std::uint8_t> mPixels;
        bool mLocked = false;
    };
}
