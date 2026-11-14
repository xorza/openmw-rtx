#include "rtxwindow.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>
#include <osg/Camera>

#include <apps/openmw/mwrender/renderer.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/sdlutil/sdlvideowrapper.hpp>

namespace MWRender
{
    namespace
    {
        /// How long the window must report one size before the renderer is rebuilt for it: a new extent
        /// releases and allocates every target again, about a tenth of a second, and a window dragged across
        /// a screen passes through hundreds of extents. A gesture is followed once it stops, and until then
        /// the compositor shows the picture scaled. Six frames at sixty.
        constexpr double sSettleSeconds = 0.1;
    }

    RtxWindow::RtxWindow(
        const std::optional<SDL_WindowFlags> surface, const bool hidden, const std::optional<osg::Vec2i> pixels)
        : mHasSurface(surface.has_value())
    {
        // The backend's own flags, and no `SDL_GL_SetAttribute`: no GL context is ever made.
        mWindow.reset(openWindow(surface.value_or(SDL_WindowFlags{ 0 }), pixels));
        if (mWindow == nullptr)
            throw Rtx::Unsupported(std::string("the window could not be made: ") + SDL_GetError());
        if (const char* const driver = SDL_GetCurrentVideoDriver())
            Crash::annotate("video driver", driver);

        // Hidden and not absent: a surface still needs a window, and a swapchain on one nobody is looking at
        // costs a present per frame, so a headless run is the same renderer rather than a second path.
        if (hidden)
            return;
        SDL_ShowWindow(mWindow.get());

        // A hidden Wayland window takes the largest scale of any display, and keeps its size in
        // points on the display it shows on: a run's pixels are made again at that one's density.
        if (!pixels.has_value())
            return;
        SDL_SyncWindow(mWindow.get());
        if (readSize() == *pixels)
            return;
        const float density = SDLUtil::pixelDensity(mWindow.get());
        SDL_SetWindowSize(
            mWindow.get(), SDLUtil::windowPoints(pixels->x(), density), SDLUtil::windowPoints(pixels->y(), density));
        SDL_SyncWindow(mWindow.get());
    }

    bool RtxWindow::hasDisplay()
    {
        const char* const driver = SDL_GetCurrentVideoDriver();
        return driver == nullptr || std::string_view(driver) != "offscreen";
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

        // The renderer's own extent, which everything above reads through the viewport.
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
