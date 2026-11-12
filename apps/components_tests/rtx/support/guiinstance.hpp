#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <MyGUI_UString.h>

#include <components/resource/imagemanager.hpp>
#include <components/vfs/filemap.hpp>
#include <components/vfs/manager.hpp>

#include "countingrenderer.hpp"

namespace Gui
{
    class InterfaceFonts;
}

namespace MyGUI
{
    class Gui;
}

namespace MyGUIPlatform
{
    class Platform;
}

namespace MyGUIRtx
{
    class RenderManager;
}

namespace Rtx::Testing
{
    /// What one frame of interface came to: the renderer's last `drawGui`.
    struct DrawnGui
    {
        std::span<const Rtx::GuiVertex> mVertices;
        std::span<const Rtx::GuiBatch> mBatches;
    };

    /// What a `GuiInstance` is stood up at.
    struct GuiSetup
    {
        /// How many frame pixels a unit of the interface takes, as `resizeView` is handed it; the
        /// fonts are rasterized at it, rounded up, as the game rasterizes them.
        float mScale = 1.0f;

        /// The frame the interface is laid out over, in pixels.
        std::uint32_t mWidth = 1024;
        std::uint32_t mHeight = 768;

        /// The size the fonts are set at, in points: `[GUI] font size`'s default.
        int mFontSize = 16;

        /// Files that stand over `files/data`'s under their paths, which the test owns.
        VFS::FileMap mOverrides = {};
    };

    /// MyGUI over the ray tracer's GUI backend and a renderer that records what it is handed, with the
    /// fork's own fonts, layers and text skins out of `files/data`, stood up in the order the window
    /// manager stands the game's up. What a test of the interface's fonts and texts drives, since only
    /// a real MyGUI says what a widget draws against.
    ///
    /// **One at a time**: MyGUI's managers are singletons, and a second instance asserts.
    ///
    /// **Every tag answers white.** The skins colour their text with `#{fontcolour=…}`, which the
    /// game answers from the content's fallbacks; a test of these reads geometry and textures.
    ///
    /// Pinned: the platform and MyGUI keep the VFS, the image manager and the renderer.
    class GuiInstance
    {
    public:
        explicit GuiInstance(GuiSetup setup = {});
        ~GuiInstance();

        GuiInstance(const GuiInstance&) = delete;
        GuiInstance& operator=(const GuiInstance&) = delete;
        GuiInstance(GuiInstance&&) = delete;
        GuiInstance& operator=(GuiInstance&&) = delete;

        CountingRenderer& getRenderer() { return mRenderer; }
        Gui::InterfaceFonts& getFonts() { return *mFonts; }

        /// How many files the interface opened from the VFS since it was made.
        std::uint32_t getVfsReads() const { return mVfsReads; }

        /// Lays out and draws one frame, and answers what the renderer was handed.
        DrawnGui draw();

    private:
        void onRetrieveTag(const MyGUI::UString& tag, MyGUI::UString& result);

        std::uint32_t mVfsReads = 0;
        VFS::Manager mVfs;
        Resource::ImageManager mImages;
        CountingRenderer mRenderer;
        MyGUIRtx::RenderManager* mRenderManager = nullptr;
        std::unique_ptr<MyGUIPlatform::Platform> mPlatform;
        std::unique_ptr<MyGUI::Gui> mGui;
        std::unique_ptr<Gui::InterfaceFonts> mFonts;
    };
}
