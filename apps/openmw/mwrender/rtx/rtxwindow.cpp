#include "rtxwindow.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <osg/Camera>

#include <components/rtx/renderer/renderer.hpp>

#include "../renderer.hpp"

namespace MWRender
{
    namespace
    {
        /// How long the window must report one size before the renderer is rebuilt for it.
        ///
        /// **Because rebuilding costs about as long as this waits.** A new extent releases every
        /// target and allocates them again — about a tenth of a second. A window dragged across a
        /// screen passes through hundreds of extents, and following each of them would draw the
        /// drag at ten frames a second.
        ///
        /// So a gesture is followed once it stops. Until then the surface keeps the extent it has,
        /// and what the compositor shows is that picture scaled — which is what a window being
        /// dragged shows anyway.
        ///
        /// **Six frames at sixty.** Long enough that a drag settles into one rebuild, short enough
        /// that letting go of a window edge and seeing the picture follow reads as immediate.
        constexpr double sSettleSeconds = 0.1;
    }

    RtxWindow::RtxWindow(const bool hidden)
    {
        // **The backend's own flag, and no `SDL_GL_SetAttribute` anywhere near it.** No GL context is
        // ever made, which is the point of the whole path.
        mWindow.reset(openWindow(SDL_WINDOW_VULKAN));
        if (mWindow == nullptr)
            throw std::runtime_error(std::string("failed to create SDL window: ") + SDL_GetError());

        // **Hidden and not absent.** A surface still needs a window, and a swapchain built on one
        // nobody is looking at costs a present per frame and nothing else — so a headless run is
        // the same renderer rather than a second path through it. Never shown, it also keeps the
        // compositor from raising a window over whatever the person running it is doing.
        if (!hidden)
            SDL_ShowWindow(mWindow.get());
    }

    osg::Vec2i RtxWindow::readSize() const
    {
        osg::Vec2i size;
        SDL_GetWindowSizeInPixels(mWindow.get(), &size.x(), &size.y());
        return size;
    }

    void RtxWindow::fit(
        Rtx::Renderer& renderer, osg::Camera& camera, const Misc::Presentation& presentation, const double now)
    {
        if (presentation.mDrawable != mAskedDrawable)
        {
            mAskedDrawable = presentation.mDrawable;
            mAskedSince = now;
        }

        if (now - mAskedSince < sSettleSeconds)
            return;

        apply(renderer, camera, presentation);
    }

    void RtxWindow::apply(Rtx::Renderer& renderer, osg::Camera& camera, const Misc::Presentation& presentation)
    {
        mAskedDrawable = presentation.mDrawable;

        renderer.showIn(static_cast<std::uint32_t>(presentation.mDrawable.x()),
            static_cast<std::uint32_t>(presentation.mDrawable.y()));
        renderer.resize(
            static_cast<std::uint32_t>(presentation.mFrame.x()), static_cast<std::uint32_t>(presentation.mFrame.y()));

        // **The renderer's own extent**, which everything above reads through the viewport.
        const Rtx::FrameExtents extents = renderer.getExtents();
        camera.setViewport(0, 0, static_cast<int>(extents.mOutputWidth), static_cast<int>(extents.mOutputHeight));
    }

    void RtxWindow::setTitle(const char* title)
    {
        if ((SDL_GetWindowFlags(mWindow.get()) & SDL_WINDOW_HIDDEN) != 0)
            return;

        SDL_SetWindowTitle(mWindow.get(), title);
    }
}
