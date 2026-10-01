#pragma once

#include <limits>
#include <memory>

#include <SDL3/SDL_video.h>
#include <osg/Vec2i>

#include <components/misc/presentation.hpp>

namespace osg
{
    class Camera;
}

namespace Rtx
{
    class Renderer;
}

namespace MWRender
{
    /// The SDL window the ray tracer draws into: made here, fitted to as it changes size, and
    /// written to for the one thing this renderer says about itself, its speed. No GL context is
    /// ever made on it, which is the point of the whole path. Before the backend in whatever owns
    /// both, because the backend's surface is on it.
    class RtxWindow
    {
    public:
        /// Makes the window from the video settings; `hidden` is a headless run, the same window
        /// with nobody watching. Throws where SDL refuses.
        explicit RtxWindow(bool hidden);

        SDL_Window* get() const { return mWindow.get(); }

        /// The size SDL reports now, in pixels.
        osg::Vec2i readSize() const;

        /// Sizes the trace, the surface and the viewport to `presentation`, `now` being the host's
        /// clock (`Misc::FrameClock::getNow`): at once where the frame alone moved, and once the
        /// window has kept one size for `sSettleSeconds` where the window moved. Asked every frame,
        /// because a surface that stopped matching the window is remade by the call it makes.
        void fit(Rtx::Renderer& renderer, osg::Camera& camera, const Misc::Presentation& presentation, double now);

        /// Sizes the trace, the surface and the viewport to `presentation`, now: what `fit` does
        /// once the window settles, and what a renderer just made does before any frame has a
        /// clock.
        void apply(Rtx::Renderer& renderer, osg::Camera& camera, const Misc::Presentation& presentation);

        /// Writes the title, where somebody can see it: a hidden window keeps whatever it had.
        void setTitle(const char* title);

    private:
        std::unique_ptr<SDL_Window, void (*)(SDL_Window*)> mWindow{ nullptr, SDL_DestroyWindow };

        /// The window size last applied or asked for, and since when it has been asked for. Never
        /// is further back than any moment: a size applied stands settled.
        osg::Vec2i mAskedDrawable;
        double mAskedSince = -std::numeric_limits<double>::infinity();
    };
}
